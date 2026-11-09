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

#define USER_LOAD_BASE      0x0000010000000000ULL   /* 1 TiB   */
#define USER_STACK_TOP      0x0000010100000000ULL   /* 1 TiB + 4 GiB */
#define USER_STACK_PAGES    32
#define USER_MAX_PAGES      1024
#define USER_KERNEL_STACK   KERNEL_THREAD_STACK
/* Keep every user mapping inside PML4 slot 2 (1 TiB..1.5 TiB) so a private
 * address space only ever has to copy/clear/free that one slot. */
#define USER_ADDRESS_LIMIT  0x0000018000000000ULL

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2
#define SYS_OPEN   3
#define SYS_READ   4
#define SYS_CLOSE  5
#define SYS_UPTIME 6
#define SYS_SPAWN       7
#define SYS_WAIT        8
#define SYS_PORT_LISTEN 9
#define SYS_PORT_CONNECT 10
#define SYS_PORT_ACCEPT 11
#define SYS_PORT_SEND   12
#define SYS_PORT_RECV   13
#define SYS_PORT_CLOSE  14
#define SYS_SHM_CREATE  15
#define SYS_SHM_MAP     16
#define SYS_SHM_UNMAP   17
#define SYS_SHM_CLOSE   18
#define SYS_FB_MAP      19
#define SYS_MOUSE       20
#define SYS_KEY         21
#define SYS_SLEEP       22
#define SYS_CPUS        23
#define SYS_RTC         24
#define SYS_READ_PATH   25
#define SYS_PORT_TRY_ACCEPT 26
#define SYS_PORT_TRY_RECV   27

#define FB_USER_BASE    0x0000010040000000ULL
#define FB_MAX_PAGES    8192

struct user_fb_info {
    u64 address;
    u64 width;
    u64 height;
    u64 stride;
    u64 format;
};

enum process_state {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_ZOMBIE
};

struct user_mapping {
    u64 virtual_address;
    u64 physical_address;
};

struct process {
    int used;
    u64 pid;
    u64 parent_pid;
    int state;
    int exit_code;
    int thread_reaped;
    struct address_space *space;
    int mapping_count;
    struct user_mapping mappings[USER_MAX_PAGES];
    u64 entry;
    u64 user_stack;
};

static struct process g_processes[MAX_PROCESSES];
static u64 g_next_pid = 1;
static u64 g_last_pid;

extern void user_enter(u64 entry, u64 stack);
static void process_exit_current(int status);

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

struct zxe_header {
    u8 magic[4];
    u16 version;
    u16 machine;
    u16 seg_count;
    u16 hdr_size;
    u32 reserved;
    u64 entry;
    u64 file_size;
} __attribute__((packed));

struct zxe_segment {
    u64 vaddr;
    u64 file_off;
    u64 file_size;
    u64 mem_size;
    u64 flags;
} __attribute__((packed));

#define ZXE_SEG_MAX 16

