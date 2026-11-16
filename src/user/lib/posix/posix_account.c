/* Zhaoxu OS - newlib account and host-name glue.
 *
 * ZOS has a single built-in account; nano only needs it to label its status
 * bar and to satisfy libc's getpwuid/getpwnam paths. */
#include <pwd.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static char g_user[] = "zhaoxu";
static char g_home[] = "/";
static char g_shell[] = "/bin/8sh";

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
