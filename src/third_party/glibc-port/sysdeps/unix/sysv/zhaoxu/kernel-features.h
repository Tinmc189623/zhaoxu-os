/* Kernel feature assumptions for the Zhaoxu port.
 *
 * glibc's Linux port lists the syscalls each supported kernel version
 * provides.  The Zhaoxu kernel implements exactly the calls in
 * bits/syscall.h, so every assumption is defined to 0: shared code takes
 * its fallback paths instead of assuming a Linux syscall exists.  All
 * names must be defined because glibc builds with -Werror=undef.
 */
#ifndef _ZHAOXU_KERNEL_FEATURES_H
#define _ZHAOXU_KERNEL_FEATURES_H 1

#define __ASSUME_ACCEPT4_SYSCALL 0
#define __ASSUME_ACCEPT_SYSCALL 0
#define __ASSUME_ALIGNED_REGISTER_PAIRS 0
#define __ASSUME_BIND_SYSCALL 0
#define __ASSUME_CLONE3 0
#define __ASSUME_CLONE_BACKWARDS 0
#define __ASSUME_CLONE_BACKWARDS2 0
#define __ASSUME_CLONE_BACKWARDS3 0
#define __ASSUME_CLONE_DEFAULT 0
#define __ASSUME_CLOSE_RANGE 0
#define __ASSUME_CONNECT_SYSCALL 0
#define __ASSUME_DIRECT_SYSVIPC_SYSCALLS 0
#define __ASSUME_EXECVEAT 0
#define __ASSUME_FACCESSAT2 0
#define __ASSUME_FADVISE64_64_6ARG 0
#define __ASSUME_FADVISE64_AS_64_64 0
#define __ASSUME_FCHMODAT2 0
#define __ASSUME_FUTEX_LOCK_PI2 0
#define __ASSUME_GETPEERNAME_SYSCALL 0
#define __ASSUME_GETSOCKNAME_SYSCALL 0
#define __ASSUME_GETSOCKOPT_SYSCALL 0
#define __ASSUME_IN_NONBLOCK 0
#define __ASSUME_LISTEN_SYSCALL 0
#define __ASSUME_MLOCK2 0
#define __ASSUME_MSEAL 0
#define __ASSUME_PIDFD_GET_INFO 0
#define __ASSUME_PREADV 0
#define __ASSUME_PRW_DUMMY_ARG 0
#define __ASSUME_PSELECT 0
#define __ASSUME_PWRITEV 0
#define __ASSUME_RECVFROM_SYSCALL 0
#define __ASSUME_RECVMMSG_SYSCALL 0
#define __ASSUME_RECVMSG_SYSCALL 0
#define __ASSUME_RECV_SYSCALL 0
#define __ASSUME_RENAMEAT2 0
#define __ASSUME_SENDMMSG 0
#define __ASSUME_SENDMMSG_SYSCALL 0
#define __ASSUME_SENDMSG_SYSCALL 0
#define __ASSUME_SENDTO_SYSCALL 0
#define __ASSUME_SEND_SYSCALL 0
#define __ASSUME_SETSOCKOPT_SYSCALL 0
#define __ASSUME_SET_ROBUST_LIST 0
#define __ASSUME_SHUTDOWN_SYSCALL 0
#define __ASSUME_SOCKETPAIR_SYSCALL 0
#define __ASSUME_SOCKET_SYSCALL 0
#define __ASSUME_STATFS64 0
#define __ASSUME_STATX 0
#define __ASSUME_SYSVIPC_BROKEN_MODE_T 0
#define __ASSUME_SYSVIPC_DEFAULT_IPC_64 0
#define __ASSUME_TERMIOS2 0
#define __ASSUME_TIME64_SYSCALL 0
#define __ASSUME_TIME64_SYSCALLS 0
#define __ASSUME_WAITID_PID0_P_PGID 0
#define __ASSUME_WORDSIZE64_ILP32 0

#endif