static struct process *current_process(void)
{
    struct thread *thread = sched_current();
    return thread ? thread->proc : NULL;
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
static int user_write(struct process *process, u64 virtual_address,
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

static u64 map_user_page(struct process *process, u64 virtual_address)
{
    for (int i = 0; i < process->mapping_count; i++) {
        if (process->mappings[i].virtual_address == virtual_address)
            return process->mappings[i].physical_address;
    }
    if (process->mapping_count >= USER_MAX_PAGES)
        return 0;

    u64 physical = pmm_alloc_page();
    if (!physical)
        return 0;
    if (vmm_space_map_page(process->space, virtual_address, physical,
                           VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
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
    if (!process || !process->space)
        return;

    for (int i = 0; i < process->mapping_count; i++) {
        u64 physical =
            vmm_space_unmap_page(process->space,
                                 process->mappings[i].virtual_address);
        if (physical)
            pmm_free_page(physical);
    }
    process->mapping_count = 0;
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

        for (u64 page = start; page < end; page += PAGE_SIZE) {
            if (!map_user_page(process, page))
                return -1;
        }

        if (program->file_size &&
            user_write(process, program->virtual_address,
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
                    u64 *entry_out)
{
    if (!image || image_size < sizeof(struct zxe_header))
        return -1;

    const struct zxe_header *header = (const struct zxe_header *)image;
    if (header->magic[0] != 'Z' || header->magic[1] != 'X' ||
        header->magic[2] != 'E' || header->magic[3] != 1)
        return -1;
    if (header->version != 1 || header->machine != 62)
        return -1;
    if (header->seg_count == 0 || header->seg_count > ZXE_SEG_MAX)
        return -1;

    u64 expected_hdr = 32 + (u64)header->seg_count *
                             sizeof(struct zxe_segment);
    expected_hdr = (expected_hdr + 15) & ~15ULL;
    if (header->hdr_size != expected_hdr || header->hdr_size > image_size)
        return -1;
    if (header->file_size != image_size)
        return -1;

    for (u16 i = 0; i < header->seg_count; i++) {
        const struct zxe_segment *segment = (const struct zxe_segment *)
            (image + 32 + (u64)i * sizeof(struct zxe_segment));
        if (segment->vaddr < USER_LOAD_BASE)
            return -1;
        if (segment->vaddr & 0xFFF)
            return -1;
        if (segment->mem_size == 0 || segment->file_size > segment->mem_size)
            return -1;
        if (segment->vaddr + segment->mem_size < segment->vaddr)
            return -1;
        if (segment->vaddr + segment->mem_size > USER_ADDRESS_LIMIT)
            return -1;
        if (segment->file_off < header->hdr_size ||
            segment->file_off + segment->file_size > image_size ||
            segment->file_off + segment->file_size < segment->file_off)
            return -1;

        u64 end = (segment->vaddr + segment->mem_size + 0xFFFULL) & ~0xFFFULL;
        for (u64 page = segment->vaddr; page < end; page += PAGE_SIZE) {
            if (!map_user_page(process, page))
                return -1;
        }

        if (segment->file_size &&
            user_write(process, segment->vaddr, image + segment->file_off,
                       segment->file_size) != 0)
            return -1;
        if (segment->mem_size > segment->file_size &&
            user_zero(process, segment->vaddr + segment->file_size,
                      segment->mem_size - segment->file_size) != 0)
            return -1;
    }

    if (header->entry < USER_LOAD_BASE ||
        header->entry >= USER_ADDRESS_LIMIT)
        return -1;

    *entry_out = header->entry;
    return 0;
}

static int image_load(struct process *process, const u8 *image, u64 image_size,
                      u64 *entry_out)
{
    if (image_size >= 4 && image[0] == 'Z' && image[1] == 'X' &&
        image[2] == 'E' && image[3] == 1)
        return zxe_load(process, image, image_size, entry_out);
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
        if (user_write(process, stack, argv[i], length) != 0)
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
    if (user_write(process, stack, frame,
                   (u64)(argc + 2) * sizeof(u64)) != 0)
        return 0;
    return stack;
}

static int prepare_image(struct process *process, const u8 *image,
                         u64 image_size, int argc, const char **argv)
{
    u64 entry = 0;
    if (image_load(process, image, image_size, &entry) != 0)
        return -1;

    for (u64 page = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
         page < USER_STACK_TOP; page += PAGE_SIZE) {
        if (!map_user_page(process, page))
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
    process->space = vmm_space_create();
    if (!process->space) {
        process_fail(process);
        return 0;
    }

    const char *default_argv[1] = { name ? name : "<memory>" };
    const char **stack_argv = (argc > 0) ? (const char **)argv : default_argv;
    if (argc <= 0)
        argc = 1;

    if (prepare_image(process, image, image_size, argc, stack_argv) != 0) {
        kprintf("[proc] %s is not a valid ZXE/ELF64 executable\n",
                name ? name : "<memory>");
        process_fail(process);
        return 0;
    }

    struct thread *thread = kthread_create_ctx(
        name ? name : "user", process_entry, process, -1,
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

static void process_exit_current(int status)
{
    struct process *process = current_process();

    if (!process) {
        kthread_exit();
        return;
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

static int user_range_ok(struct process *process, u64 pointer, u64 length)
{
    if (length == 0)
        return 1;
    if (!process || pointer == 0 || pointer >= USER_ADDRESS_LIMIT)
        return 0;
    u64 last = pointer + length - 1;
    if (last < pointer || last >= USER_ADDRESS_LIMIT)
        return 0;

    u64 page = pointer & ~0xFFFULL;
    u64 last_page = last & ~0xFFFULL;
    for (; page <= last_page; page += PAGE_SIZE) {
        if (!vmm_space_page_is_user(process->space, page))
            return 0;
    }
    return 1;
}

static int copy_user_string(struct process *process, char *destination,
                            usize capacity, u64 source)
{
    if (!capacity)
        return -1;
    for (usize i = 0; i < capacity; i++) {
        if (user_range_ok(process, source + i, 1) != 1)
            return -1;
        char character = *(const char *)(uintptr_t)(source + i);
        destination[i] = character;
        if (character == 0)
            return 0;
    }
    destination[capacity - 1] = 0;
    return -1;
}

static long syscall_write(struct process *process, u64 descriptor, u64 buffer,
                          u64 length)
{
    if (descriptor != 1 && descriptor != 2)
        return -1;
    if (length > 65536)
        length = 65536;
    if (user_range_ok(process, buffer, length) != 1)
        return -1;
    console_write_n((const char *)(uintptr_t)buffer, length);
    return (long)length;
}

static long syscall_open(struct process *process, u64 path_pointer)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    return vfs_open(path);
}

static long syscall_read(struct process *process, u64 descriptor, u64 buffer,
                         u64 length)
{
    if (length > 0x100000)
        length = 0x100000;
    if (user_range_ok(process, buffer, length) != 1)
        return -1;
    return vfs_read((int)descriptor, (void *)(uintptr_t)buffer, length);
}

static long syscall_spawn(struct process *process, u64 path_pointer)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    return (long)process_spawn(path, 0, NULL);
}

static long syscall_wait(struct process *process, u64 pid, u64 status_pointer)
{
    int status = 0;
    if (process_wait(pid, &status) != 0)
        return -1;
    if (status_pointer) {
        if (user_range_ok(process, status_pointer, sizeof(int)) != 1)
            return -1;
        if (user_write(process, status_pointer, &status, sizeof(int)) != 0)
            return -1;
    }
    return status;
}

static long syscall_port_try_accept(struct process *process, u64 port,
                                    u64 channel_pointer)
{
    if (user_range_ok(process, channel_pointer, sizeof(u64)) != 1)
        return -1;
    u64 channel = 0;
    int status = ipc_try_accept(port, &channel);
    if (status > 0 && user_write(process, channel_pointer, &channel,
                                 sizeof(channel)) != 0)
        return -1;
    return status;
}

static long syscall_port_try_recv(struct process *process, u64 channel,
                                  u64 type_pointer, u64 data_pointer,
                                  u64 capacity)
{
    if (capacity > IPC_MSG_MAX)
        capacity = IPC_MSG_MAX;
    if (capacity && user_range_ok(process, data_pointer, capacity) != 1)
        return -1;

    u64 type = 0;
    u8 data[IPC_MSG_MAX];
    int length = ipc_try_recv(channel, &type, data, (u32)capacity);
    if (length <= 0)
        return length;

    if (type_pointer) {
        if (user_range_ok(process, type_pointer, sizeof(u64)) != 1)
            return -1;
        if (user_write(process, type_pointer, &type, sizeof(u64)) != 0)
            return -1;
    }
    if (length && user_write(process, data_pointer, data, (u64)length) != 0)
        return -1;
    return length;
}

static long syscall_fb_map(struct process *process, u64 info_pointer)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot || !boot->framebuffer_base || !process->space)
        return -1;
    if (user_range_ok(process, info_pointer,
                      sizeof(struct user_fb_info)) != 1)
        return -1;

    u64 bytes = (u64)boot->pitch * boot->height;
    u64 pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0 || pages > FB_MAX_PAGES)
        return -1;

    for (u64 i = 0; i < pages; i++) {
        if (vmm_space_map_page(process->space,
                               FB_USER_BASE + i * PAGE_SIZE,
                               boot->framebuffer_base + i * PAGE_SIZE,
                               VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
            for (u64 undo = 0; undo < i; undo++)
                vmm_space_unmap_page(process->space,
                                     FB_USER_BASE + undo * PAGE_SIZE);
            return -1;
        }
    }

    struct user_fb_info info;
    info.address = FB_USER_BASE;
    info.width = boot->width;
    info.height = boot->height;
    info.stride = boot->pitch / 4;
    info.format = boot->pixel_format;
    if (user_write(process, info_pointer, &info, sizeof(info)) != 0)
        return -1;
    return (long)FB_USER_BASE;
}

static long syscall_mouse(struct process *process, u64 state_pointer)
{
    if (user_range_ok(process, state_pointer, 3 * sizeof(u64)) != 1)
        return -1;
    u64 state[3];
    state[0] = (u64)mouse_x();
    state[1] = (u64)mouse_y();
    state[2] = (u64)mouse_buttons();
    if (user_write(process, state_pointer, state, sizeof(state)) != 0)
        return -1;
    return mouse_ready() ? 1 : 0;
}

static long syscall_read_path(struct process *process, u64 path_pointer,
                              u64 buffer_pointer, u64 capacity)
{
    char path[256];
    if (copy_user_string(process, path, sizeof(path), path_pointer) != 0)
        return -1;
    if (capacity > 16u * 1024 * 1024)
        capacity = 16u * 1024 * 1024;
    if (capacity && user_range_ok(process, buffer_pointer, capacity) != 1)
        return -1;

    u64 read = 0;
    if (vfs_read_into(path, (u8 *)(uintptr_t)buffer_pointer, capacity,
                      &read) != 0)
        return -1;
    return (long)read;
}

static long syscall_rtc(struct process *process, u64 time_pointer)
{
    struct rtc_time now;
    if (user_range_ok(process, time_pointer, sizeof(now)) != 1)
        return -1;
    if (rtc_read(&now) != 0)
        return -1;
    if (user_write(process, time_pointer, &now, sizeof(now)) != 0)
        return -1;
    return 0;
}

static long syscall_port_listen(struct process *process, u64 name_pointer)
{
    char name[IPC_NAME_MAX];
    if (copy_user_string(process, name, sizeof(name), name_pointer) != 0)
        return -1;
    return (long)ipc_listen(name);
}

static long syscall_port_connect(struct process *process, u64 name_pointer)
{
    char name[IPC_NAME_MAX];
    if (copy_user_string(process, name, sizeof(name), name_pointer) != 0)
        return -1;
    return (long)ipc_connect(name);
}

static long syscall_port_send(struct process *process, u64 channel, u64 type,
                              u64 data_pointer, u64 length)
{
    if (length > IPC_MSG_MAX)
        return -1;
    if (length && user_range_ok(process, data_pointer, length) != 1)
        return -1;

    u8 data[IPC_MSG_MAX];
    if (length)
        memcpy(data, (const void *)(uintptr_t)data_pointer, length);
    return ipc_send(channel, type, data, (u32)length);
}

static long syscall_port_recv(struct process *process, u64 channel,
                              u64 type_pointer, u64 data_pointer, u64 capacity)
{
    if (capacity > IPC_MSG_MAX)
        capacity = IPC_MSG_MAX;
    if (capacity && user_range_ok(process, data_pointer, capacity) != 1)
        return -1;

    u64 type = 0;
    u8 data[IPC_MSG_MAX];
    int length = ipc_recv(channel, &type, data, (u32)capacity);
    if (length < 0)
        return -1;

    if (type_pointer) {
        if (user_range_ok(process, type_pointer, sizeof(u64)) != 1)
            return -1;
        if (user_write(process, type_pointer, &type, sizeof(u64)) != 0)
            return -1;
    }
    if (length) {
        if (user_write(process, data_pointer, data, (u64)length) != 0)
            return -1;
    }
    return length;
}

void syscall_dispatch(struct interrupt_frame *frame)
{
    struct process *process = current_process();
    if (!process) {
        frame->rax = (u64)-1;
        return;
    }

    long result = -1;
    switch (frame->rax) {
    case SYS_EXIT:
        process_exit_current((int)frame->rdi);
        return;
    case SYS_WRITE:
        result = syscall_write(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_GETPID:
        result = (long)process->pid;
        break;
    case SYS_OPEN:
        result = syscall_open(process, frame->rdi);
        break;
    case SYS_READ:
        result = syscall_read(process, frame->rdi, frame->rsi, frame->rdx);
        break;
    case SYS_CLOSE:
        result = vfs_close((int)frame->rdi);
        break;
    case SYS_UPTIME:
        result = (long)(timer_ticks() * 1000 / timer_hz());
        break;
    case SYS_SPAWN:
        result = syscall_spawn(process, frame->rdi);
        break;
    case SYS_WAIT:
        result = syscall_wait(process, frame->rdi, frame->rsi);
        break;
    case SYS_PORT_LISTEN:
        result = syscall_port_listen(process, frame->rdi);
        break;
    case SYS_PORT_CONNECT:
        result = syscall_port_connect(process, frame->rdi);
        break;
    case SYS_PORT_ACCEPT:
        result = (long)ipc_accept(frame->rdi);
        break;
    case SYS_PORT_SEND:
        result = syscall_port_send(process, frame->rdi, frame->rsi,
                                   frame->rdx, frame->r10);
        break;
    case SYS_PORT_RECV:
        result = syscall_port_recv(process, frame->rdi, frame->rsi,
                                   frame->rdx, frame->r10);
        break;
    case SYS_PORT_CLOSE:
        result = ipc_close(frame->rdi);
        break;
    case SYS_SHM_CREATE:
        result = (long)shm_create(frame->rdi);
        break;
    case SYS_SHM_MAP:
        result = (long)shm_map(frame->rdi);
        break;
    case SYS_SHM_UNMAP:
        result = shm_unmap(frame->rdi);
        break;
    case SYS_SHM_CLOSE:
        result = shm_close(frame->rdi);
        break;
    case SYS_FB_MAP:
        result = syscall_fb_map(process, frame->rdi);
        break;
    case SYS_MOUSE:
        result = syscall_mouse(process, frame->rdi);
        break;
    case SYS_KEY:
        result = keyboard_getchar();
        break;
    case SYS_SLEEP:
        thread_sleep_ms((u32)frame->rdi);
        result = 0;
        break;
    case SYS_CPUS:
        result = (long)smp_cpu_count();
        break;
    case SYS_RTC:
        result = syscall_rtc(process, frame->rdi);
        break;
    case SYS_READ_PATH:
        result = syscall_read_path(process, frame->rdi, frame->rsi,
                                   frame->rdx);
        break;
    case SYS_PORT_TRY_ACCEPT:
        result = syscall_port_try_accept(process, frame->rdi, frame->rsi);
        break;
    case SYS_PORT_TRY_RECV:
        result = syscall_port_try_recv(process, frame->rdi, frame->rsi,
                                       frame->rdx, frame->r10);
        break;
    default:
        result = -1;
        break;
    }
    frame->rax = (u64)result;
}

void process_handle_exception(struct interrupt_frame *frame)
{
    u64 cr2 = 0;
    if (frame->vector == 14)
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\n[proc] user exception %llu at rip 0x%llx, cr2 0x%llx\n",
            frame->vector, frame->rip, cr2);
    console_reset_color();

    process_exit_current(-(int)frame->vector);
}
