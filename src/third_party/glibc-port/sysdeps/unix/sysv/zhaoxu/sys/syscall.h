/* The generic sysdeps/unix/sysv tree has no <sys/syscall.h> of its own, and
 * glibc's cancellation macros read the __NR_* numbers directly.  Route them
 * to the port's bits/syscall.h, which mirrors the kernel table. */
#ifndef _ZHAOXU_SYS_SYSCALL_H
#define _ZHAOXU_SYS_SYSCALL_H 1

#include <bits/syscall.h>

#endif
