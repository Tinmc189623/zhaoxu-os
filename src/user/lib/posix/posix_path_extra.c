/*
 * Zhaoxu OS - directory-relative path calls and metadata operations.
 *
 * The VFS syscall table has no mkdir, symlink, ownership or timestamp
 * entry, so each of those reports the gap with a real errno.  Calls that
 * reduce to open/stat/unlink are answered for real.
 */
#include "posix_glue.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

/* Only AT_FDCWD exists: the kernel resolves absolute paths from the root. */
static int at_supported(int dirfd, const char *path)
{
    if (!path)
        return 0;
    if (path[0] == '/')
        return 1;
    return dirfd == AT_FDCWD;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    if (!at_supported(dirfd, path)) {
        errno = ENOTSUP;
        return -1;
    }
    return open(path, flags);
}

int fstatat(int dirfd, const char *path, struct stat *status, int flags)
{
    (void)flags;
    if (!at_supported(dirfd, path)) {
        errno = ENOTSUP;
        return -1;
    }
    return stat(path, status);
}

int lstat(const char *path, struct stat *status)
{
    return stat(path, status);
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
    (void)flags;
    if (!at_supported(dirfd, path)) {
        errno = ENOTSUP;
        return -1;
    }
    return access(path, mode);
}

int unlinkat(int dirfd, const char *path, int flags)
{
    if (flags != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!at_supported(dirfd, path)) {
        errno = ENOTSUP;
        return -1;
    }
    return unlink(path);
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    (void)dirfd;
    return mkdir(path, mode);
}

int rmdir(const char *path)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (zos_syscall3(ZOS_RMDIR, (long)(uintptr_t)path, 0, 0) != 0) {
        errno = ENOTEMPTY;
        return -1;
    }
    return 0;
}

int mknod(const char *path, mode_t mode, dev_t device)
{
    (void)path;
    (void)mode;
    (void)device;
    errno = ENOSYS;
    return -1;
}

int mknodat(int dirfd, const char *path, mode_t mode, dev_t device)
{
    (void)dirfd;
    return mknod(path, mode, device);
}

int symlink(const char *target, const char *path)
{
    (void)target;
    (void)path;
    errno = ENOSYS;
    return -1;
}

int symlinkat(const char *target, int dirfd, const char *path)
{
    (void)dirfd;
    return symlink(target, path);
}

int linkat(int olddirfd, const char *oldpath, int newdirfd,
           const char *newpath, int flags)
{
    (void)olddirfd;
    (void)newdirfd;
    (void)flags;
    return link(oldpath, newpath);
}

ssize_t readlink(const char *path, char *buffer, size_t length)
{
    (void)path;
    (void)buffer;
    (void)length;
    errno = EINVAL;                    /* the VFS has no symbolic links */
    return -1;
}

ssize_t readlinkat(int dirfd, const char *path, char *buffer, size_t length)
{
    (void)dirfd;
    return readlink(path, buffer, length);
}

/* Rename within one mounted filesystem through syscall 84. */
int rename(const char *from, const char *to)
{
    if (!from || !to) {
        errno = EINVAL;
        return -1;
    }
    if (zos_syscall3(ZOS_RENAME, (long)(uintptr_t)from,
                     (long)(uintptr_t)to, 0) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int chroot(const char *path)
{
    (void)path;
    errno = EPERM;
    return -1;
}

int fchdir(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

/* Every file belongs to the single root identity; other owners are refused. */
static int owner_allowed(uid_t uid, gid_t gid)
{
    if (uid != (uid_t)-1 && uid != 0) {
        errno = EPERM;
        return 0;
    }
    if (gid != (gid_t)-1 && gid != 0) {
        errno = EPERM;
        return 0;
    }
    return 1;
}

int chown(const char *path, uid_t uid, gid_t gid)
{
    int fd = open(path, 0);
    if (fd < 0)
        return -1;
    close(fd);
    return owner_allowed(uid, gid) ? 0 : -1;
}

int lchown(const char *path, uid_t uid, gid_t gid)
{
    return chown(path, uid, gid);
}

int fchown(int fd, uid_t uid, gid_t gid)
{
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    return owner_allowed(uid, gid) ? 0 : -1;
}

int fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flags)
{
    (void)flags;
    if (!at_supported(dirfd, path)) {
        errno = ENOTSUP;
        return -1;
    }
    return chown(path, uid, gid);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
    (void)flags;
    (void)mode;
    if (!at_supported(dirfd, path)) {
        errno = ENOTSUP;
        return -1;
    }
    errno = ENOSYS;
    return -1;
}

int utimensat(int dirfd, const char *path, const struct timespec times[2],
              int flags)
{
    (void)dirfd;
    (void)path;
    (void)times;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int futimens(int fd, const struct timespec times[2])
{
    (void)fd;
    (void)times;
    errno = ENOSYS;
    return -1;
}
