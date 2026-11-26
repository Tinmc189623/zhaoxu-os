/*
 * Zhaoxu OS - clocks and entropy syscalls (98 getrandom, 99 clock_gettime).
 *
 * CLOCK_REALTIME comes from the RTC in UTC; CLOCK_MONOTONIC and
 * CLOCK_BOOTTIME share timer_now_ns().  Per-process and per-thread CPU clocks
 * are not accounted yet and report EINVAL instead of an invented value.
 */
#include "process_internal.h"

#include <dev.h>
#include <mem.h>
#include <random.h>
#include <rtc.h>
#include <thread.h>
#include <time.h>
#include <timer.h>
#include <types.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_BOOTTIME  7

#define EFAULT_CODE 14
#define EINVAL_CODE 22

#define GETRANDOM_MAX 8192

struct kernel_timespec {
    i64 seconds;
    i64 nanoseconds;
};

long sys_clock_gettime(struct process *process, u64 clock_id, u64 pointer)
{
    if (user_range_ok(process, pointer, sizeof(struct kernel_timespec)) != 1)
        return -EFAULT_CODE;

    struct kernel_timespec value;
    if (clock_id == CLOCK_REALTIME) {
        struct rtc_time clock;
        if (dev_rtc_read(&clock) != 0)
            return -EINVAL_CODE;
        value.seconds = time_epoch_seconds(&clock);
        value.nanoseconds = 0;
    } else if (clock_id == CLOCK_MONOTONIC || clock_id == CLOCK_MONOTONIC_RAW ||
               clock_id == CLOCK_BOOTTIME) {
        u64 now = timer_now_ns();
        value.seconds = (i64)(now / 1000000000ULL);
        value.nanoseconds = (i64)(now % 1000000000ULL);
    } else {
        return -EINVAL_CODE;
    }

    return process_user_write(process, pointer, &value, sizeof(value)) == 0
               ? 0 : -EFAULT_CODE;
}

/* getrandom: entropy goes through a kernel buffer, then copy_to_user. */
long sys_getrandom(struct process *process, u64 buffer, u64 length, u64 flags)
{
    (void)flags;
    if (length == 0)
        return 0;
    if (length > GETRANDOM_MAX)
        length = GETRANDOM_MAX;
    if (user_range_ok(process, buffer, length) != 1)
        return -EFAULT_CODE;

    u8 *scratch = kmalloc((usize)length);
    if (!scratch)
        return -EINVAL_CODE;
    random_bytes(scratch, (usize)length);
    int status = process_user_write(process, buffer, scratch, length);
    kfree(scratch);
    return status == 0 ? (long)length : -EFAULT_CODE;
}
