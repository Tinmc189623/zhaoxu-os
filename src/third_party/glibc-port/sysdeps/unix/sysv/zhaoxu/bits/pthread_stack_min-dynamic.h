/* PTHREAD_STACK_MIN for the Zhaoxu port.
 *
 * The port has no sysconf-based dynamic stack size, so the constant from
 * bits/pthread_stack_min.h is always used. */
#ifndef _ZHAOXU_PTHREAD_STACK_MIN_DYNAMIC_H
#define _ZHAOXU_PTHREAD_STACK_MIN_DYNAMIC_H 1

#ifndef PTHREAD_STACK_MIN
# include <bits/pthread_stack_min.h>
#endif

#endif
