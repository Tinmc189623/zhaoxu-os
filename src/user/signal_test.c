/* Zhaoxu OS - signal regression (SYS_RT_SIGACTION..SYS_KILL, 93-97).
 *
 * Installs a handler with its own restorer, checks delivery, blocking and
 * unblocking, SIG_IGN, the SIGKILL default action, and a SIGSEGV handler
 * that rewrites uc_mcontext.rip to step over the faulting instruction.
 */
#include "userlib.h"

#define SIGKILL 9
#define SIGSEGV 11
#define SIG_IGN 1UL

/* Mirror of the kernel's 256-byte sigcontext so the handler can locate rip. */
struct test_sigcontext {
    unsigned long r8, r9, r10, r11, r12, r13, r14, r15;
    unsigned long rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, rflags;
    unsigned short cs, gs, fs, pad0;
    unsigned long err, trapno, oldmask, cr2;
    unsigned long fpstate;
    unsigned long reserved[8];
};

static volatile int handled;
static volatile int last_sig;
static volatile int after_fault;

/* Returns to user code with rt_sigreturn (95). */
static void sig_restorer(void) __attribute__((naked));
static void sig_restorer(void)
{
    __asm__ volatile("mov $95, %eax\n\tint $0x80");
}

static void handler(int signal_number, void *info, void *ucontext)
{
    handled++;
    last_sig = signal_number;
    (void)info;
    if (signal_number == SIGSEGV) {
        unsigned long *rip = (unsigned long *)
            ((unsigned char *)ucontext + 40 +
             __builtin_offsetof(struct test_sigcontext, rip));
        *rip += 3;      /* movq (%%rax), %%rbx is three bytes */
    }
}

static int fail(const char *reason)
{
    u_puts("[signal] FAIL: ");
    u_puts(reason);
    u_puts("\n");
    return 1;
}

/* Install `fn` (or SIG_IGN) with SA_RESTORER | SA_SIGINFO. */
static long install(int signal_number, unsigned long fn)
{
    struct zos_sigaction action;
    action.handler = fn;
    action.flags = SA_RESTORER | SA_SIGINFO;
    action.restorer = (unsigned long)sig_restorer;
    action.mask = 0;
    return sys_rt_sigaction(signal_number, &action, 0);
}

int user_main(long *stack)
{
    (void)stack;

    if (install(SIGUSR1, (unsigned long)handler) != 0)
        return fail("sigaction");
    if (sys_kill(sys_getpid(), SIGUSR1) != 0)
        return fail("kill");
    if (handled != 1 || last_sig != SIGUSR1)
        return fail("deliver");
    u_puts("[signal] deliver PASS\n");

    unsigned long mask = 1UL << (SIGUSR1 - 1);
    unsigned long old_mask = 0;
    if (sys_rt_sigprocmask(0, &mask, &old_mask) != 0)      /* SIG_BLOCK */
        return fail("block");
    handled = 0;
    if (sys_kill(sys_getpid(), SIGUSR1) != 0)
        return fail("kill-blocked");
    sys_sleep(20);
    if (handled != 0)
        return fail("blocked-delivered");
    if (sys_rt_sigprocmask(1, &mask, 0) != 0)              /* SIG_UNBLOCK */
        return fail("unblock");
    if (handled != 1)
        return fail("pending-after-unblock");
    u_puts("[signal] block/unblock PASS\n");

    handled = 0;
    if (install(SIGUSR2, SIG_IGN) != 0)
        return fail("ignore-action");
    if (sys_kill(sys_getpid(), SIGUSR2) != 0)
        return fail("kill-ignored");
    if (handled != 0)
        return fail("ignored-delivered");
    u_puts("[signal] ignore PASS\n");

    long pid = sys_fork();
    if (pid < 0)
        return fail("fork");
    if (pid == 0) {
        sys_kill(sys_getpid(), SIGKILL);
        sys_sleep(1000);       /* SIGKILL must not return */
        return 2;
    }
    int status = -1;
    if (sys_wait(pid, &status) < 0 || status != 128 + SIGKILL)
        return fail("sigkill-status");
    u_puts("[signal] sigkill PASS\n");

    if (install(SIGSEGV, (unsigned long)handler) != 0)
        return fail("segv-action");
    handled = 0;
    unsigned long value;
    __asm__ volatile("xorl %%eax, %%eax\n\tmovq (%%rax), %%rbx"
                     : "=b"(value) : : "rax", "memory");
    after_fault = 1;
    if (handled != 1 || last_sig != SIGSEGV || after_fault != 1)
        return fail("segv");
    u_puts("[signal] segv PASS\n");

    if (install(SIGUSR1, 0) != 0)      /* back to SIG_DFL */
        return fail("reset");
    u_puts("[signal] SIGNAL_TEST exit 0\n");
    return 0;
}
