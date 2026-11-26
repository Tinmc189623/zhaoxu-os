#ifndef ZHAOXU_ZSIGNAL_H
#define ZHAOXU_ZSIGNAL_H

#include <types.h>

/* Signal numbers follow Linux x86_64. */
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define SIGURG   23
#define SIGXCPU  24
#define SIGXFSZ  25
#define SIGVTALRM 26
#define SIGWINCH 28

#define SIG_DFL 0ULL
#define SIG_IGN 1ULL

#define SA_SIGINFO   0x00000004ULL
#define SA_RESTORER  0x04000000ULL
#define SA_NODEFER   0x40000000ULL
#define SA_RESETHAND 0x80000000ULL

#define ZOS_NSIG 64

/* Kernel-side sigaction record shared with rt_sigaction (32 bytes). */
struct zos_sigaction {
    u64 handler;
    u64 flags;
    u64 restorer;
    u64 mask;
};

/* x86_64 sigcontext as glibc expects it inside ucontext_t (256 bytes). */
struct zos_sigcontext {
    u64 r8, r9, r10, r11, r12, r13, r14, r15;
    u64 rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, rflags;
    u16 cs, gs, fs, pad0;
    u64 err, trapno, oldmask, cr2;
    u64 fpstate;
    u64 reserved[8];
};

/* glibc-visible ucontext_t: mcontext at 40, sigmask at 296, 968 bytes. */
struct zos_ucontext {
    u64 uc_flags;
    u64 uc_link;
    u64 ss_sp;
    u64 ss_flags;
    u64 ss_size;
    struct zos_sigcontext mcontext;
    u8 uc_sigmask[128];
    u8 fpregs_mem[544];
};

/* rt_sigframe: pretcode, ucontext, siginfo, FXSAVE area (1104 + 512). */
struct zos_sigframe {
    u64 pretcode;
    struct zos_ucontext uc;
    u8 info[128];
    u8 fpstate[512];
};

_Static_assert(__builtin_offsetof(struct zos_ucontext, mcontext) == 40,
               "ucontext mcontext offset");
_Static_assert(__builtin_offsetof(struct zos_ucontext, uc_sigmask) == 296,
               "ucontext sigmask offset");
_Static_assert(sizeof(struct zos_ucontext) == 968, "ucontext size");
_Static_assert(__builtin_offsetof(struct zos_sigframe, fpstate) == 1104,
               "sigframe fpstate offset");

struct process;
struct interrupt_frame;

/* Deliver one pending signal by rewriting the return frame; returns 1 when a
 * handler was entered, 0 when nothing was pending. */
int signal_deliver(struct process *process, struct interrupt_frame *frame);
/* Map a CPU exception to a signal and deliver it; 1 when handled. */
int signal_deliver_fault(struct process *process, struct interrupt_frame *frame);
/* Wake a blocked thread of `process` so it can notice a new pending signal. */
void signal_wake(struct process *process);

#endif
