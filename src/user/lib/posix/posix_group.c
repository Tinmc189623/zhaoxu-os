/*
 * Zhaoxu OS - group database lookups.
 *
 * /etc/group is a plain colon-separated file in the rootfs; these parse it
 * directly so getgrgid and getgrnam answer from the real image contents.
 */
#include <errno.h>
#include <grp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fill the static group record from one /etc/group line. */
static int group_from_line(char *line, struct group *entry, char *name,
                           char *password)
{
    char *fields[4];
    int count = 0;
    char *cursor = line;
    while (cursor && count < 4) {
        fields[count++] = cursor;
        char *colon = strchr(cursor, ':');
        if (colon)
            *colon = '\0';
        cursor = colon ? colon + 1 : NULL;
    }
    if (count < 3)
        return -1;
    snprintf(name, 64, "%s", fields[0]);
    snprintf(password, 64, "%s", fields[1]);
    entry->gr_name = name;
    entry->gr_passwd = password;
    entry->gr_gid = (gid_t)strtoul(fields[2], NULL, 10);
    entry->gr_mem = NULL;
    return 0;
}

/* Look up a group by numeric id. */
struct group *getgrgid(gid_t gid)
{
    static struct group entry;
    static char name[64];
    static char password[64];
    FILE *file = fopen("/etc/group", "r");
    if (!file) {
        errno = ENOENT;
        return NULL;
    }
    char line[256];
    struct group *found = NULL;
    while (fgets(line, (int)sizeof(line), file)) {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        if (group_from_line(line, &entry, name, password) != 0)
            continue;
        if (entry.gr_gid == gid) {
            found = &entry;
            break;
        }
    }
    fclose(file);
    if (!found)
        errno = ENOENT;
    return found;
}

/* Look up a group by name. */
struct group *getgrnam(const char *wanted)
{
    static struct group entry;
    static char name[64];
    static char password[64];
    if (!wanted) {
        errno = EINVAL;
        return NULL;
    }
    FILE *file = fopen("/etc/group", "r");
    if (!file) {
        errno = ENOENT;
        return NULL;
    }
    char line[256];
    struct group *found = NULL;
    while (fgets(line, (int)sizeof(line), file)) {
        char *end = strchr(line, '\n');
        if (end)
            *end = '\0';
        if (group_from_line(line, &entry, name, password) != 0)
            continue;
        if (strcmp(entry.gr_name, wanted) == 0) {
            found = &entry;
            break;
        }
    }
    fclose(file);
    if (!found)
        errno = ENOENT;
    return found;
}
