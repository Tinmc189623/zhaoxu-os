/*
 * Zhaoxu OS - timerfd objects for the Wayland event loop.
 *
 * Expiration uses the scheduler tick counter.  A relative timer arms at
 * "now + value"; TFD_TIMER_ABSTIME arms at "value" on the same monotonic
 * timeline.  Repeating timers advance by their interval each read.
 */
#include <timerfd.h>

#include <mem.h>
#include <poll.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

struct timerfd {
    spinlock_t lock;
    u64 expire;
    u64 interval;
    u32 clock_id;
    int armed;
    int references;
};

#define NANOSECONDS_PER_SECOND 1000000000ULL

/* Convert a timespec-like value into scheduler ticks.  Invalid negative or
 * out-of-range nanoseconds are rejected with 0. */
static u64 timerfd_ticks_from_spec(struct timerfd_spec spec)
{
    if (spec.seconds < 0 || spec.nanoseconds < 0 ||
        (u64)spec.nanoseconds >= NANOSECONDS_PER_SECOND)
        return 0;

    u64 hz = timer_hz();
    u64 ticks = (u64)spec.seconds * hz;
    ticks += ((u64)spec.nanoseconds * hz + NANOSECONDS_PER_SECOND - 1) /
             NANOSECONDS_PER_SECOND;
    return ticks;
}

/* Convert scheduler ticks back into seconds and nanoseconds. */
static void timerfd_spec_from_ticks(u64 ticks, struct timerfd_spec *spec)
{
    u64 hz = timer_hz();
    spec->seconds = (i64)(ticks / hz);
    spec->nanoseconds =
        (i64)(((ticks % hz) * NANOSECONDS_PER_SECOND + hz / 2) / hz);
    if (spec->nanoseconds >= (i64)NANOSECONDS_PER_SECOND) {
        spec->seconds++;
        spec->nanoseconds -= (i64)NANOSECONDS_PER_SECOND;
    }
}

/* Allocate a disarmed timer with one caller reference. */
struct timerfd *timerfd_create(u32 clock_id)
{
    struct timerfd *timer = kmalloc(sizeof(*timer));
    if (!timer)
        return NULL;
    memset(timer, 0, sizeof(*timer));
    timer->clock_id = clock_id;
    timer->references = 1;
    return timer;
}

/* Add a reference for a descriptor duplicate. */
void timerfd_ref(struct timerfd *timer)
{
    if (timer)
        __sync_add_and_fetch(&timer->references, 1);
}

/* Drop a reference and free the timer when the last one goes away. */
void timerfd_release(struct timerfd *timer)
{
    if (timer && __sync_sub_and_fetch(&timer->references, 1) == 0)
        kfree(timer);
}

/* Arm or disarm the timer.  A zero value disarms it; a repeating timer keeps
 * its interval even while disarmed so a later settime can reuse it. */
int timerfd_settime(struct timerfd *timer, int absolute,
                    struct timerfd_spec value, struct timerfd_spec interval,
                    struct timerfd_spec *old_value,
                    struct timerfd_spec *old_interval)
{
    if (!timer)
        return -1;
    if (value.seconds < 0 || (u64)value.nanoseconds >= NANOSECONDS_PER_SECOND ||
        interval.seconds < 0 ||
        (u64)interval.nanoseconds >= NANOSECONDS_PER_SECOND)
        return -1;

    u64 lock = spin_lock_irqsave(&timer->lock);
    if (old_value) {
        u64 now = timer_ticks();
        u64 remaining = timer->armed && timer->expire > now
                            ? timer->expire - now
                            : 0;
        timerfd_spec_from_ticks(remaining, old_value);
    }
    if (old_interval)
        timerfd_spec_from_ticks(timer->interval, old_interval);

    u64 value_ticks = timerfd_ticks_from_spec(value);
    u64 interval_ticks = timerfd_ticks_from_spec(interval);
    if (interval_ticks == 0 && (interval.seconds || interval.nanoseconds))
        interval_ticks = 1;

    if (value_ticks == 0) {
        timer->armed = 0;
        timer->expire = 0;
    } else {
        u64 base = absolute ? 0 : timer_ticks();
        timer->expire = base + value_ticks;
        timer->armed = 1;
    }
    timer->interval = interval_ticks;
    spin_unlock_irqrestore(&timer->lock, lock);
    return 0;
}

/* Report the time remaining and the configured interval. */
int timerfd_gettime(struct timerfd *timer, struct timerfd_spec *value,
                    struct timerfd_spec *interval)
{
    if (!timer || !value || !interval)
        return -1;

    u64 lock = spin_lock_irqsave(&timer->lock);
    u64 now = timer_ticks();
    u64 remaining = timer->armed && timer->expire > now
                        ? timer->expire - now
                        : 0;
    timerfd_spec_from_ticks(remaining, value);
    timerfd_spec_from_ticks(timer->interval, interval);
    spin_unlock_irqrestore(&timer->lock, lock);
    return 0;
}

/* Read the number of expirations since the last read.  A repeating timer
 * advances by whole intervals; a one-shot timer disarms after the read. */
int timerfd_read(struct timerfd *timer, int nonblock, u64 *expirations)
{
    if (!timer || !expirations)
        return -1;

    for (;;) {
        u64 lock = spin_lock_irqsave(&timer->lock);
        u64 now = timer_ticks();
        if (timer->armed && now >= timer->expire) {
            u64 count = 1;
            if (timer->interval) {
                count += (now - timer->expire) / timer->interval;
                timer->expire += count * timer->interval;
            } else {
                timer->armed = 0;
                timer->expire = 0;
            }
            spin_unlock_irqrestore(&timer->lock, lock);
            *expirations = count;
            return 8;
        }
        spin_unlock_irqrestore(&timer->lock, lock);

        if (nonblock)
            return -1;
        thread_sleep_ms(1);
    }
}

/* Poll readiness: ZPOLLIN once the timer has expired, when requested. */
short timerfd_poll(struct timerfd *timer, short requested)
{
    if (!timer)
        return ZPOLLNVAL;

    u64 lock = spin_lock_irqsave(&timer->lock);
    u64 now = timer_ticks();
    short events = (requested & ZPOLLIN) && timer->armed && now >= timer->expire
                       ? ZPOLLIN
                       : 0;
    spin_unlock_irqrestore(&timer->lock, lock);
    return events;
}
