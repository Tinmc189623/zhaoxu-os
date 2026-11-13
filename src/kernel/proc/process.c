/*
 * Zhaoxu OS - ring 3 processes
 *
 * Every process owns a private address space (CR3) and one preemptive
 * kernel thread.  The kernel higher half is shared between all spaces, so a
 * context switch only has to reload CR3; the user window at 1 TiB is private.
 */
#include <boot.h>
#include <console.h>
#include <drivers.h>
#include <ipc.h>
#include <install.h>
#include <keyboard.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <rtc.h>
#include <shm.h>
#include <smp.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>
#include <vfs.h>
#include <zxe_format.h>

#define USER_LOAD_BASE      0x0000010000000000ULL   /* 1 TiB   */
#define USER_STACK_TOP      0x0000010100000000ULL   /* 1 TiB + 4 GiB */
#define USER_STACK_PAGES    32
#define USER_KERNEL_STACK   KERNEL_THREAD_STACK
#define USER_ADDRESS_LIMIT  0x0000018000000000ULL
#include "process_internal.h"

static struct process g_processes[MAX_PROCESSES];
static u64 g_next_pid = 1;
static u64 g_last_pid;

extern void user_enter(u64 entry, u64 stack);
void process_exit_current(int status);

struct elf64_header {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 program_header_offset;
    u64 section_header_offset;
    u32 flags;
    u16 header_size;
    u16 program_header_size;
    u16 program_header_count;
    u16 section_header_size;
    u16 section_header_count;
    u16 string_table_index;
} __attribute__((packed));

struct elf64_program_header {
    u32 type;
    u32 flags;
    u64 offset;
    u64 virtual_address;
    u64 physical_address;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
} __attribute__((packed));

#define ELF_PT_LOAD 1

static struct process *current_process(void)
{
    struct thread *thread = sched_current();
    return thread ? thread->proc : NULL;
}

struct process *process_current(void)
{
    return current_process();
}

static struct process *process_find(u64 pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_processes[i].used && g_processes[i].pid == pid)
            return &g_processes[i];
    }
    return NULL;
}

static struct process *process_alloc(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!g_processes[i].used) {
            memset(&g_processes[i], 0, sizeof(g_processes[i]));
            g_processes[i].used = 1;
            g_processes[i].pid = g_next_pid++;
            g_processes[i].state = PROC_RUNNING;
            return &g_processes[i];
        }
    }
    return NULL;
}

/* Write into a user virtual range through the identity map, so loading works
 * while the new address space is not the active CR3. */
int process_user_write(struct process *process, u64 virtual_address,
                      const void *source, u64 length)
{
    const u8 *cursor = (const u8 *)source;

    while (length) {
        u64 page = virtual_address & ~0xFFFULL;
        u64 offset = virtual_address & 0xFFF;
        u64 chunk = PAGE_SIZE - offset;
        if (chunk > length)
            chunk = length;

        u64 physical = vmm_space_get_physical(process->space, page);
        if (!physical)
            return -1;
        memcpy((u8 *)(uintptr_t)physical + offset, cursor, chunk);

        virtual_address += chunk;
        cursor += chunk;
        length -= chunk;
    }
    return 0;
}

static int user_zero(struct process *process, u64 virtual_address, u64 length)
{
    while (length) {
        u64 page = virtual_address & ~0xFFFULL;
        u64 offset = virtual_address & 0xFFF;
        u64 chunk = PAGE_SIZE - offset;
        if (chunk > length)
            chunk = length;

        u64 physical = vmm_space_get_physical(process->space, page);
        if (!physical)
            return -1;
        memset((u8 *)(uintptr_t)physical + offset, 0, chunk);

        virtual_address += chunk;
        length -= chunk;
    }
    return 0;
}

