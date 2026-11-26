/* Zhaoxu OS syscall numbers for glibc's generated stubs.
 *
 * These are Zhaoxu's own numbers (kernel/proc/syscall.c), not Linux's.  Only
 * calls whose semantics match the generic glibc wrapper are listed; anything
 * with a different kernel ABI (stat/readdir/fcntl flags) gets a hand-written
 * wrapper in this port instead of a thin stub.
 */
#ifndef _BITS_SYSCALL_H
#define _BITS_SYSCALL_H 1

#define SYS_read            4
#define SYS_write           1
#define SYS_open            3
#define SYS_close           5
#define SYS_lseek           53
#define SYS_mmap            54
#define SYS_munmap          55
#define SYS_brk             85
#define SYS_mprotect        86
#define SYS_madvise         87
#define SYS_arch_prctl      88
#define SYS_set_tid_address 89
#define SYS_gettid          90
#define SYS_exit_group      91
#define SYS_futex           92
#define SYS_rt_sigaction    93
#define SYS_rt_sigprocmask  94
#define SYS_rt_sigreturn    95
#define SYS_tgkill          96
#define SYS_kill            97
#define SYS_getrandom       98
#define SYS_clock_gettime   99
#define SYS_set_robust_list 100
#define SYS_prlimit64       101
#define SYS_getrlimit       102
#define SYS_uname           103
#define SYS_access          104
#define SYS_readlink        105
#define SYS_last_error      112

#define SYS_exit            0
#define SYS_getpid          2
#define SYS_getppid         78
#define SYS_fork            72
#define SYS_execve          73
#define SYS_pipe            74
#define SYS_dup2            75
#define SYS_ioctl           76
#define SYS_mkdir           82
#define SYS_rmdir           83
#define SYS_rename          84
#define SYS_socket          43
#define SYS_bind            44
#define SYS_listen          45
#define SYS_accept          46
#define SYS_connect         47
#define SYS_sendmsg         48
#define SYS_recvmsg         49
#define SYS_poll            50
#define SYS_socketpair      57
#define SYS_fcntl           56
#define SYS_memfd_create    51
#define SYS_ftruncate       52
#define SYS_eventfd2        61
#define SYS_timerfd_create  62
#define SYS_timerfd_settime 63
#define SYS_timerfd_gettime 64
#define SYS_epoll_create1   65
#define SYS_epoll_ctl       66
#define SYS_epoll_wait      67

#define __NR_read            SYS_read
#define __NR_write           SYS_write
#define __NR_open            SYS_open
#define __NR_close           SYS_close
#define __NR_lseek           SYS_lseek
#define __NR_mmap            SYS_mmap
#define __NR_munmap          SYS_munmap
#define __NR_brk             SYS_brk
#define __NR_mprotect        SYS_mprotect
#define __NR_madvise         SYS_madvise
#define __NR_arch_prctl      SYS_arch_prctl
#define __NR_set_tid_address SYS_set_tid_address
#define __NR_gettid          SYS_gettid
#define __NR_exit_group      SYS_exit_group
#define __NR_futex           SYS_futex
#define __NR_rt_sigaction    SYS_rt_sigaction
#define __NR_rt_sigprocmask  SYS_rt_sigprocmask
#define __NR_rt_sigreturn    SYS_rt_sigreturn
#define __NR_tgkill          SYS_tgkill
#define __NR_kill            SYS_kill
#define __NR_getrandom       SYS_getrandom
#define __NR_clock_gettime   SYS_clock_gettime
#define __NR_set_robust_list SYS_set_robust_list
#define __NR_prlimit64       SYS_prlimit64
#define __NR_getrlimit       SYS_getrlimit
#define __NR_uname           SYS_uname
#define __NR_access          SYS_access
#define __NR_readlink        SYS_readlink
#define __NR_exit            SYS_exit
#define __NR_getpid          SYS_getpid
#define __NR_getppid         SYS_getppid
#define __NR_fork            SYS_fork
#define __NR_execve          SYS_execve
#define __NR_pipe            SYS_pipe
#define __NR_dup2            SYS_dup2
#define __NR_ioctl           SYS_ioctl
#define __NR_mkdir           SYS_mkdir
#define __NR_rmdir           SYS_rmdir
#define __NR_rename          SYS_rename
#define __NR_socket          SYS_socket
#define __NR_bind            SYS_bind
#define __NR_listen          SYS_listen
#define __NR_accept          SYS_accept
#define __NR_connect         SYS_connect
#define __NR_sendmsg         SYS_sendmsg
#define __NR_recvmsg         SYS_recvmsg
#define __NR_poll            SYS_poll
#define __NR_socketpair      SYS_socketpair
#define __NR_fcntl           SYS_fcntl
#define __NR_memfd_create    SYS_memfd_create
#define __NR_ftruncate       SYS_ftruncate
#define __NR_eventfd2        SYS_eventfd2
#define __NR_timerfd_create  SYS_timerfd_create
#define __NR_timerfd_settime SYS_timerfd_settime
#define __NR_timerfd_gettime SYS_timerfd_gettime
#define __NR_epoll_create1   SYS_epoll_create1
#define __NR_epoll_ctl       SYS_epoll_ctl
#define __NR_epoll_wait      SYS_epoll_wait

#endif
