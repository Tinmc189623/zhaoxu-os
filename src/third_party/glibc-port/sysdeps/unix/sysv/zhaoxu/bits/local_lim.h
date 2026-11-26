/* POSIX and system limits for the Zhaoxu port.
 *
 * The Linux port reads most of these from <linux/limits.h>; the Zhaoxu
 * kernel exposes the same practical values (4096-byte paths, 255-byte names,
 * 128 KiB argument blocks, 4 KiB pipes), so they are stated here directly.
 */
#ifndef _ZHAOXU_LOCAL_LIM_H
#define _ZHAOXU_LOCAL_LIM_H 1

#define NGROUPS_MAX       65536
#define ARG_MAX           131072
#define LINK_MAX          127
#define MAX_CANON         255
#define MAX_INPUT         255
#define NAME_MAX          255
#define PATH_MAX          4096
#define PIPE_BUF          4096
#define XATTR_NAME_MAX    255
#define XATTR_SIZE_MAX    65536
#define XATTR_LIST_MAX    65536
#define RTSIG_MAX         32

/* Thread-specific data keys per process and destructor iterations. */
#define _POSIX_THREAD_KEYS_MAX 128
#define PTHREAD_KEYS_MAX       1024
#define _POSIX_THREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_DESTRUCTOR_ITERATIONS \
  _POSIX_THREAD_DESTRUCTOR_ITERATIONS

#define _POSIX_THREAD_THREADS_MAX 64
#undef PTHREAD_THREADS_MAX

#define AIO_PRIO_DELTA_MAX 20

/* PTHREAD_STACK_MIN, possibly dynamic, comes from the port's own header. */
#include <bits/pthread_stack_min-dynamic.h>

#define DELAYTIMER_MAX 2147483647
#define TTY_NAME_MAX   32
#define LOGIN_NAME_MAX 256
#define HOST_NAME_MAX  64
#define MQ_PRIO_MAX    32768
#define SEM_VALUE_MAX  2147483647

#endif
