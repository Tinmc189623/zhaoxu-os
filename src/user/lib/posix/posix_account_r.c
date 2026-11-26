/*
 * Zhaoxu OS - reentrant account lookups and the shadow entry.
 *
 * The non-reentrant getpwnam and getgrnam calls read /etc/passwd and
 * /etc/group through the VFS; these wrappers copy the strings into the
 * caller's buffer and return ERANGE when it is too small, as POSIX requires.
 */
#include "posix_glue.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* Copy one string into the caller's buffer; returns 0 or ERANGE. */
static int store_string(const char *source, char *buffer, size_t size,
                        size_t *used, char **target)
{
    size_t length = strlen(source) + 1;
    if (*used + length > size)
        return ERANGE;
    memcpy(buffer + *used, source, length);
    *target = buffer + *used;
    *used += length;
    return 0;
}

/* Fill a passwd record inside the caller's buffer. */
static int copy_passwd(const struct passwd *source, struct passwd *result,
                       char *buffer, size_t size, struct passwd **found)
{
    size_t used = 0;
    int status = store_string(source->pw_name, buffer, size, &used,
                              &result->pw_name);
    if (status == 0)
        status = store_string(source->pw_passwd ? source->pw_passwd : "",
                              buffer, size, &used, &result->pw_passwd);
    if (status == 0)
        status = store_string(source->pw_gecos ? source->pw_gecos : "",
                              buffer, size, &used, &result->pw_gecos);
    if (status == 0)
        status = store_string(source->pw_dir ? source->pw_dir : "",
                              buffer, size, &used, &result->pw_dir);
    if (status == 0)
        status = store_string(source->pw_shell ? source->pw_shell : "",
                              buffer, size, &used, &result->pw_shell);
    if (status != 0) {
        *found = NULL;
        return status;
    }
    result->pw_uid = source->pw_uid;
    result->pw_gid = source->pw_gid;
    *found = result;
    return 0;
}

/* Fill a group record inside the caller's buffer. */
static int copy_group(const struct group *source, struct group *result,
                      char *buffer, size_t size, struct group **found)
{
    size_t used = 0;
    int status = store_string(source->gr_name, buffer, size, &used,
                              &result->gr_name);
    if (status == 0)
        status = store_string(source->gr_passwd ? source->gr_passwd : "",
                              buffer, size, &used, &result->gr_passwd);
    if (status != 0) {
        *found = NULL;
        return status;
    }
    result->gr_gid = source->gr_gid;
    /* Member lists are not copied: the kernel has no supplementary groups. */
    result->gr_mem = NULL;
    *found = result;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *result, char *buffer,
               size_t size, struct passwd **found)
{
    if (!name || !result || !buffer || !found) {
        errno = EINVAL;
        return EINVAL;
    }
    *found = NULL;
    struct passwd *entry = getpwnam(name);
    if (!entry)
        return errno == 0 ? ENOENT : errno;
    return copy_passwd(entry, result, buffer, size, found);
}

int getpwuid_r(uid_t uid, struct passwd *result, char *buffer, size_t size,
               struct passwd **found)
{
    if (!result || !buffer || !found) {
        errno = EINVAL;
        return EINVAL;
    }
    *found = NULL;
    struct passwd *entry = getpwuid(uid);
    if (!entry)
        return errno == 0 ? ENOENT : errno;
    return copy_passwd(entry, result, buffer, size, found);
}

int getgrnam_r(const char *name, struct group *result, char *buffer,
               size_t size, struct group **found)
{
    if (!name || !result || !buffer || !found) {
        errno = EINVAL;
        return EINVAL;
    }
    *found = NULL;
    struct group *entry = getgrnam(name);
    if (!entry)
        return errno == 0 ? ENOENT : errno;
    return copy_group(entry, result, buffer, size, found);
}

int getgrgid_r(gid_t gid, struct group *result, char *buffer, size_t size,
               struct group **found)
{
    if (!result || !buffer || !found) {
        errno = EINVAL;
        return EINVAL;
    }
    *found = NULL;
    struct group *entry = getgrgid(gid);
    if (!entry)
        return errno == 0 ? ENOENT : errno;
    return copy_group(entry, result, buffer, size, found);
}

/* Read the /etc/shadow line for one account. */
struct spwd *getspnam(const char *name)
{
    static struct spwd entry;
    static char name_buffer[64];
    static char password_buffer[256];

    if (!name)
        return NULL;
    FILE *file = fopen("/etc/shadow", "r");
    if (!file) {
        errno = ENOENT;
        return NULL;
    }
    char line[512];
    struct spwd *result = NULL;
    while (fgets(line, (int)sizeof(line), file)) {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        if (strncmp(line, name, strlen(name)) != 0 || line[strlen(name)] != ':')
            continue;
        char *field = line;
        char *fields[9];
        int count = 0;
        while (field && count < 9) {
            fields[count++] = field;
            char *colon = strchr(field, ':');
            if (colon)
                *colon = '\0';
            field = colon ? colon + 1 : NULL;
        }
        if (count < 2)
            break;
        snprintf(name_buffer, sizeof(name_buffer), "%s", fields[0]);
        snprintf(password_buffer, sizeof(password_buffer), "%s", fields[1]);
        entry.sp_namp = name_buffer;
        entry.sp_pwdp = password_buffer;
        entry.sp_lstchg = count > 2 ? strtol(fields[2], NULL, 10) : 0;
        entry.sp_min = count > 3 ? strtol(fields[3], NULL, 10) : 0;
        entry.sp_max = count > 4 ? strtol(fields[4], NULL, 10) : 0;
        entry.sp_warn = count > 5 ? strtol(fields[5], NULL, 10) : 0;
        entry.sp_inact = count > 6 ? strtol(fields[6], NULL, 10) : 0;
        entry.sp_expire = count > 7 ? strtol(fields[7], NULL, 10) : 0;
        entry.sp_flag = count > 8 ? (unsigned long)strtoul(fields[8], NULL, 10)
                                  : 0;
        result = &entry;
        break;
    }
    fclose(file);
    if (!result)
        errno = ENOENT;
    return result;
}

