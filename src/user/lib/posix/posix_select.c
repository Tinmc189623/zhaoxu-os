/* Zhaoxu OS - select(2) over the kernel poll syscall.
 *
 * The kernel exposes structured poll readiness only; fd_set conversion and
 * the timeout arithmetic live here.  A select with no descriptors sleeps for
 * the requested timeout, because poll rejects a zero count.
 */
#include "posix_glue.h"

#include <errno.h>
#include <stddef.h>
#include <sys/select.h>
#include <sys/time.h>

#define ZPOLL_MAX 64

struct zos_pollfd {
    int fd;
    short events;
    short revents;
};

#define ZPOLLIN   0x001
#define ZPOLLOUT  0x004
#define ZPOLLERR  0x008
#define ZPOLLHUP  0x010
#define ZPOLLNVAL 0x020

/* Convert a timeval to milliseconds; -1 means block forever. */
static long timeout_ms(const struct timeval *timeout)
{
    if (!timeout)
        return -1;
    return (long)timeout->tv_sec * 1000 +
           ((long)timeout->tv_usec + 999) / 1000;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    struct zos_pollfd fds[ZPOLL_MAX];
    int map[ZPOLL_MAX];
    int count = 0;
    int limit = nfds < FD_SETSIZE ? nfds : FD_SETSIZE;

    for (int fd = 0; fd < limit; fd++) {
        short events = 0;
        if (readfds && FD_ISSET(fd, readfds))
            events |= ZPOLLIN;
        if (writefds && FD_ISSET(fd, writefds))
            events |= ZPOLLOUT;
        if (exceptfds && FD_ISSET(fd, exceptfds))
            events |= ZPOLLERR;
        if (!events)
            continue;
        if (count == ZPOLL_MAX) {
            errno = EINVAL;
            return -1;
        }
        fds[count].fd = fd;
        fds[count].events = events;
        fds[count].revents = 0;
        map[count] = fd;
        count++;
    }

    long ms = timeout_ms(timeout);
    if (count == 0) {
        if (!timeout) {
            errno = EINVAL;
            return -1;
        }
        zos_syscall3(ZOS_SLEEP, ms, 0, 0);
        return 0;
    }

    long result = zos_syscall3(ZOS_POLL, (long)fds, count, ms);
    if (result < 0) {
        errno = EINTR;
        return -1;
    }

    if (readfds)
        FD_ZERO(readfds);
    if (writefds)
        FD_ZERO(writefds);
    if (exceptfds)
        FD_ZERO(exceptfds);

    int ready = 0;
    for (int i = 0; i < count; i++) {
        short revents = fds[i].revents;
        if (!revents)
            continue;
        ready++;
        if (readfds && (revents & (ZPOLLIN | ZPOLLHUP | ZPOLLERR)))
            FD_SET(map[i], readfds);
        if (writefds && (revents & (ZPOLLOUT | ZPOLLERR)))
            FD_SET(map[i], writefds);
        if (exceptfds && (revents & (ZPOLLERR | ZPOLLHUP | ZPOLLNVAL)))
            FD_SET(map[i], exceptfds);
    }
    return ready;
}
