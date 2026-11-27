/* Zhaoxu OS - newlib signal glue.
 *
 * newlib's sigset_t is one 64-bit word and its sigaction layout differs from
 * the kernel's Linux-shaped record, so each call converts and forwards to
 * SYS_RT_SIGACTION / SYS_RT_SIGPROCMASK.  Handlers return through a small
 * restorer that issues SYS_RT_SIGRETURN. */
#include "posix_glue.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember

/* Kernel-side record shared with sys_rt_sigaction (32 bytes). */
struct zos_kernel_sigaction {
    unsigned long handler;
    unsigned long flags;
    unsigned long restorer;
    unsigned long mask;
};

#define ZOS_SA_RESTORER 0x04000000UL
#define ZOS_SIGSET_BYTES 8

/* Kernel signal bits are (signal - 1); newlib's set helpers use the same. */
static unsigned long mask_to_kernel(const sigset_t *set)
{
    if (!set)
        return 0;
    return (unsigned long)*set & ~((1UL << (SIGKILL - 1)) |
                                   (1UL << (SIGSTOP - 1)));
}

static void mask_from_kernel(unsigned long mask, sigset_t *set)
{
    if (set)
        *set = (sigset_t)mask;
}

/* Handler return path: build the rt_sigreturn frame the kernel expects. */
__asm__(".text\n"
        ".globl zos_signal_restorer\n"
        ".type zos_signal_restorer, @function\n"
        "zos_signal_restorer:\n"
        "  movl $95, %eax\n"
        "  int $0x80\n"
        "  ud2\n"
        ".size zos_signal_restorer, .-zos_signal_restorer\n");
extern void zos_signal_restorer(void);

static struct sigaction g_signal_actions[NSIG];

/* Clear a signal set. */
int sigemptyset(sigset_t *set)
{
    if (!set)
        return -1;
    memset(set, 0, sizeof(*set));
    return 0;
}

/* Fill a signal set with every signal. */
int sigfillset(sigset_t *set)
{
    if (!set)
        return -1;
    memset(set, 0xFF, sizeof(*set));
    return 0;
}

/* Add one signal to a set. */
int sigaddset(sigset_t *set, int signal)
{
    if (!set || signal < 0 || signal >= NSIG)
        return -1;
    ((unsigned char *)set)[signal / 8] |= (unsigned char)(1u << (signal % 8));
    return 0;
}

/* Remove one signal from a set. */
int sigdelset(sigset_t *set, int signal)
{
    if (!set || signal < 0 || signal >= NSIG)
        return -1;
    ((unsigned char *)set)[signal / 8] &=
        (unsigned char)~(1u << (signal % 8));
    return 0;
}

/* Test set membership. */
int sigismember(const sigset_t *set, int signal)
{
    if (!set || signal < 0 || signal >= NSIG)
        return -1;
    return (((const unsigned char *)set)[signal / 8] &
            (1u << (signal % 8))) != 0;
}

/* Install or query a signal handler, converting to the kernel record. */
int sigaction(int signal, const struct sigaction *action,
              struct sigaction *old_action)
{
    if (signal < 1 || signal >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    if (old_action)
        *old_action = g_signal_actions[signal];
    if (!action)
        return 0;

    struct zos_kernel_sigaction record;
    record.handler = (unsigned long)action->sa_handler;
    record.flags = (unsigned long)action->sa_flags | ZOS_SA_RESTORER;
    record.restorer = (unsigned long)zos_signal_restorer;
    record.mask = mask_to_kernel(&action->sa_mask);

    long result = zos_syscall4(ZOS_RT_SIGACTION, (long)signal,
                               (long)&record, 0, ZOS_SIGSET_BYTES);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    g_signal_actions[signal] = *action;
    return 0;
}

/* newlib numbers its how values in the opposite order to the kernel. */
static int kernel_how(int how)
{
    switch (how) {
    case SIG_BLOCK:
        return 0;
    case SIG_UNBLOCK:
        return 1;
    default:
        return 2;
    }
}

/* Block, unblock or replace the mask held by the kernel. */
int sigprocmask(int how, const sigset_t *set, sigset_t *old_set)
{
    unsigned long new_mask = mask_to_kernel(set);
    unsigned long old_mask = 0;
    long result = zos_syscall4(ZOS_RT_SIGPROCMASK, kernel_how(how),
                               set ? (long)&new_mask : 0,
                               (long)&old_mask, ZOS_SIGSET_BYTES);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    mask_from_kernel(old_mask, old_set);
    return 0;
}

/* Wait for a signal with a temporary mask; the kernel delivers on return. */
int sigsuspend(const sigset_t *mask)
{
    unsigned long wanted = mask_to_kernel(mask);
    unsigned long saved = 0;
    (void)zos_syscall4(ZOS_RT_SIGPROCMASK, 2, (long)&wanted, (long)&saved,
                       ZOS_SIGSET_BYTES);
    zos_syscall3(ZOS_SLEEP, 10, 0, 0);
    (void)zos_syscall4(ZOS_RT_SIGPROCMASK, 2, (long)&saved, 0,
                       ZOS_SIGSET_BYTES);
    errno = EINTR;
    return -1;
}
