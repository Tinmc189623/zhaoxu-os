/* Group database lookups. */
#ifndef ZHAOXU_GRP_H
#define ZHAOXU_GRP_H

#include <sys/types.h>

struct group {
    char *gr_name;
    char *gr_passwd;
    gid_t gr_gid;
    char **gr_mem;
};

struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);
int getgrouplist(const char *user, gid_t group, gid_t *groups, int *count);
int getgrnam_r(const char *name, struct group *result, char *buffer,
               size_t size, struct group **found);
int getgrgid_r(gid_t gid, struct group *result, char *buffer, size_t size,
               struct group **found);
int initgroups(const char *user, gid_t group);

#endif
