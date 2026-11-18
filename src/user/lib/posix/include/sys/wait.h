#ifndef ZOS_SYS_WAIT_WRAPPER_H
#define ZOS_SYS_WAIT_WRAPPER_H

#include_next <sys/wait.h>

/* Zhaoxu never reports a continued child, so WCONTINUED is absent. */
#ifndef WIFCONTINUED
#define WIFCONTINUED(status) 0
#endif

#ifndef WCONTINUED
#define WCONTINUED 0
#endif

#endif
