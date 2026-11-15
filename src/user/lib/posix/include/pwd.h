#ifndef ZHAOXU_PWD_H
#define ZHAOXU_PWD_H

/* newlib's passwd database is not implemented; this header gives nano the
 * POSIX declarations with a single built-in account. */
#include <sys/types.h>

struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);
struct passwd *getpwent(void);
void setpwent(void);
void endpwent(void);

#endif
