/*
 * Zhaoxu OS - process, identity, limit and system-information calls.
 *
 * Answers come from real kernel counters (uptime, CPU count) or from the
 * files the kernel exposes under /proc and /etc.  A single-user, single
 * scheduling class kernel has no process groups, credential sets or rlimits,
 * so those report the gap rather than pretending to succeed.
 */
#include "posix_glue.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Process groups and sessions do not exist in the scheduler. */
int setsid(void)
{
    errno = ENOSYS;
    return -1;
}

/* Every process is its own group until the kernel grows one; a shell can
 * start when it sees the pid it expects, and foreground arbitration is a
 * no-op instead of an error. */
int setpgid(pid_t pid, pid_t group)
{
    (void)pid;
    (void)group;
    return 0;
}

pid_t getpgrp(void)
{
    return getpid();
}

pid_t getsid(pid_t pid)
{
    (void)pid;
    errno = ENOSYS;
    return -1;
}

/* One static priority: every query reports the default. */
int nice(int increment)
{
    (void)increment;
    return 0;
}

int getpriority(int which, int who)
{
    (void)which;
    (void)who;
    return 0;
}

int setpriority(int which, int who, int value)
{
    (void)which;
    (void)who;
    (void)value;
    errno = EPERM;
    return -1;
}

int sched_get_priority_max(int policy)
{
    (void)policy;
    return 0;
}

int sched_get_priority_min(int policy)
{
    (void)policy;
    return 0;
}

int sched_getscheduler(pid_t pid)
{
    (void)pid;
    return SCHED_OTHER;
}

int sched_getparam(pid_t pid, struct sched_param *param)
{
    (void)pid;
    if (!param) {
        errno = EINVAL;
        return -1;
    }
    param->sched_priority = 0;
    return 0;
}

int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param)
{
    (void)pid;
    (void)policy;
    (void)param;
    errno = EPERM;
    return -1;
}

/* A vfork is the kernel's copy-on-write fork. */
pid_t vfork(void)
{
    return fork();
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

/* The kernel reports no per-child accounting; the structure stays zeroed. */
pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage)
{
    if (usage)
        memset(usage, 0, sizeof(*usage));
    return waitpid(pid, status, options);
}

/* wait3 is wait4 for any child; kept for tcsh and old BSD code. */
pid_t wait3(int *status, int options, struct rusage *usage)
{
    return wait4(-1, status, options, usage);
}

/* Block forever: no signal is delivered to interrupt a pause yet. */
int pause(void)
{
    for (;;)
        zos_syscall3(ZOS_SLEEP, 1000000, 0, 0);
}

int prlimit(pid_t pid, int resource, const struct rlimit *new_limit,
            struct rlimit *old_limit)
{
    (void)pid;
    (void)resource;
    (void)new_limit;
    (void)old_limit;
    errno = ENOSYS;
    return -1;
}

int personality(unsigned long persona)
{
    (void)persona;
    errno = ENOSYS;
    return -1;
}

int sethostname(const char *name, size_t length)
{
    (void)name;
    (void)length;
    errno = ENOSYS;
    return -1;
}

int reboot(int command)
{
    (void)command;
    return (int)zos_syscall3(ZOS_REBOOT, 0, 0, 0);
}

/* Read one KEY="value" pair from a file the kernel exposes. */
static int read_value(const char *path, const char *key, char *out,
                      size_t out_size)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return -1;
    size_t key_length = strlen(key);
    char line[256];
    int found = -1;
    while (fgets(line, (int)sizeof(line), file)) {
        if (strncmp(line, key, key_length) != 0 || line[key_length] != '=')
            continue;
        char *value = line + key_length + 1;
        char *end = value + strlen(value);
        while (end > value && (end[-1] == '\n' || end[-1] == '\r'))
            end--;
        *end = '\0';
        if (*value == '"')
            value++;
        if (end > value && end[-1] == '"')
            end[-1] = '\0';
        snprintf(out, out_size, "%s", value);
        found = 0;
        break;
    }
    fclose(file);
    return found;
}

int uname(struct utsname *name)
{
    if (!name) {
        errno = EINVAL;
        return -1;
    }
    memset(name, 0, sizeof(*name));
    snprintf(name->sysname, sizeof(name->sysname), "%s", "Zhaoxu");
    snprintf(name->nodename, sizeof(name->nodename), "%s", "zhaoxu");
    if (read_value("/etc/os-release", "PRETTY_NAME", name->version,
                   sizeof(name->version)) != 0)
        snprintf(name->version, sizeof(name->version), "%s", "Zhaoxu OS");
    if (read_value("/etc/os-release", "KERNEL_VERSION", name->release,
                   sizeof(name->release)) != 0)
        snprintf(name->release, sizeof(name->release), "%s", "Yuanshuo");
    snprintf(name->machine, sizeof(name->machine), "%s", "x86_64");
    snprintf(name->domainname, sizeof(name->domainname), "%s", "zhaoxu");
    return 0;
}

