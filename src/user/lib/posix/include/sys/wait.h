#ifndef ZOS_SYS_WAIT_WRAPPER_H
#define ZOS_SYS_WAIT_WRAPPER_H

#include_next <sys/wait.h>

#include <sys/resource.h>
#include <sys/types.h>

/* BSD-style wait variants used by tcsh and older user-space code. */
pid_t wait3(int *status, int options, struct rusage *usage);
pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage);

/* Zhaoxu never reports a continued child, so WCONTINUED is absent. */
#ifndef WIFCONTINUED
#define WIFCONTINUED(status) 0
#endif

#ifndef WCONTINUED
#define WCONTINUED 0
#endif

#endif
