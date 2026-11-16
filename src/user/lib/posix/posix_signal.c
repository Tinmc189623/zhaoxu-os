/* Zhaoxu OS - newlib signal glue.
 *
 * Handlers are recorded and signal sets are real, but the kernel does not
 * deliver asynchronous signals yet; raise() is provided by gnulib/nano.
 * newlib defines the sigset helpers as macros, so libgloss exports real
 * symbols for the applications that take their address or link them. */
#include "posix_glue.h"

#include <errno.h>
#include <signal.h>
#include <string.h>

#undef sigemptyset
#undef sigfillset
#undef sigaddset
#undef sigdelset
#undef sigismember

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

/* Install or query a signal handler. */
int sigaction(int signal, const struct sigaction *action,
              struct sigaction *old_action)
{
    if (signal < 0 || signal >= NSIG)
        return -1;
    if (old_action)
        *old_action = g_signal_actions[signal];
    if (action)
        g_signal_actions[signal] = *action;
    return 0;
}

/* Signal masks are accepted and remembered as empty. */
int sigprocmask(int how, const sigset_t *set, sigset_t *old_set)
{
    (void)how;
    (void)set;
    if (old_set)
        sigemptyset(old_set);
    return 0;
}
