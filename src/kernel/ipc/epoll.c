/*
 * Zhaoxu OS - epoll objects for the Wayland event loop.
 *
 * Registration is a fixed table per epoll descriptor.  epoll_wait copies the
 * table under the object lock, then polls each target without holding it so a
 * socket lock can be taken safely.  The wait is level-triggered and uses the
 * same 1 ms scheduler sleep as poll.
 */
#include <epoll.h>

#include <mem.h>
#include <poll.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>
#include <zfd.h>

struct epoll_entry {
    int used;
    int fd;
    u32 events;
    u64 data;
};

struct epoll {
    spinlock_t lock;
    int references;
    struct epoll_entry entries[EPOLL_MAX_ENTRIES];
};

/* Allocate an empty epoll object with one caller reference. */
struct epoll *epoll_create(void)
{
    struct epoll *epoll = kmalloc(sizeof(*epoll));
    if (!epoll)
        return NULL;
    memset(epoll, 0, sizeof(*epoll));
    epoll->references = 1;
    return epoll;
}

/* Add a reference for a descriptor duplicate. */
void epoll_ref(struct epoll *epoll)
{
    if (epoll)
        __sync_add_and_fetch(&epoll->references, 1);
}

/* Drop a reference and free the registration table at zero. */
void epoll_release(struct epoll *epoll)
{
    if (epoll && __sync_sub_and_fetch(&epoll->references, 1) == 0)
        kfree(epoll);
}

/* Add, modify or delete one registration.  The target must be a live zfd
 * descriptor; nesting an epoll inside another epoll is rejected. */
int epoll_ctl(struct process *process, struct epoll *epoll, int op, int fd,
              u32 events, u64 data)
{
    if (!process || !epoll)
        return -1;
    struct zfd_entry *target = zfd_get(process, fd);
    if (!target || target->type == ZFD_TYPE_EPOLL)
        return -1;

    u64 lock = spin_lock_irqsave(&epoll->lock);
    int slot = -1;
    for (int i = 0; i < EPOLL_MAX_ENTRIES; i++) {
        if (epoll->entries[i].used && epoll->entries[i].fd == fd) {
            slot = i;
            break;
        }
    }

    if (op == EPOLL_CTL_DEL) {
        if (slot < 0) {
            spin_unlock_irqrestore(&epoll->lock, lock);
            return -1;
        }
        memset(&epoll->entries[slot], 0, sizeof(epoll->entries[slot]));
        spin_unlock_irqrestore(&epoll->lock, lock);
        return 0;
    }

    if (op == EPOLL_CTL_MOD) {
        if (slot < 0) {
            spin_unlock_irqrestore(&epoll->lock, lock);
            return -1;
        }
        epoll->entries[slot].events = events;
        epoll->entries[slot].data = data;
        spin_unlock_irqrestore(&epoll->lock, lock);
        return 0;
    }

    if (op != EPOLL_CTL_ADD || slot >= 0) {
        spin_unlock_irqrestore(&epoll->lock, lock);
        return -1;
    }

    for (int i = 0; i < EPOLL_MAX_ENTRIES; i++) {
        if (!epoll->entries[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        spin_unlock_irqrestore(&epoll->lock, lock);
        return -1;
    }
    epoll->entries[slot].used = 1;
    epoll->entries[slot].fd = fd;
    epoll->entries[slot].events = events;
    epoll->entries[slot].data = data;
    spin_unlock_irqrestore(&epoll->lock, lock);
    return 0;
}

/* Wait for at least one registered descriptor.  timeout_ms is 0 for an
 * immediate poll, negative to wait indefinitely, positive for milliseconds. */
int epoll_wait(struct process *process, struct epoll *epoll,
               struct epoll_result *results, int max_events, int timeout_ms)
{
    if (!process || !epoll || !results || max_events <= 0)
        return -1;
    if (max_events > EPOLL_MAX_ENTRIES)
        max_events = EPOLL_MAX_ENTRIES;

    u64 deadline = 0;
    if (timeout_ms > 0)
        deadline = timer_ticks() +
                   ((u64)timeout_ms * timer_hz() + 999) / 1000;

    for (;;) {
        struct epoll_entry snapshot[EPOLL_MAX_ENTRIES];
        u64 lock = spin_lock_irqsave(&epoll->lock);
        memcpy(snapshot, epoll->entries, sizeof(snapshot));
        spin_unlock_irqrestore(&epoll->lock, lock);

        int ready = 0;
        for (int i = 0; i < EPOLL_MAX_ENTRIES && ready < max_events; i++) {
            if (!snapshot[i].used)
                continue;
            short polled = zfd_poll_events(process, snapshot[i].fd,
                                           (short)snapshot[i].events);
            u32 events = 0;
            if (polled & ZPOLLIN)
                events |= EPOLLIN;
            if (polled & ZPOLLOUT)
                events |= EPOLLOUT;
            if (polled & (ZPOLLERR | ZPOLLNVAL))
                events |= EPOLLERR;
            if (polled & ZPOLLHUP)
                events |= EPOLLHUP;
            events &= snapshot[i].events | EPOLLERR | EPOLLHUP;
            if (!events)
                continue;
            results[ready].events = events;
            results[ready].data = snapshot[i].data;
            ready++;
        }
        if (ready)
            return ready;
        if (timeout_ms == 0)
            return 0;
        if (timeout_ms > 0 && timer_ticks() >= deadline)
            return 0;
        thread_sleep_ms(1);
    }
}
