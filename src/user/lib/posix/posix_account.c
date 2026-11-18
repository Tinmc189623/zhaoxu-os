/* Zhaoxu OS - newlib account and host-name glue.
 *
 * ZOS has a single built-in account; nano only needs it to label its status
 * bar and to satisfy libc's getpwuid/getpwnam paths. */
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static char g_user[] = "zhaoxu";
static char g_home[] = "/";
static char g_shell[] = "";

static struct passwd g_passwd = {
    g_user, (char *)"x", 0, 0, (char *)"", g_home, g_shell
};

/* Every process runs as the same unprivileged account. */
uid_t getuid(void)
{
    return 0;
}

uid_t geteuid(void)
{
    return 0;
}

gid_t getgid(void)
{
    return 0;
}

gid_t getegid(void)
{
    return 0;
}

/* Resolve any uid to the built-in account. */
struct passwd *getpwuid(uid_t uid)
{
    (void)uid;
    return &g_passwd;
}

/* Resolve any name to the built-in account. */
struct passwd *getpwnam(const char *name)
{
    (void)name;
    return &g_passwd;
}

/* There is no passwd file to enumerate. */
struct passwd *getpwent(void)
{
    return NULL;
}

void setpwent(void)
{
}

void endpwent(void)
{
}

/* The single account may only set its own uid/root. */
int setuid(uid_t uid)
{
    if (uid == 0 || uid == getuid())
        return 0;
    errno = EPERM;
    return -1;
}

int setgid(gid_t gid)
{
    if (gid == 0 || gid == getgid())
        return 0;
    errno = EPERM;
    return -1;
}

int setreuid(uid_t real, uid_t effective)
{
    return setuid(real) == 0 && setuid(effective) == 0 ? 0 : -1;
}

int setregid(gid_t real, gid_t effective)
{
    return setgid(real) == 0 && setgid(effective) == 0 ? 0 : -1;
}

/* Zhaoxu has no group database to enumerate. */
struct group *getgrent(void)
{
    return NULL;
}

void setgrent(void)
{
}

void endgrent(void)
{
}

/* Login name used by nano's status line. */
char *getlogin(void)
{
    return g_user;
}

/* Host name reported by uname-style callers. */
int gethostname(char *name, size_t length)
{
    static const char host[] = "zhaoxu";
    if (!name || length < sizeof(host))
        return -1;
    memcpy(name, host, sizeof(host));
    return 0;
}

/* Program name used by gnulib and nano's error paths. */
char *getprogname(void)
{
    return (char *)"nano";
}