u64 process_map_page(struct process *process, u64 virtual_address,
                     u64 page_flags)
{
    u64 existing = vmm_space_get_physical(process->space, virtual_address);
    if (existing)
        return existing;
    if (process->mapping_count >= USER_MAX_PAGES)
        return 0;
    if (process->mapping_count == process->mapping_capacity) {
        int capacity = process->mapping_capacity
                           ? process->mapping_capacity * 2
                           : USER_MAP_INITIAL_CAPACITY;
        if (capacity > USER_MAX_PAGES)
            capacity = USER_MAX_PAGES;
        struct user_mapping *mappings =
            kmalloc((usize)capacity * sizeof(*mappings));
        if (!mappings)
            return 0;
        if (process->mapping_count)
            memcpy(mappings, process->mappings,
                   (usize)process->mapping_count * sizeof(*mappings));
        kfree(process->mappings);
        process->mappings = mappings;
        process->mapping_capacity = capacity;
    }

    u64 physical = pmm_alloc_page();
    if (!physical)
        return 0;
    if (vmm_space_map_page(process->space, virtual_address, physical,
                           VMM_PRESENT | VMM_USER | page_flags) != 0) {
        pmm_free_page(physical);
        return 0;
    }
    memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);

    int index = process->mapping_count++;
    process->mappings[index].virtual_address = virtual_address;
    process->mappings[index].physical_address = physical;
    return physical;
}

static void release_user_pages(struct process *process)
{
    if (!process)
        return;

    if (process->space) {
        for (int i = 0; i < process->mapping_count; i++) {
            u64 physical =
                vmm_space_unmap_page(process->space,
                                     process->mappings[i].virtual_address);
            if (physical)
                pmm_free_page(physical);
        }
    }
    process->mapping_count = 0;
    process->mapping_capacity = 0;
    kfree(process->mappings);
    process->mappings = NULL;
}

static int elf_load(struct process *process, const u8 *image, u64 image_size,
                    u64 *entry_out)
{
    if (!image || image_size < sizeof(struct elf64_header))
        return -1;

    const struct elf64_header *header = (const struct elf64_header *)image;
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F')
        return -1;
    if (header->ident[4] != 2 || header->ident[5] != 1)
        return -1;
    if (header->type != 2 || header->machine != 62)
        return -1;
    if (header->program_header_size < sizeof(struct elf64_program_header))
        return -1;
    if (header->program_header_count == 0 ||
        header->program_header_count > 32)
        return -1;

    u64 table_end = header->program_header_offset +
                    (u64)header->program_header_count *
                        header->program_header_size;
    if (table_end > image_size)
        return -1;

    for (u32 i = 0; i < header->program_header_count; i++) {
        const struct elf64_program_header *program =
            (const struct elf64_program_header *)
                (image + header->program_header_offset +
                 (u64)i * header->program_header_size);
        if (program->type != ELF_PT_LOAD || program->memory_size == 0)
            continue;
        if (program->file_size > program->memory_size)
            return -1;
        if (program->offset + program->file_size > image_size)
            return -1;
        if (program->virtual_address < USER_LOAD_BASE)
            return -1;
        if (program->virtual_address + program->memory_size <
            program->virtual_address)
            return -1;
        if (program->virtual_address + program->memory_size >
            USER_ADDRESS_LIMIT)
            return -1;

        u64 start = program->virtual_address & ~0xFFFULL;
        u64 end = (program->virtual_address + program->memory_size +
                   0xFFFULL) & ~0xFFFULL;

        u64 page_flags = (program->flags & 2) ? VMM_WRITABLE : 0;
        if (!(program->flags & 1))
            page_flags |= VMM_EXECUTE_DISABLE;
        if ((program->flags & 3) == 3)
            return -1;
        for (u64 page = start; page < end; page += PAGE_SIZE) {
            if (!process_map_page(process, page, page_flags))
                return -1;
        }

        if (program->file_size &&
            process_user_write(process, program->virtual_address,
                       image + program->offset, program->file_size) != 0)
            return -1;
        if (program->memory_size > program->file_size &&
            user_zero(process,
                      program->virtual_address + program->file_size,
                      program->memory_size - program->file_size) != 0)
            return -1;
    }

    if (header->entry < USER_LOAD_BASE ||
        header->entry >= USER_ADDRESS_LIMIT)
        return -1;

    *entry_out = header->entry;
    return 0;
}

static int zxe_load(struct process *process, const u8 *image, u64 image_size,
                    const char *path, u64 *entry_out)
{
    return zxe_load_image(process, image, image_size, path, entry_out);
}

