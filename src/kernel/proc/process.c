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

#include "process_internal.h"
#include "elf_loader.h"

static struct process g_processes[MAX_PROCESSES];
static u64 g_next_pid = 1;
static u64 g_last_pid;

extern void user_enter(u64 entry, u64 stack);
void process_exit_current(int status);

static struct process *current_process(void)
{
    struct thread *thread = sched_current();
    return thread ? thread->proc : NULL;
}

struct process *process_current(void)
{
    return current_process();
}

struct process *process_lookup(u64 pid)
{
    if (!pid)
        return NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_processes[i].used && g_processes[i].pid == pid)
            return &g_processes[i];
    }
    return NULL;
}

static struct process *process_find(u64 pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_processes[i].used && g_processes[i].pid == pid)
            return &g_processes[i];
    }
    return NULL;
}

struct process *process_alloc(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!g_processes[i].used) {
            memset(&g_processes[i], 0, sizeof(g_processes[i]));
            g_processes[i].used = 1;
            g_processes[i].pid = g_next_pid++;
            g_processes[i].state = PROC_RUNNING;
            for (int fd = 0; fd < 3; fd++)
                g_processes[i].stdio[fd] = -1;
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
    process->mappings[index].flags = page_flags;
    return physical;
}

/* Drop one recorded page mapping and free its physical page. */
void process_unmap_page(struct process *process, u64 virtual_address)
{
    for (int i = 0; i < process->mapping_count; i++) {
        if (process->mappings[i].virtual_address != virtual_address)
            continue;
        u64 physical = vmm_space_unmap_page(process->space, virtual_address);
        if (physical)
            pmm_free_page(physical);
        process->mappings[i] = process->mappings[--process->mapping_count];
        return;
    }
}

/* Copy a NUL-terminated path from user memory; an unterminated string that
 * fills the buffer is rejected rather than silently truncated. */
int copy_path_from_user(struct process *process, char *destination,
                        usize capacity, u64 source)
{
    if (!process || !destination || !capacity)
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

/* Move the program break; pages are mapped on demand and freed on shrink.
 * A request outside [brk_start, brk_limit] leaves the break unchanged, which
 * is the Linux contract glibc's malloc expects. */
u64 process_brk(struct process *process, u64 requested)
{
    if (!process || !process->space)
        return 0;
    if (requested == 0)
        return process->brk_current;
    if (requested < process->brk_start || requested > process->brk_limit)
        return process->brk_current;

    u64 old_end = ALIGN_UP(process->brk_current, PAGE_SIZE);
    u64 new_end = ALIGN_UP(requested, PAGE_SIZE);
    if (new_end > old_end) {
        u64 mapped = old_end;
        for (; mapped < new_end; mapped += PAGE_SIZE)
            if (!process_map_page(process, mapped,
                                  VMM_WRITABLE | VMM_EXECUTE_DISABLE))
                break;
        if (mapped < new_end) {
            for (u64 undo = old_end; undo < mapped; undo += PAGE_SIZE)
                process_unmap_page(process, undo);
            return process->brk_current;
        }
    } else if (new_end < old_end) {
        for (u64 va = old_end; va > new_end;) {
            va -= PAGE_SIZE;
            process_unmap_page(process, va);
        }
    }
    process->brk_current = requested;
    return requested;
}

void process_release_pages(struct process *process)
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
    process->anon_count = 0;
    process->anon_cursor = 0;
    kfree(process->mappings);
    process->mappings = NULL;
}

int process_prepare_image(struct process *process, const u8 *image,
                          u64 image_size, const char *path, int argc,
                          const char **argv)
{
    u64 entry = 0;
    u64 stack = 0;
    if (image_load(process, image, image_size, path, argc, argv, &entry,
                   &stack) != 0)
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
    process_release_pages(process);
    if (process->space) {
        vmm_space_destroy(process->space);
        process->space = NULL;
    }
    process->used = 0;
    process->state = PROC_UNUSED;
}

/* Rule 49: a userland driver is a .udrv bundle.  The spawn resolver passes
 * "<bundle>/executable"; match either the bundle path or its executable. */
