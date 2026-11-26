/*
 * Zhaoxu OS - inotify surface.
 *
 * File-watch notifications belong to the FreeBSD event layer; the kernel
 * exposes no vnode watch yet, so the interface fails at init time rather
 * than handing back a descriptor that can never fire.
 */
#include <errno.h>
#include <sys/inotify.h>

int inotify_init(void)
{
    errno = ENOSYS;
    return -1;
}

int inotify_add_watch(int fd, const char *path, unsigned int mask)
{
    (void)fd;
    (void)path;
    (void)mask;
    errno = ENOSYS;
    return -1;
}

int inotify_rm_watch(int fd, int watch)
{
    (void)fd;
    (void)watch;
    errno = ENOSYS;
    return -1;
}
