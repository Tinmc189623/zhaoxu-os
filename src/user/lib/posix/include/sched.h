#ifndef ZHAOXU_SCHED_H
#define ZHAOXU_SCHED_H
#include <sys/types.h>
#ifndef SCHED_OTHER
#define SCHED_OTHER 0
#endif
#ifndef SCHED_FIFO
#define SCHED_FIFO 1
#endif
#define SCHED_RR 2
#define SCHED_BATCH 3
#define SCHED_IDLE 5
#define SCHED_RESET_ON_FORK 0x40000000

int sched_yield(void);
int sched_setscheduler(int pid, int policy,
                       const struct sched_param *parameters);
int sched_getscheduler(int pid);
int sched_setparam(int pid, const struct sched_param *parameters);
int sched_getparam(int pid, struct sched_param *parameters);
int sched_get_priority_max(int policy);
int sched_get_priority_min(int policy);
int sched_setaffinity(int pid, size_t size, const void *mask);
int sched_getaffinity(int pid, size_t size, void *mask);

#endif
