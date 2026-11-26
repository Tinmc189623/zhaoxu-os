/*
 * Zhaoxu OS - poll over the kernel's descriptor readiness call.
 *
 * The kernel takes the same struct pollfd layout the header declares and
 * blocks in millisecond units; a negative timeout waits indefinitely.
 */
#include "posix_glue.h"

#include <errno.h>
#include <poll.h>
#include <stddef.h>

int poll(struct pollfd *descriptors, unsigned long count, int timeout)
{
    /* No descriptors means "sleep for the timeout"; the kernel rejects 0. */
    if (count == 0) {
        if (timeout > 0)
            zos_syscall3(ZOS_SLEEP, timeout, 0, 0);
        return 0;
    }
    if (count > 64) {
        errno = EINVAL;
        return -1;
    }
    long result = zos_syscall3(ZOS_POLL, (long)(uintptr_t)descriptors,
                               (long)count, (long)timeout);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)result;
}
