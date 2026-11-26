/*
 * Zhaoxu OS - POSIX signals (93-97) and fault delivery.
 *
 * Actions, masks and pending bits are per process.  A delivered signal builds
 * a Linux x86_64 rt_sigframe below the interrupted user stack: pretcode, a
 * 968-byte ucontext_t, siginfo and the FXSAVE area.  Handlers must carry
 * SA_RESTORER, because the frame's return path is the restorer's address.
 */
#include "process_internal.h"

#include <idt.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <thread.h>
#include <types.h>
#include <zsignal.h>

#define EFAULT_CODE 14
#define EINVAL_CODE 22
#define ESRCH_CODE  3

#define SIGSET_BYTES 8
#define SIGMASK_ALL  0xffffffffffffffffULL

/* One bit in the process signal masks; signal n uses bit n-1. */
static u64 sig_bit(int sig)
{
    return 1ULL << (sig - 1);
}

/* Signals numbered 1..64 are valid on this ABI. */
static int sig_valid(int sig)
{
    return sig >= 1 && sig <= ZOS_NSIG;
}

/* Default dispositions that ignore the signal instead of terminating. */
static int sig_default_ignored(int sig)
{
    switch (sig) {
    case SIGCHLD:
    case SIGCONT:
    case SIGURG:
    case SIGWINCH:
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
        return 1;
    default:
        return 0;
    }
}

/* Build the handler frame on the user stack and redirect the return frame. */
static int sig_setup_frame(struct process *process,
                           struct interrupt_frame *frame, int sig,
                           const struct zos_sigaction *action)
{
    u64 sp = (frame->rsp - sizeof(struct zos_sigframe)) & ~0xFULL;
    if (user_range_ok(process, sp, sizeof(struct zos_sigframe)) != 1)
        return -1;

    struct zos_sigframe *out = kmalloc(sizeof(*out));
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

    out->pretcode = action->restorer;
    struct zos_sigcontext *mc = &out->uc.mcontext;
    mc->r8 = frame->r8;
    mc->r9 = frame->r9;
    mc->r10 = frame->r10;
    mc->r11 = frame->r11;
    mc->r12 = frame->r12;
    mc->r13 = frame->r13;
    mc->r14 = frame->r14;
    mc->r15 = frame->r15;
    mc->rdi = frame->rdi;
    mc->rsi = frame->rsi;
    mc->rbp = frame->rbp;
    mc->rbx = frame->rbx;
    mc->rdx = frame->rdx;
    mc->rax = frame->rax;
    mc->rcx = frame->rcx;
    mc->rsp = frame->rsp;
    mc->rip = frame->rip;
    mc->rflags = frame->rflags;
    mc->cs = (u16)frame->cs;
    mc->err = frame->error;
    mc->trapno = frame->vector;
    memcpy(out->uc.uc_sigmask, &process->sig_blocked, SIGSET_BYTES);

    /* siginfo: si_signo, si_errno, si_code = SI_USER (0) for kill(). */
    ((u32 *)out->info)[0] = (u32)sig;
    ((u32 *)out->info)[1] = 0;
    ((i32 *)out->info)[2] = 0;

    int status = process_user_write(process, sp, out, sizeof(*out));
    kfree(out);
    if (status != 0)
        return -1;

    frame->rip = action->handler;
    frame->rsp = sp;
    frame->rdi = (u64)sig;
    frame->rsi = sp + 8 + sizeof(struct zos_ucontext);
    frame->rdx = sp + 8;
    frame->rax = 0;
    frame->rflags |= 0x200;

    process->sig_blocked |= action->mask;
    if (!(action->flags & SA_NODEFER))
        process->sig_blocked |= sig_bit(sig);
    if (action->flags & SA_RESETHAND)
        process->sig_actions[sig - 1].handler = SIG_DFL;
    return 0;
}

int signal_deliver(struct process *process, struct interrupt_frame *frame)
{
    if (!process || !frame || (frame->cs & 3) != 3)
        return 0;
    u64 ready = process->sig_pending & ~process->sig_blocked;
    if (!ready)
        return 0;

    for (int sig = 1; sig <= ZOS_NSIG; sig++) {
        u64 bit = sig_bit(sig);
        if (!(ready & bit))
            continue;

        struct zos_sigaction action = process->sig_actions[sig - 1];
        if (sig == SIGKILL || sig == SIGSTOP || action.handler == SIG_DFL) {
            process->sig_pending &= ~bit;
            if (sig != SIGKILL && sig != SIGSTOP &&
                sig_default_ignored(sig))
                continue;
            process_exit_current(128 + sig);
        }
        if (action.handler == SIG_IGN) {
            process->sig_pending &= ~bit;
            continue;
        }
        /* Without a restorer the handler cannot return; leave it pending. */
        if (!(action.flags & SA_RESTORER) || !action.restorer)
            return 0;
        if (sig_setup_frame(process, frame, sig, &action) != 0)
            return 0;
        process->sig_pending &= ~bit;
        return 1;
    }
    return 0;
}

/* Signal raised by a CPU exception vector. */
static int fault_signal(u64 vector)
{
    switch (vector) {
    case 0:
        return SIGFPE;
    case 1:
    case 3:
        return SIGTRAP;
    case 4:
    case 6:
        return SIGILL;
    case 11:
    case 12:
    case 13:
    case 14:
    case 17:
        return SIGSEGV;
    case 16:
    case 19:
        return SIGFPE;
    default:
        return 0;
    }
}