static int image_load(struct process *process, const u8 *image, u64 image_size,
                      const char *path, u64 *entry_out)
{
    if (image_size >= 4 && memcmp(image, ZXE_MAGIC, 4) == 0)
        return zxe_load(process, image, image_size, path, entry_out);
    if (image_size >= 3 && image[0] == 'Z' && image[1] == 'X' && image[2] == 'E')
        return -1;
    return elf_load(process, image, image_size, entry_out);
}

static u64 build_user_stack(struct process *process, int argc,
                            const char **argv)
{
    u64 stack = USER_STACK_TOP - 16;
    u64 pointers[16];

    if (argc > 16)
        argc = 16;
    if (argc <= 0)
        return 0;

    for (int i = argc - 1; i >= 0; i--) {
        usize length = strlen(argv[i]) + 1;
        stack -= length;
        if (process_user_write(process, stack, argv[i], length) != 0)
            return 0;
        pointers[i] = stack;
    }

    stack &= ~0xFULL;
    stack -= (u64)(argc + 2) * sizeof(u64);

    u64 frame[18];
    frame[0] = (u64)argc;
    for (int i = 0; i < argc; i++)
        frame[1 + i] = pointers[i];
    frame[1 + argc] = 0;
    if (process_user_write(process, stack, frame,
                   (u64)(argc + 2) * sizeof(u64)) != 0)
        return 0;
    return stack;
}

static int prepare_image(struct process *process, const u8 *image,
                         u64 image_size, const char *path, int argc,
                         const char **argv)
{
    u64 entry = 0;
    if (image_load(process, image, image_size, path, &entry) != 0)
        return -1;

    for (u64 page = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
         page < USER_STACK_TOP; page += PAGE_SIZE) {
        if (!process_map_page(process, page,
                              VMM_WRITABLE | VMM_EXECUTE_DISABLE))
            return -1;
    }

    u64 stack = build_user_stack(process, argc, argv);
    if (!stack)
        return -1;

    process->entry = entry;
    process->user_stack = stack;
    return 0;
}

static void process_entry(void *argument)
{
    struct process *process = (struct process *)argument;
    user_enter(process->entry, process->user_stack);
    process_exit_current(-1);
}

static void process_fail(struct process *process)
{
    release_user_pages(process);
    if (process->space) {
        vmm_space_destroy(process->space);
        process->space = NULL;
    }
    process->used = 0;
    process->state = PROC_UNUSED;
}

static u64 spawn_image(const u8 *image, u64 image_size, const char *name,
                       int argc, char **argv)
{
    struct process *parent = current_process();
    struct process *process = process_alloc();
    if (!process)
        return 0;

    process->parent_pid = parent ? parent->pid : 0;
    /* Services that touch raw device memory (AHCI DMA, ext4 blocks) get the
     * 0..4 GiB user identity map so a physical address is a valid pointer,
     * and /bin/ext4fs is the only holder of the trusted block capability.
     * Both are granted to kernel-initiated spawns only: a Ring3 process that
     * spawns "/bin/ext4fs" itself (SYS_SPAWN_ARG with a forged control page)
     * must not end up able to drive the raw disk.  Kernel boot spawns
     * (dev_start, fs_service_start) run without a current process. */
    int trusted = (parent == NULL) && name &&
                  (strcmp(name, "/bin/drivers") == 0 ||
                   strcmp(name, "/bin/ext4fs") == 0);
    process->space = vmm_space_create(trusted);
    if (!process->space) {
        process_fail(process);
        return 0;
    }
    process->block_cap = (parent == NULL) && name &&
                         strcmp(name, "/bin/ext4fs") == 0;

    const char *default_argv[1] = { name ? name : "<memory>" };
    const char **stack_argv = (argc > 0) ? (const char **)argv : default_argv;
    if (argc <= 0)
        argc = 1;

    if (prepare_image(process, image, image_size, name, argc, stack_argv) != 0) {
        kprintf("[proc] %s is not a valid ZXE/ELF64 executable\n",
                name ? name : "<memory>");
        process_fail(process);
        return 0;
    }

    int affinity = (name && strcmp(name, "/bin/drivers") == 0) ? 0 : -1;
    struct thread *thread = kthread_create_ctx(
        name ? name : "user", process_entry, process, affinity,
        process->space->cr3, process->space, process);
    if (!thread) {
        process_fail(process);
        return 0;
    }

    kprintf("[proc] pid %llu: %s, entry 0x%llx, stack 0x%llx, CPL 3\n",
            process->pid, name ? name : "<memory>", process->entry,
            process->user_stack);
    return process->pid;
}