static int path_is_bundle(const char *name, const char *bundle)
{
    if (!name)
        return 0;
    if (strcmp(name, bundle) == 0)
        return 1;
    usize length = strlen(bundle);
    return strncmp(name, bundle, length) == 0 &&
           strcmp(name + length, "/executable") == 0;
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
                  (path_is_bundle(name, "/bin/drivers.udrv") ||
                   path_is_bundle(name, "/bin/ext4fs.udrv") ||
                   path_is_bundle(name, "/bin/display.udrv"));
    process->space = vmm_space_create(trusted);
    if (!process->space) {
        process_fail(process);
        return 0;
    }
    process->block_cap = (parent == NULL) && name &&
                         path_is_bundle(name, "/bin/ext4fs.udrv");
    process->display_cap = (parent == NULL) && name &&
                           path_is_bundle(name, "/bin/display.udrv");

    const char *default_argv[1] = { name ? name : "<memory>" };
    const char **stack_argv = (argc > 0) ? (const char **)argv : default_argv;
    if (argc <= 0)
        argc = 1;

    if (process_prepare_image(process, image, image_size, name, argc,
                              stack_argv) != 0) {
        kprintf("[proc] %s is not a valid ZXE/ELF64 executable\n",
                name ? name : "<memory>");
        process_fail(process);
        return 0;
    }

    int affinity = (name && (path_is_bundle(name, "/bin/drivers.udrv") ||
                             path_is_bundle(name, "/bin/display.udrv")))
                       ? 0
                       : -1;
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

/* Rule 49: a userland driver is a .udrv bundle whose executable is the ZXE
 * named by its manifest.  Spawning the bundle path resolves to that file. */
u64 process_spawn(const char *path, int argc, char **argv)
{
    if (!path)
        return 0;

    u8 *image = NULL;
    u64 image_size = 0;
    const char *resolved = path;
    char bundled[160];
    if (vfs_read_all(path, &image, &image_size) != 0) {
        int written = ksnprintf(bundled, sizeof(bundled), "%s/executable",
                                path);
        if (written <= 0 || (usize)written >= sizeof(bundled) ||
            vfs_read_all(bundled, &image, &image_size) != 0) {
            kprintf("[proc] cannot read %s\n", path);
            return 0;
        }
        resolved = bundled;
    }

    u64 pid = spawn_image(image, image_size, resolved, argc, argv);
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
        yuanshuo_kthread_exit();
    }

    process->state = PROC_ZOMBIE;
    process->exit_code = status;
    /* set_tid_address observers poll this word to learn the thread died. */
    struct thread *thread = sched_current();
    if (thread && thread->clear_child_tid) {
        u32 zero = 0;
        process_user_write(process, thread->clear_child_tid, &zero, sizeof(zero));
        thread->clear_child_tid = 0;
    }
    ipc_process_exit(process->pid);
    shm_process_exit(process->pid);
    zfd_process_exit(process);
    process_release_pages(process);
    /* Tell a waiting shell that this child changed state. */
    if (process->parent_pid) {
        struct process *parent = process_lookup(process->parent_pid);
        if (parent) {
            parent->sig_pending |= 1ULL << (SIGCHLD - 1);
            signal_wake(parent);
        }
    }
    g_last_pid = process->pid;
    yuanshuo_kthread_exit();
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
    /* A registered handler for SIGSEGV/SIGILL/SIGFPE takes over here; only an
     * unhandled fault reaches the reporter below. */
    if (signal_deliver_fault(current_process(), frame))
        return;

    u64 cr2 = 0;
    if (frame->vector == 14)
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\n[proc] user exception %llu at rip 0x%llx, cr2 0x%llx, error 0x%llx, rsp 0x%llx\n",
            frame->vector, frame->rip, cr2, frame->error, frame->rsp);
    /* A #GP from a non-canonical data address is invisible without the
     * register file: the faulting operand is usually a corrupted pointer. */
    kprintf("[proc] regs rax 0x%llx rbx 0x%llx rcx 0x%llx rdx 0x%llx\n",
            frame->rax, frame->rbx, frame->rcx, frame->rdx);
    kprintf("[proc] regs rsi 0x%llx rdi 0x%llx rbp 0x%llx r8 0x%llx\n",
            frame->rsi, frame->rdi, frame->rbp, frame->r8);
    kprintf("[proc] regs r9 0x%llx r10 0x%llx r11 0x%llx r12 0x%llx "
            "r13 0x%llx r14 0x%llx r15 0x%llx\n",
            frame->r9, frame->r10, frame->r11, frame->r12, frame->r13,
            frame->r14, frame->r15);
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
