#ifndef ZHAOXU_SYS_RESOURCE_H
#define ZHAOXU_SYS_RESOURCE_H

/* Minimal rlimit surface.  ZOS has no per-process rlimits yet; the values
 * exist so gnulib and nano compile and getdtablesize() has a stable answer. */
#include <sys/types.h>

typedef unsigned long rlim_t;

#define RLIM_INFINITY (~(rlim_t)0)
#define RLIMIT_NOFILE 7

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

int getrlimit(int resource, struct rlimit *limit);
int setrlimit(int resource, const struct rlimit *limit);

#endif
