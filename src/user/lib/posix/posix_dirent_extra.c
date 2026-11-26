/*
 * Zhaoxu OS - directory and kernel-log corners.
 *
 * fdopendir needs a way to turn an open descriptor into a directory stream;
 * the VFS has no such call.  klogctl would need the kernel's log ring, which
 * is not exported to Ring 3 (the serial driver already mirrors it).
 */
#include "posix_glue.h"

#include <dirent.h>
#include <errno.h>
#include <sys/klog.h>

DIR *fdopendir(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return NULL;
}

/* dmesg reads the kernel log ring through syscall 81. */
int klogctl(int type, char *buffer, int length)
{
    long result = zos_syscall3(ZOS_KLOG, type, (long)(uintptr_t)buffer,
                               length);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)result;
}
