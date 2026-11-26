/* Zhaoxu OS - futex regression (SYS_FUTEX, 92).
 *
 * A forked child blocks on a word inside a shared-memory page that it maps
 * again after fork (so the two processes share physical pages but not virtual
 * addresses); the parent flips the word and wakes it.  Also covers the
 * value-changed (-EAGAIN) and timeout (-ETIMEDOUT) paths without a waiter.
 */
#include "userlib.h"

#define FUTEX_TIMEOUT_MS 50

static int fail(const char *reason)
{
    u_puts("[futex] FAIL: ");
    u_puts(reason);
    u_puts("\n");
    return 1;
}

/* Wait on `word` for `milliseconds`, returning the raw syscall result. */
static long wait_ms(volatile int *word, int expected, long milliseconds)
{
    struct zos_timespec span;
    span.seconds = milliseconds / 1000;
    span.nanoseconds = (milliseconds % 1000) * 1000000L;
    return sys_futex(word, FUTEX_WAIT, expected, &span, 0);
}

int user_main(long *stack)
{
    (void)stack;

    long id = sys_shm_create(4096);
    if (id <= 0)
        return fail("shm-create");
    volatile int *shared = (volatile int *)sys_shm_map(id);
    if ((long)shared <= 0)
        return fail("shm-map");

    /* Two words: [0] is the futex, [1] is the child-ready handshake. */
    shared[0] = 0;
    shared[1] = 0;

    long pid = sys_fork();
    if (pid < 0)
        return fail("fork");
    if (pid == 0) {
        /* Child: map the same object again, which may land at a different
         * virtual address, then announce readiness and sleep on the word. */
        volatile int *child_word = (volatile int *)sys_shm_map(id);
        if ((long)child_word <= 0)
            return fail("child-map");
        child_word[1] = 1;
        long status = wait_ms(&child_word[0], 0, 3000);
        int seen = child_word[0];
        u_puts(status == 0 && seen == 1 ? "[futex] child woke PASS\n"
                                        : "[futex] child woke FAIL\n");
        return (status == 0 && seen == 1) ? 0 : 1;
    }

    /* Parent: wait for the child to block, then set the word and wake. */
    for (int i = 0; i < 200 && shared[1] != 1; i++)
        sys_sleep(10);
    if (shared[1] != 1)
        return fail("child-ready");
    sys_sleep(50);            /* give the child time to enter the wait */
    shared[0] = 1;
    long woken = sys_futex(&shared[0], FUTEX_WAKE, 1, 0, 0);
    if (woken != 1)
        return fail("wake-count");
    u_puts("[futex] wake PASS\n");

    int status = -1;
    if (sys_wait(pid, &status) < 0 || status != 0)
        return fail("child-status");

    /* Value mismatch must not block and reports -EAGAIN. */
    shared[0] = 5;
    if (wait_ms(&shared[0], 0, FUTEX_TIMEOUT_MS) != -11)
        return fail("eagain");
    u_puts("[futex] eagain PASS\n");

    /* A matching value with a timeout blocks, then reports -ETIMEDOUT. */
    shared[0] = 0;
    if (wait_ms(&shared[0], 0, FUTEX_TIMEOUT_MS) != -110)
        return fail("timeout");
    u_puts("[futex] timeout PASS\n");

    if (sys_futex(&shared[0], FUTEX_WAKE, 1, 0, 0) != 0)
        return fail("wake-empty");
    if (sys_futex(&shared[0], FUTEX_WAKE_BITSET, 1, 0, 0xff) != 0)
        return fail("wake-bitset");
    u_puts("[futex] no-waiter PASS\n");

    if (sys_futex((volatile int *)((unsigned char *)shared + 1),
                  FUTEX_WAKE, 1, 0, 0) != -22)
        return fail("align");
    if (sys_futex(&shared[0], 99, 0, 0, 0) != -38)
        return fail("enosys");
    u_puts("[futex] reject PASS\n");

    sys_shm_unmap(id);
    sys_shm_close(id);
    u_puts("[futex] FUTEX_TEST exit 0\n");
    return 0;
}
