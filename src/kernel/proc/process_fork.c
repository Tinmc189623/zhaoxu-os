/*
 * Zhaoxu OS - POSIX fork() and exec().
 *
 * fork() copies the calling address space page by page, duplicates the zfd
 * descriptors, and starts a second kernel thread that returns to the exact
 * instruction after the parent's int 0x80 with rax = 0.  exec() builds a
 * brand-new address space, loads the ZXE/ELF image there, switches CR3 and
 * rewrites the caller's interrupt frame so the same syscall returns at the
 * new entry point.  Both operations keep the existing pid and kernel stack.
 */
#include "process_internal.h"

#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <string.h>
#include <thread.h>
#include <types.h>
#include <vfs.h>
#include <zfd.h>

/* Implemented in arch/x86_64/user.S. */
void user_enter_context(const struct user_context *context);

struct fork_request {
    struct process *process;
    struct user_context context;
};

/* Child thread body: enter ring 3 with the copied parent context. */
static void fork_child_entry(void *argument)
{
    struct fork_request *request = (struct fork_request *)argument;
    struct user_context context = request->context;

    kfree(request);
    user_enter_context(&context);
    process_exit_current(-1);
}

/* Undo a partially created child: free its pages, its page tables and its
 * process slot.  Descriptors are released by the normal exit path only when
 * the child actually ran, so a failed fork closes the copies here. */
static void fork_discard(struct process *child)
{
    process_release_pages(child);
    zfd_process_exit(child);
    if (child->space) {
        vmm_space_destroy(child->space);
        child->space = NULL;
    }
    child->used = 0;
    child->state = PROC_UNUSED;
    child->pid = 0;
}

long process_fork(struct interrupt_frame *frame)
{
    struct process *parent = process_current();
    if (!parent || !frame)
        return -1;

    struct process *child = process_alloc();
    if (!child)
        return -1;
    child->parent_pid = parent->pid;
    child->space = vmm_space_create(0);
    if (!child->space) {
        child->used = 0;
        child->state = PROC_UNUSED;
        return -1;
    }

    for (int i = 0; i < parent->mapping_count; i++) {
        const struct user_mapping *source = &parent->mappings[i];
        u64 physical =
            process_map_page(child, source->virtual_address, source->flags);
        if (!physical || !source->physical_address) {
            kprintf("[proc] fork: cannot copy page 0x%llx\n",
                    source->virtual_address);
            fork_discard(child);
            return -1;
        }
        memcpy((void *)(uintptr_t)physical,
               (const void *)(uintptr_t)source->physical_address, PAGE_SIZE);
    }
    if (zfd_fork(parent, child) != 0) {
        fork_discard(child);
        return -1;
    }

    struct fork_request *request = kzalloc(sizeof(*request));
    if (!request) {
        fork_discard(child);
        return -1;
    }
    request->process = child;
    request->context.rbx = frame->rbx;
    request->context.rbp = frame->rbp;
    request->context.r12 = frame->r12;
    request->context.r13 = frame->r13;
    request->context.r14 = frame->r14;
    request->context.r15 = frame->r15;
    request->context.rip = frame->rip;
    request->context.rflags = frame->rflags;
    request->context.rsp = frame->rsp;

    child->entry = parent->entry;
    child->user_stack = parent->user_stack;
    struct thread *thread = kthread_create_ctx(
        "fork", fork_child_entry, request, -1, child->space->cr3,
        child->space, child);
    if (!thread) {
        kfree(request);
        fork_discard(child);
        return -1;
    }

    kprintf("[proc] pid %llu: fork of %llu, rip 0x%llx rsp 0x%llx\n",
            child->pid, parent->pid, frame->rip, frame->rsp);
    return (long)child->pid;
}

long process_exec(struct interrupt_frame *frame, const char *path, int argc,
                  char **argv)
{
    struct process *process = process_current();
    struct thread *thread = sched_current();

    if (!process || !thread || !frame || !path)
        return -1;

    u8 *image = NULL;
    u64 image_size = 0;
    if (vfs_read_all(path, &image, &image_size) != 0) {
        kprintf("[proc] exec: cannot read %s\n", path);
        return -1;
    }

    struct address_space *old_space = process->space;
    struct user_mapping *old_mappings = process->mappings;
    int old_count = process->mapping_count;
    int old_capacity = process->mapping_capacity;

    struct address_space *new_space = vmm_space_create(0);
    if (!new_space) {
        kfree(image);
        return -1;
    }

    /* Load into the fresh space first.  A bad image must leave the caller
     * running its old program, not a half-replaced one. */
    process->space = new_space;
    process->mappings = NULL;
    process->mapping_count = 0;
    process->mapping_capacity = 0;
    if (process_prepare_image(process, image, image_size, path, argc,
                              (const char **)argv) != 0) {
        process_release_pages(process);
        vmm_space_destroy(new_space);
        process->space = old_space;
        process->mappings = old_mappings;
        process->mapping_count = old_count;
        process->mapping_capacity = old_capacity;
        kfree(image);
        kprintf("[proc] exec: %s is not a valid ZXE/ELF64 executable\n",
                path);
        return -1;
    }
    kfree(image);

    vmm_space_activate(new_space);
    thread->space = new_space;
    thread->cr3 = new_space->cr3;

    /* The old image is unreachable now; release its pages and page tables. */
    for (int i = 0; i < old_count; i++) {
        u64 physical =
            vmm_space_unmap_page(old_space, old_mappings[i].virtual_address);
        if (physical)
            pmm_free_page(physical);
    }
    vmm_space_destroy(old_space);
    kfree(old_mappings);

    frame->rip = process->entry;
    frame->rsp = process->user_stack;
    frame->rax = 0;
    kprintf("[proc] pid %llu: exec %s, entry 0x%llx, stack 0x%llx\n",
            process->pid, path, process->entry, process->user_stack);
    return 0;
}
