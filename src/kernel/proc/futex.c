/*
 * Zhaoxu OS - futex syscall (92).
 *
 * The wait list lives in the scheduler (thread->futex_address), so the value
 * comparison and the block happen in one critical section.  This file only
 * decodes the Linux futex ABI: WAIT/WAKE and the BITSET variants that glibc
 * and pthread use.  No requeue or priority inheritance.
 */
#include "process_internal.h"

#include <mem.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256

#define FUTEX_BITSET_ALL 0xffffffffu

struct futex_timespec {
    i64 seconds;
    i64 nanoseconds;
};

/* Convert a nanosecond span into a timer tick deadline, rounding up. */
static u64 deadline_from_ns(u64 nanoseconds)
{
    u64 hz = timer_hz();
    u64 delta = (nanoseconds * hz + 999999999ULL) / 1000000000ULL;
    if (delta == 0)
        delta = 1;
    return timer_ticks() + delta;
}

/* Read the user-provided relative (WAIT) or absolute (WAIT_BITSET) timeout. */
static int read_timeout(struct process *process, u64 pointer, int absolute,
                        u64 *deadline)
{
    if (!pointer) {
        *deadline = ~0ULL;   /* no timeout */
        return 0;
    }
    if (user_range_ok(process, pointer, sizeof(struct futex_timespec)) != 1)
        return -14;
    struct futex_timespec span;
    const struct futex_timespec *source =
        (const struct futex_timespec *)(uintptr_t)pointer;
    span = *source;
    if (span.seconds < 0 || span.nanoseconds < 0 ||
        span.nanoseconds >= 1000000000LL)
        return -22;

    u64 ns = (u64)span.seconds * 1000000000ULL + (u64)span.nanoseconds;
    if (absolute) {
        u64 now = timer_now_ns();
        if (ns <= now)
            return -110;   /* deadline already passed */
        ns -= now;
    }
    *deadline = deadline_from_ns(ns);
    return 0;
}

/* Key a futex word by its physical address so a shared page mapped at two
 * different virtual addresses is the same futex. */
static u64 futex_key(struct process *process, u64 address)
{
    u64 page = address & ~(PAGE_SIZE - 1);
    u64 physical = vmm_space_get_physical(process->space, page);
    if (!physical)
        return 0;
    return physical + (address & (PAGE_SIZE - 1));
}

long sys_futex(struct process *process, u64 address, u64 operation, u64 value,
               u64 timeout_pointer, u64 bitset)
{
    u64 command = operation & ~(u64)(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME);
    if (!process || !address || (address & 3))
        return -22;
    if (user_range_ok(process, address, sizeof(u32)) != 1)
        return -14;
    u64 key = futex_key(process, address);
    if (!key)
        return -14;

    if (command == FUTEX_WAIT || command == FUTEX_WAIT_BITSET) {
        u32 mask = command == FUTEX_WAIT ? FUTEX_BITSET_ALL : (u32)bitset;
        if (mask == 0)
            return -22;
        u64 deadline;
        int status = read_timeout(process, timeout_pointer,
                                  command == FUTEX_WAIT_BITSET, &deadline);
        if (status != 0)
            return status;
        int result = sched_futex_wait(key, address, (u32)value, mask,
                                      deadline);
        if (result < 0)
            return -11;        /* EAGAIN: the word changed before the block */
        return result == 1 ? 0 : -110;
    }

    if (command == FUTEX_WAKE || command == FUTEX_WAKE_BITSET)
        return sched_futex_wake(key, (int)value);

    return -38;                /* ENOSYS */
}