u64 process_spawn(const char *path, int argc, char **argv)
{
    if (!path)
        return 0;

    u8 *image = NULL;
    u64 image_size = 0;
    if (vfs_read_all(path, &image, &image_size) != 0) {
        kprintf("[proc] cannot read %s\n", path);
        return 0;
    }

    u64 pid = spawn_image(image, image_size, path, argc, argv);
    kfree(image);
    return pid;
}

int process_wait(u64 pid, int *status)
{
    struct process *process = process_find(pid);
    if (!process)
        return -1;

    while (!(process->state == PROC_ZOMBIE && process->thread_reaped))
        thread_sleep_ms(5);

    if (status)
        *status = process->exit_code;
    process->used = 0;
    process->state = PROC_UNUSED;
    process->pid = 0;
    return 0;
}

int process_run(const char *path, int argc, char **argv)
{
    u64 pid = process_spawn(path, argc, argv);
    if (!pid)
        return -1;

    int status = -1;
    if (process_wait(pid, &status) != 0)
        return -1;
    return status;
}

int process_run_image(const u8 *image, u64 size, const char *name,
                      int argc, char **argv)
{
    u64 pid = spawn_image(image, size, name, argc, argv);
    if (!pid)
        return -1;

    int status = -1;
    if (process_wait(pid, &status) != 0)
        return -1;
    return status;
}

void process_thread_reaped(struct thread *thread)
{
    struct process *process = thread ? thread->proc : NULL;

    if (thread && thread->space) {
        vmm_space_destroy(thread->space);
        thread->space = NULL;
    }
    if (process) {
        process->space = NULL;
        process->thread_reaped = 1;
        thread->proc = NULL;
    }
}

void process_exit_current(int status)
{
    struct process *process = current_process();

    if (!process) {
        kthread_exit();
    }

    process->state = PROC_ZOMBIE;
    process->exit_code = status;
    ipc_process_exit(process->pid);
    shm_process_exit(process->pid);
    release_user_pages(process);
    g_last_pid = process->pid;
    kthread_exit();
}

void process_init(void)
{
    memset(g_processes, 0, sizeof(g_processes));
    g_next_pid = 1;
    g_last_pid = 0;
}

u64 process_self_pid(void)
{
    struct process *process = current_process();
    return process ? process->pid : 0;
}

struct address_space *process_current_space(void)
{
    struct process *process = current_process();
    return process ? process->space : NULL;
}

int process_active(void)
{
    return current_process() != NULL;
}

/* Only /bin/ext4fs may issue the trusted block syscalls: they bypass the VFS
 * and touch whole devices. */
int process_block_capable(void)
{
    struct process *process = current_process();
    return process && process->block_cap;
}

const struct process_info *process_get_info(void)
{
    static struct process_info info;
    struct process *process = current_process();

    memset(&info, 0, sizeof(info));
    if (!process && g_last_pid)
        process = process_find(g_last_pid);
    if (!process)
        return &info;

    info.active = process->state == PROC_RUNNING;
    info.pid = process->pid;
    info.entry = process->entry;
    info.user_pages = (u64)process->mapping_count;
    return &info;
}

void process_handle_exception(struct interrupt_frame *frame)
{
    u64 cr2 = 0;
    if (frame->vector == 14)
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\n[proc] user exception %llu at rip 0x%llx, cr2 0x%llx, error 0x%llx, rsp 0x%llx\n",
            frame->vector, frame->rip, cr2, frame->error, frame->rsp);
    if (frame->vector == 14 && process_current()) {
        struct process *process = current_process();
        for (u32 i = 0; i < 6; i++) {
            u64 address = frame->rsp + (u64)i * sizeof(u64);
            u64 physical = vmm_space_get_physical(process->space, address);
            u64 value = physical ? *(u64 *)(uintptr_t)physical : 0;
            kprintf("[proc] stack[%u] @ 0x%llx = 0x%llx\n", i,
                    address, value);
        }
    }
    console_reset_color();
    process_exit_current(-(int)frame->vector);
}