/* Values that follow from the kernel's fixed page size and timer. */
long sysconf(int name)
{
    switch (name) {
    case _SC_PAGESIZE:
        return 4096;
    case _SC_CLK_TCK:
        return 1000;                   /* SYS_UPTIME counts milliseconds */
    case _SC_NPROCESSORS_ONLN:
    case _SC_NPROCESSORS_CONF: {
        long cpus = zos_syscall3(ZOS_CPUS, 0, 0, 0);
        return cpus > 0 ? cpus : 1;
    }
    case _SC_OPEN_MAX:
        return 256;
    case _SC_ARG_MAX:
        return 16384;
    case _SC_CHILD_MAX:
        return 64;
    case _SC_VERSION:
        return 200809L;
    case _SC_LINE_MAX:
        return 2048;
    default:
        errno = EINVAL;
        return -1;
    }
}

long pathconf(const char *path, int name)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    switch (name) {
    case _PC_NAME_MAX:
    case _PC_PATH_MAX:
        return 255;
    case _PC_LINK_MAX:
        return 1;
    case _PC_PIPE_BUF:
        return 4096;
    case _PC_CHOWN_RESTRICTED:
        return 1;
    case _PC_NO_TRUNC:
        return 1;
    default:
        errno = EINVAL;
        return -1;
    }
}

long fpathconf(int fd, int name)
{
    (void)fd;
    return pathconf("/", name);
}

size_t confstr(int name, char *buffer, size_t length)
{
    const char *value;
#ifndef _CS_PATH
#define _CS_PATH 0
#endif
    switch (name) {
    case _CS_PATH:
        value = "/bin:/usr/bin";
        break;
    default:
        errno = EINVAL;
        return 0;
    }
    size_t needed = strlen(value) + 1;
    if (buffer && length > 0) {
        snprintf(buffer, length, "%s", value);
    }
    return needed;
}

/* No supplementary groups exist; the primary gid is the whole list. */
int getgroups(int size, gid_t groups[])
{
    (void)size;
    (void)groups;
    return 0;
}

int initgroups(const char *user, gid_t group)
{
    (void)user;
    (void)group;
    errno = ENOSYS;
    return -1;
}

int getgrouplist(const char *user, gid_t group, gid_t *groups, int *count)
{
    (void)user;
    if (!count) {
        errno = EINVAL;
        return -1;
    }
    if (groups && *count > 0)
        groups[0] = group;
    *count = 1;
    return 1;
}

/* Parse one "Key:     value kB" line from /proc/meminfo. */
static unsigned long meminfo_kb(const char *buffer, const char *key)
{
    const char *line = strstr(buffer, key);
    if (!line)
        return 0;
    line += strlen(key);
    while (*line == ' ' || *line == ':')
        line++;
    return strtoul(line, NULL, 10);
}

int sysinfo(struct sysinfo *info)
{
    if (!info) {
        errno = EINVAL;
        return -1;
    }
    memset(info, 0, sizeof(*info));
    long uptime = zos_syscall3(ZOS_UPTIME, 0, 0, 0);
    if (uptime > 0)
        info->uptime = uptime / 1000;

    char buffer[2048];
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file)
        return -1;
    size_t total = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[total] = '\0';

    unsigned long bytes_per_unit = 1024;
    info->totalram = meminfo_kb(buffer, "MemTotal") * bytes_per_unit;
    info->freeram = meminfo_kb(buffer, "MemFree") * bytes_per_unit;
    info->sharedram = meminfo_kb(buffer, "Shmem") * bytes_per_unit;
    info->bufferram = meminfo_kb(buffer, "Buffers") * bytes_per_unit;
    info->totalswap = meminfo_kb(buffer, "SwapTotal") * bytes_per_unit;
    info->freeswap = meminfo_kb(buffer, "SwapFree") * bytes_per_unit;
    info->mem_unit = 1;
    long cpus = zos_syscall3(ZOS_CPUS, 0, 0, 0);
    info->procs = (unsigned short)(cpus > 0 ? cpus : 1);
    return 0;
}

/* No raw syscall ABI is exported; named stubs cover the documented table. */
long syscall(long number, ...)
{
    (void)number;
    errno = ENOSYS;
    return -1;
}
