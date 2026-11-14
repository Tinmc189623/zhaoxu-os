#ifndef ZHAOXU_TIMERFD_H
#define ZHAOXU_TIMERFD_H

#include <types.h>

/* timerfd object: a monotonic expiration counter exposed as a descriptor.
 * Only CLOCK_MONOTONIC semantics are implemented; CLOCK_REALTIME is accepted
 * and mapped to the same tick source. */
struct timerfd_spec {
    i64 seconds;
    i64 nanoseconds;
};

struct timerfd;

struct timerfd *timerfd_create(u32 clock_id);
void timerfd_ref(struct timerfd *timer);
void timerfd_release(struct timerfd *timer);
int timerfd_settime(struct timerfd *timer, int absolute,
                    struct timerfd_spec value, struct timerfd_spec interval,
                    struct timerfd_spec *old_value,
                    struct timerfd_spec *old_interval);
int timerfd_gettime(struct timerfd *timer, struct timerfd_spec *value,
                    struct timerfd_spec *interval);
int timerfd_read(struct timerfd *timer, int nonblock, u64 *expirations);
short timerfd_poll(struct timerfd *timer, short requested);

#endif
