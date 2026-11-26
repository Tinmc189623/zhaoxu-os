/*
 * Zhaoxu OS - mount table and filesystem statistics.
 *
 * The kernel keeps no user-visible mount list: / is the initramfs-backed
 * root and /mnt is the data volume, neither of which can be enquired about
 * from Ring 3.  mount and umount have no syscall.  mountlist_istype is the
 * BSD libc helper toybox links against, so it keeps its real semantics.
 */
#include "posix_glue.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/* Mirror of toybox's struct mtab_list so the type pointer lines up. */
struct zos_mtab_entry {
    struct zos_mtab_entry *next;
    struct zos_mtab_entry *prev;
    struct stat stat_record;
    struct statvfs statvfs_record;
    char *dir;
    char *device;
    char *opts;
    char type[];
};

/* Split the next comma-separated token; advances *list past it. */
static char *comma_next(char **list, int *length)
{
    if (!*list || !**list)
        return NULL;
    char *token = *list;
    char *comma = strchr(token, ',');
    *length = comma ? (int)(comma - token) : (int)strlen(token);
    *list = comma ? comma + 1 : token + *length;
    return token;
}

/* Drop a leading "no" from a token. */
static char *skip_no(char *token)
{
    return (token[0] == 'n' && token[1] == 'o') ? token + 2 : token;
}

/* BSD libc helper: "noA,B" matches only when the type is not listed. */
int mountlist_istype(struct zos_mtab_entry *entry, char *typelist)
{
    if (!entry || !typelist)
        return 1;

    /* A leading "no" inverts the whole list. */
    int skip = strncmp(typelist, "no", 2);
    char *cursor = typelist;
    for (;;) {
        int length = 0;
        char *token = comma_next(&cursor, &length);
        if (!token)
            break;
        if (!skip) {
            /* Later "no" prefixes are ignored. */
            char *base = skip_no(token);
            if (!strncmp(base, entry->type, (size_t)(length - 2))) {
                skip = 1;
                break;
            }
        } else if (!strncmp(token, entry->type, (size_t)length) &&
                   !entry->type[length]) {
            skip = 0;
            break;
        }
    }
    return !skip;
}

int mount(const char *source, const char *target, const char *type,
          unsigned long flags, const void *data)
{
    (void)source;
    (void)target;
    (void)type;
    (void)flags;
    (void)data;
    errno = ENOSYS;
    return -1;
}

int umount(const char *target)
{
    (void)target;
    errno = ENOSYS;
    return -1;
}

int umount2(const char *target, int flags)
{
    (void)flags;
    return umount(target);
}

int statfs(const char *path, struct statfs *buffer)
{
    (void)path;
    (void)buffer;
    errno = ENOSYS;
    return -1;
}

int fstatfs(int fd, struct statfs *buffer)
{
    (void)fd;
    (void)buffer;
    errno = ENOSYS;
    return -1;
}

int statvfs(const char *path, struct statvfs *buffer)
{
    (void)path;
    (void)buffer;
    errno = ENOSYS;
    return -1;
}

int fstatvfs(int fd, struct statvfs *buffer)
{
    (void)fd;
    (void)buffer;
    errno = ENOSYS;
    return -1;
}

/* No mount list is published; callers see an empty table. */
int getmntinfo(struct statfs **buffer, int flags)
{
    (void)flags;
    if (buffer)
        *buffer = NULL;
    errno = ENOENT;
    return 0;
}

int swapon(const char *path, int flags)
{
    (void)path;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int swapoff(const char *path)
{
    (void)path;
    errno = ENOSYS;
    return -1;
}