int signal_deliver_fault(struct process *process, struct interrupt_frame *frame)
{
    if (!process || !frame)
        return 0;
    int sig = fault_signal(frame->vector);
    if (!sig)
        return 0;
    u64 bit = sig_bit(sig);
    /* A synchronous fault reaches the handler even while masked. */
    process->sig_blocked &= ~bit;
    process->sig_pending |= bit;
    return signal_deliver(process, frame);
}

void signal_wake(struct process *process)
{
    sched_wake_process(process);
}

long sys_rt_sigaction(struct process *process, u64 signal_number,
                      u64 action_pointer, u64 old_pointer, u64 sigsetsize)
{
    if (!sig_valid((int)signal_number) || sigsetsize != SIGSET_BYTES)
        return -EINVAL_CODE;

    if (old_pointer) {
        if (user_range_ok(process, old_pointer,
                          sizeof(struct zos_sigaction)) != 1)
            return -EFAULT_CODE;
        struct zos_sigaction old =
            process->sig_actions[signal_number - 1];
        if (process_user_write(process, old_pointer, &old, sizeof(old)) != 0)
            return -EFAULT_CODE;
    }

    if (action_pointer) {
        if (user_range_ok(process, action_pointer,
                          sizeof(struct zos_sigaction)) != 1)
            return -EFAULT_CODE;
        struct zos_sigaction action;
        memcpy(&action, (const void *)(uintptr_t)action_pointer,
               sizeof(action));
        if ((signal_number == SIGKILL || signal_number == SIGSTOP) &&
            action.handler != SIG_DFL)
            return -EINVAL_CODE;
        action.mask &= ~(sig_bit(SIGKILL) | sig_bit(SIGSTOP));
        process->sig_actions[signal_number - 1] = action;
    }
    return 0;
}

long sys_rt_sigprocmask(struct process *process, u64 how, u64 set_pointer,
                        u64 old_pointer, u64 sigsetsize)
{
    if (sigsetsize != SIGSET_BYTES)
        return -EINVAL_CODE;

    if (old_pointer) {
        if (user_range_ok(process, old_pointer, SIGSET_BYTES) != 1)
            return -EFAULT_CODE;
        if (process_user_write(process, old_pointer, &process->sig_blocked,
                               SIGSET_BYTES) != 0)
            return -EFAULT_CODE;
    }
    if (!set_pointer)
        return 0;
    if (user_range_ok(process, set_pointer, SIGSET_BYTES) != 1)
        return -EFAULT_CODE;

    u64 set;
    memcpy(&set, (const void *)(uintptr_t)set_pointer, sizeof(set));
    set &= ~(sig_bit(SIGKILL) | sig_bit(SIGSTOP));
    switch (how) {
    case 0:                    /* SIG_BLOCK */
        process->sig_blocked |= set;
        break;
    case 1:                    /* SIG_UNBLOCK */
        process->sig_blocked &= ~set;
        break;
    case 2:                    /* SIG_SETMASK */
        process->sig_blocked = set;
        break;
    default:
        return -EINVAL_CODE;
    }
    return 0;
}

/* rt_sigreturn: restore the frame saved before the handler.  The restorer
 * reached the gate with `ret`, so rsp points just above pretcode. */
long sys_rt_sigreturn(struct process *process, struct interrupt_frame *frame)
{
    u64 sp = frame->rsp - sizeof(u64);
    if (user_range_ok(process, sp, sizeof(struct zos_sigframe)) != 1)
        return -EFAULT_CODE;

    struct zos_sigframe *saved = kmalloc(sizeof(*saved));
    if (!saved)
        return -EFAULT_CODE;
    memcpy(saved, (const void *)(uintptr_t)sp, sizeof(*saved));

    struct zos_sigcontext *mc = &saved->uc.mcontext;
    frame->r8 = mc->r8;
    frame->r9 = mc->r9;
    frame->r10 = mc->r10;
    frame->r11 = mc->r11;
    frame->r12 = mc->r12;
    frame->r13 = mc->r13;
    frame->r14 = mc->r14;
    frame->r15 = mc->r15;
    frame->rdi = mc->rdi;
    frame->rsi = mc->rsi;
    frame->rbp = mc->rbp;
    frame->rbx = mc->rbx;
    frame->rdx = mc->rdx;
    frame->rcx = mc->rcx;
    frame->rsp = mc->rsp;
    frame->rip = mc->rip;
    frame->rflags = mc->rflags;
    frame->rax = mc->rax;
    memcpy(&process->sig_blocked, saved->uc.uc_sigmask, SIGSET_BYTES);
    process->sig_blocked &= SIGMASK_ALL;
    long result = (long)mc->rax;
    kfree(saved);
    return result;
}

long sys_kill(struct process *process, u64 pid, u64 signal_number)
{
    int sig = (int)signal_number;
    if (sig < 0 || sig > ZOS_NSIG)
        return -EINVAL_CODE;

    struct process *target = process;
    if ((i64)pid > 0) {
        target = process_lookup(pid);
        if (!target)
            return -ESRCH_CODE;
    } else if ((i64)pid < -1) {
        return -EINVAL_CODE;
    }
    if (sig == 0)
        return 0;

    target->sig_pending |= sig_bit(sig);
    if (target != process)
        signal_wake(target);
    return 0;
}

long sys_tgkill(struct process *process, u64 tgid, u64 tid,
                u64 signal_number)
{
    int sig = (int)signal_number;
    if (sig < 0 || sig > ZOS_NSIG)
        return -EINVAL_CODE;
    if ((u64)tgid != process->pid || tid != sched_current()->tid)
        return -ESRCH_CODE;
    if (sig == 0)
        return 0;
    process->sig_pending |= sig_bit(sig);
    return 0;
}
