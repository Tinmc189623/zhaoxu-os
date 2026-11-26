/*
 * Zhaoxu OS - POSIX task identity and limit syscalls (89, 90, 91, 100-102).
 *
 * The kernel has no POSIX threads yet, so a task is a process and its tid is
 * the kernel thread id.  Limits are static values: this system runs as root
 * with no resource accounting, and reporting them lets libc initialize.
 */
#include "process_internal.h"

#include <thread.h>
#include <types.h>

#define EINVAL_CODE 22
#define EPERM_CODE  1

#define RLIMIT_CORE   4
#define RLIMIT_NOFILE 7
#define RLIMIT_NPROC  6
#define RLIMIT_STACK  3
#define RLIMIT_COUNT  16

#define ROBUST_LIST_SIZE 24

struct zos_rlimit {
    u64 current;
    u64 maximum;
};

#define RLIMIT_UNLIMITED 0x7fffffffffffffffULL

/* Default limit values reported to userspace. */
static void limit_value(int resource, struct zos_rlimit *limit)
{
    limit->current = RLIMIT_UNLIMITED;
    limit->maximum = RLIMIT_UNLIMITED;
    switch (resource) {
    case RLIMIT_STACK:
        limit->current = 8ULL * 1024 * 1024;
        limit->maximum = 8ULL * 1024 * 1024;
        break;
    case RLIMIT_NOFILE:
    case RLIMIT_NPROC:
        limit->current = 1024;
        limit->maximum = 1024;
        break;
    case RLIMIT_CORE:
        limit->current = 0;
        limit->maximum = 0;
        break;
    default:
        break;
    }
}

long sys_gettid(void)
{
    struct thread *thread = sched_current();
    return thread ? (long)thread->tid : -1;
}

/* set_tid_address: remember the word to clear at thread exit, return the tid. */
long sys_set_tid_address(struct process *process, u64 pointer)
{
    struct thread *thread = sched_current();
    if (!thread)
        return -1;
    if (pointer && user_range_ok(process, pointer, sizeof(u32)) != 1)
        return -EINVAL_CODE;
    thread->clear_child_tid = pointer;
    return (long)thread->tid;
}

/* exit_group ends the process; a single kernel thread carries it here. */
void sys_exit_group(struct process *process, int status)
    __attribute__((noreturn));
void sys_exit_group(struct process *process, int status)
{
    (void)process;
    process_exit_current(status);
}

long sys_set_robust_list(struct process *process, u64 head, u64 length)
{
    (void)process;
    (void)head;
    return length == ROBUST_LIST_SIZE ? 0 : -EINVAL_CODE;
}

/* prlimit64 / getrlimit: report the static limits, refuse changes. */
long sys_prlimit64(struct process *process, u64 pid, u64 resource,
                   u64 new_limit, u64 old_limit)
{
    (void)pid;
    if (resource >= RLIMIT_COUNT)
        return -EINVAL_CODE;
    if (new_limit)
        return -EPERM_CODE;
    if (old_limit) {
        if (user_range_ok(process, old_limit, sizeof(struct zos_rlimit)) != 1)
            return -EINVAL_CODE;
        struct zos_rlimit limit;
        limit_value((int)resource, &limit);
        if (process_user_write(process, old_limit, &limit, sizeof(limit)) != 0)
            return -EINVAL_CODE;
    }
    return 0;
}

long sys_getrlimit(struct process *process, u64 resource, u64 limit_pointer)
{
    return sys_prlimit64(process, 0, resource, 0, limit_pointer);
}
