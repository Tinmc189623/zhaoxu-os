/* Zhaoxu OS - newlib path helpers.
 *
 * ZOS has no working-directory state yet, so chdir/getcwd report the root
 * and dirname/basename implement the POSIX string rules. */
#include <errno.h>
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

/* POSIX getcwd over the same fixed root. */
char *getcwd(char *buffer, size_t size)
{
    if (!buffer || size < 2)
        return NULL;
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

/* There is no permission model to change yet. */
int chmod(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

/* Directory creation is not exposed by the kernel yet. */
int mkdir(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

/* newlib calls the underscored stub; keep it beside the public mkdir. */
int _mkdir(const char *path, mode_t mode)
{
    return mkdir(path, mode);
}
