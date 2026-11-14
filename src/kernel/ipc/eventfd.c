/*
 * Zhaoxu OS - eventfd objects for the Wayland event loop.
 *
 * The object holds one 64-bit counter.  EFD_SEMAPHORE changes a read from
 * "take the whole counter" to "take one", which is what libwayland's event
 * loop uses to wake pollers.
 */
#include <eventfd.h>

#include <mem.h>
#include <poll.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <types.h>

/* The counter can reach UINT64_MAX - 1; UINT64_MAX itself is the value the
 * writer would use to overflow, so it is rejected. */
#define EVENTFD_COUNTER_MAX (~0ULL - 1ULL)

struct eventfd {
    spinlock_t lock;
    u64 counter;
    int semaphore;
    int references;
};

/* Allocate a counter object with one caller reference. */
struct eventfd *eventfd_create(u32 initial, int semaphore)
{
    struct eventfd *event = kmalloc(sizeof(*event));
    if (!event)
        return NULL;
    memset(event, 0, sizeof(*event));
    event->counter = initial;
    event->semaphore = semaphore ? 1 : 0;
    event->references = 1;
    return event;
}

/* Add a reference for a descriptor duplicate or SCM_RIGHTS transfer. */
void eventfd_ref(struct eventfd *event)
{
    if (event)
        __sync_add_and_fetch(&event->references, 1);
}

/* Drop a reference and free the counter when the last one goes away. */
void eventfd_release(struct eventfd *event)
{
    if (event && __sync_sub_and_fetch(&event->references, 1) == 0)
        kfree(event);
}

/* Read one eventfd value.  A blocking read sleeps until a writer adds to the
 * counter; a nonblocking read returns -1 when the counter is zero. */
int eventfd_read(struct eventfd *event, int nonblock, u64 *value)
{
    if (!event || !value)
        return -1;

    for (;;) {
        u64 lock = spin_lock_irqsave(&event->lock);
        if (event->counter) {
            u64 result;
            if (event->semaphore) {
                result = 1;
                event->counter--;
            } else {
                result = event->counter;
                event->counter = 0;
            }
            spin_unlock_irqrestore(&event->lock, lock);
            *value = result;
            return 8;
        }
        spin_unlock_irqrestore(&event->lock, lock);

        if (nonblock)
            return -1;
        thread_sleep_ms(1);
    }
}

/* Add `value` to the counter.  The write blocks when the addition would pass
 * the maximum, matching the Linux eventfd semantics. */
int eventfd_write(struct eventfd *event, u64 value, int nonblock)
{
    if (!event || value == ~0ULL)
        return -1;

    for (;;) {
        u64 lock = spin_lock_irqsave(&event->lock);
        if (event->counter <= EVENTFD_COUNTER_MAX - value) {
            event->counter += value;
            spin_unlock_irqrestore(&event->lock, lock);
            return 8;
        }
        spin_unlock_irqrestore(&event->lock, lock);

        if (nonblock)
            return -1;
        thread_sleep_ms(1);
    }
}

/* Poll readiness: ZPOLLIN when a read would succeed, ZPOLLOUT while the
 * counter can accept another write.  Only requested bits are reported. */
short eventfd_poll(struct eventfd *event, short requested)
{
    if (!event)
        return ZPOLLNVAL;

    u64 lock = spin_lock_irqsave(&event->lock);
    short events = 0;
    if ((requested & ZPOLLIN) && event->counter)
        events |= ZPOLLIN;
    if ((requested & ZPOLLOUT) && event->counter < EVENTFD_COUNTER_MAX)
        events |= ZPOLLOUT;
    spin_unlock_irqrestore(&event->lock, lock);
    return events;
}
