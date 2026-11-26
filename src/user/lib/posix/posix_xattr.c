/*
 * Zhaoxu OS - extended attributes.
 *
 * The VFS stores path, size and type only; there is no attribute store.
 * Each call fails with ENOTSUP so tools that probe for xattr support take
 * their "not supported" branch.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/xattr.h>

static ssize_t xattr_unsupported(void)
{
    errno = ENOTSUP;
    return -1;
}

ssize_t getxattr(const char *path, const char *name, void *value, size_t size)
{
    (void)path; (void)name; (void)value; (void)size;
    return xattr_unsupported();
}

ssize_t lgetxattr(const char *path, const char *name, void *value,
                  size_t size)
{
    (void)path; (void)name; (void)value; (void)size;
    return xattr_unsupported();
}

ssize_t fgetxattr(int fd, const char *name, void *value, size_t size)
{
    (void)fd; (void)name; (void)value; (void)size;
    return xattr_unsupported();
}

ssize_t listxattr(const char *path, char *list, size_t size)
{
    (void)path; (void)list; (void)size;
    return xattr_unsupported();
}

ssize_t llistxattr(const char *path, char *list, size_t size)
{
    (void)path; (void)list; (void)size;
    return xattr_unsupported();
}

ssize_t flistxattr(int fd, char *list, size_t size)
{
    (void)fd; (void)list; (void)size;
    return xattr_unsupported();
}

int setxattr(const char *path, const char *name, const void *value,
             size_t size, int flags)
{
    (void)path; (void)name; (void)value; (void)size; (void)flags;
    errno = ENOTSUP;
    return -1;
}

int lsetxattr(const char *path, const char *name, const void *value,
              size_t size, int flags)
{
    (void)path; (void)name; (void)value; (void)size; (void)flags;
    errno = ENOTSUP;
    return -1;
}

int fsetxattr(int fd, const char *name, const void *value, size_t size,
              int flags)
{
    (void)fd; (void)name; (void)value; (void)size; (void)flags;
    errno = ENOTSUP;
    return -1;
}

int removexattr(const char *path, const char *name)
{
    (void)path; (void)name;
    errno = ENOTSUP;
    return -1;
}

int lremovexattr(const char *path, const char *name)
{
    (void)path; (void)name;
    errno = ENOTSUP;
    return -1;
}

int fremovexattr(int fd, const char *name)
{
    (void)fd; (void)name;
    errno = ENOTSUP;
    return -1;
}
