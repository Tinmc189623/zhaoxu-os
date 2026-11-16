/* Zhaoxu OS - newlib resource-limit glue.
 *
 * There are no per-process rlimits yet; these calls report a fixed descriptor
 * budget so nano and gnulib can size their tables. */
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#define ZOS_FD_LIMIT 128

/* Number of descriptors the stdio layer may assume. */
int getdtablesize(void)
{
    return ZOS_FD_LIMIT;
}

/* Report a fixed soft and hard descriptor limit. */
int getrlimit(int resource, struct rlimit *limit)
{
    (void)resource;
    if (!limit)
        return -1;
    limit->rlim_cur = ZOS_FD_LIMIT;
    limit->rlim_max = ZOS_FD_LIMIT;
    return 0;
}

/* Accept limit changes without storing them yet. */
int setrlimit(int resource, const struct rlimit *limit)
{
    (void)resource;
    (void)limit;
    return 0;
}
