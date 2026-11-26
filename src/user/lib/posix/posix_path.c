/* Zhaoxu OS - newlib path helpers.
 *
 * ZOS has no working-directory state yet, so chdir/getcwd report the root
 * and dirname/basename implement the POSIX string rules. */
#include "posix_glue.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* The process working directory is always "/" for now. */
int chdir(const char *path)
{
    (void)path;
    return 0;
}

/* Copy the fixed root directory into the caller's buffer. */
char *getwd(char *buffer)
{
    if (!buffer)
        return NULL;
    buffer[0] = '/';
    buffer[1] = '\0';
    return buffer;
}

/* POSIX getcwd over the same fixed root; a NULL buffer allocates. */
char *getcwd(char *buffer, size_t size)
{
    if (!buffer) {
        size = size ? size : 2;
        buffer = malloc(size);
        if (!buffer) {
            errno = ENOMEM;
            return NULL;
        }
    } else if (size < 2) {
        errno = ERANGE;
        return NULL;
    }
    buffer[0] = '/';
    buffer[1] = '\0';
    return buffer;
}

/* Strip the final component in place. */
char *dirname(char *path)
{
    if (!path || !*path)
        return (char *)".";
    char *slash = path + strlen(path);
    while (slash > path && slash[-1] == '/')
        slash--;
    while (slash > path && slash[-1] != '/')
        slash--;
    if (slash == path)
        return (char *)".";
    while (slash > path && slash[-1] == '/')
        slash--;
    if (slash == path)
        return (char *)"/";
    *slash = '\0';
    return path;
}

/* Return the final path component, without trailing slashes. */
char *basename(char *path)
{
    if (!path || !*path)
        return (char *)".";
    char *end = path + strlen(path);
    while (end > path && end[-1] == '/')
        end--;
    char *base = end;
    while (base > path && base[-1] != '/')
        base--;
    if (end < path + strlen(path))
        *end = '\0';
    return base;
}

/* File-creation mask; the kernel does not apply it to open() yet. */
static mode_t g_umask = 022;

mode_t umask(mode_t mask)
{
    mode_t old = g_umask;
    g_umask = mask;
    return old;
}

/* The kernel has no open-file mode change. */
int fchmod(int fd, mode_t mode)
{
    (void)fd;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

/* The VFS has no hard links. */
int link(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

/* There is no permission model to change yet. */
int chmod(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

/* Create one directory through syscall 82. */
int mkdir(const char *path, mode_t mode)
{
    (void)mode;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    if (zos_syscall3(ZOS_MKDIR, (long)(uintptr_t)path, 0, 0) != 0) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

/* newlib calls the underscored stub; keep it beside the public mkdir. */
int _mkdir(const char *path, mode_t mode)
{
    return mkdir(path, mode);
}
