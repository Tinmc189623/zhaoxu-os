#ifndef ZHAOXU_POSIX_ABI_H
#define ZHAOXU_POSIX_ABI_H

#include <types.h>

/* Syscalls 86 and up return -errno.  These declarations are the task,
 * clock and information calls the C library binds against. */
struct process;

long sys_gettid(void);
long sys_set_tid_address(struct process *process, u64 pointer);
void sys_exit_group(struct process *process, int status)
    __attribute__((noreturn));
long sys_set_robust_list(struct process *process, u64 head, u64 length);
long sys_prlimit64(struct process *process, u64 pid, u64 resource,
                   u64 new_limit, u64 old_limit);
long sys_getrlimit(struct process *process, u64 resource, u64 limit_pointer);

long sys_getrandom(struct process *process, u64 buffer, u64 length, u64 flags);
long sys_clock_gettime(struct process *process, u64 clock_id, u64 pointer);

long sys_uname(struct process *process, u64 pointer);
long sys_access(struct process *process, u64 path_pointer, u64 mode);
long sys_readlink(struct process *process, u64 path_pointer, u64 buffer,
                  u64 capacity);
long sys_last_error(void);

struct interrupt_frame;
long sys_rt_sigaction(struct process *process, u64 signal_number,
                      u64 action_pointer, u64 old_pointer, u64 sigsetsize);
long sys_rt_sigprocmask(struct process *process, u64 how, u64 set_pointer,
                        u64 old_pointer, u64 sigsetsize);
long sys_rt_sigreturn(struct process *process, struct interrupt_frame *frame);
long sys_kill(struct process *process, u64 pid, u64 signal_number);
long sys_tgkill(struct process *process, u64 tgid, u64 tid,
                u64 signal_number);

#endif
