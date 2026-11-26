/*
 * Zhaoxu OS - kqueue over the kernel's epoll objects.
 *
 * FreeBSD's event interface is served by the same readiness primitive the
 * kernel already exports; READ and WRITE filters map onto EPOLLIN/EPOLLOUT
 * and EV_DELETE onto EPOLL_CTL_DEL.  Filters without a kernel counterpart
 * are rejected instead of silently ignored.
 */
#include "posix_glue.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/event.h>
#include <time.h>

#define KE_EPOLLIN   0x001u
#define KE_EPOLLOUT  0x004u
#define KE_CTL_ADD   1
#define KE_CTL_DEL   2

/* Create the underlying readiness object. */
int kqueue(void)
{
    long result = zos_syscall3(ZOS_EPOLL_CREATE1, 0, 0, 0);
    if (result < 0) {
        errno = EMFILE;
        return -1;
    }
    return (int)result;
}

/* Translate one kevent filter into the kernel's epoll bits. */
static int filter_bits(short filter)
{
    if (filter == EVFILT_READ)
        return (int)KE_EPOLLIN;
    if (filter == EVFILT_WRITE)
        return (int)KE_EPOLLOUT;
    return -1;
}

/* Deliver pending events; the timeout is honoured in milliseconds. */
int kevent(int fd, const struct kevent *changes, int change_count,
           struct kevent *events, int event_count,
           const struct timespec *timeout)
{
    for (int i = 0; i < change_count; i++) {
        int bits = filter_bits(changes[i].filter);
        if (bits < 0 || changes[i].ident == 0) {
            errno = EINVAL;
            return -1;
        }
        struct zos_epoll_event event;
        memset(&event, 0, sizeof(event));
        event.events = (unsigned int)bits;
        event.data = changes[i].ident;
        int op = (changes[i].flags & EV_DELETE) ? KE_CTL_DEL : KE_CTL_ADD;
        long result = zos_syscall4(ZOS_EPOLL_CTL, fd, op,
                                   (long)changes[i].ident, (long)&event);
        if (result != 0) {
            errno = EINVAL;
            return -1;
        }
    }

    if (event_count <= 0)
        return 0;

    long wait_ms = -1;
    if (timeout)
        wait_ms = (long)timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000;

    struct zos_epoll_event *ready =
        (struct zos_epoll_event *)__builtin_alloca(
            (unsigned long)event_count * sizeof(*ready));
    long count = zos_syscall4(ZOS_EPOLL_WAIT, fd, (long)ready, event_count,
                              wait_ms);
    if (count < 0) {
        errno = EINTR;
        return -1;
    }
    int produced = 0;
    for (long i = 0; i < count; i++) {
        struct kevent *out = &events[produced++];
        memset(out, 0, sizeof(*out));
        out->ident = ready[i].data;
        out->filter = (ready[i].events & KE_EPOLLOUT) ? EVFILT_WRITE
                                                      : EVFILT_READ;
        out->flags = 0;
        out->data = 0;
    }
    return produced;
}
