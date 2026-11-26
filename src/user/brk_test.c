/* Zhaoxu OS - program-break regression (SYS_BRK, 85).
 *
 * Covers query, growth, write/read, shrink, fresh zero pages after regrow,
 * and the limit rejection that glibc's malloc relies on.
 */
#include "userlib.h"

static int fail(const char *reason)
{
    u_puts("[brk ] FAIL: ");
    u_puts(reason);
    u_puts("\n");
    return 1;
}

int user_main(long *stack)
{
    (void)stack;

    long base = sys_brk(0);
    if (base <= 0)
        return fail("query");

    long grown = sys_brk((void *)(base + 3 * 4096));
    if (grown != base + 3 * 4096)
        return fail("grow");

    unsigned char *heap = (unsigned char *)base;
    heap[0] = 0x11;
    heap[2 * 4096 + 17] = 0x22;
    if (heap[0] != 0x11 || heap[2 * 4096 + 17] != 0x22)
        return fail("write");
    u_puts("[brk ] brk grow PASS\n");

    long shrunk = sys_brk((void *)base);
    if (shrunk != base)
        return fail("shrink");
    u_puts("[brk ] brk shrink PASS\n");

    long regrown = sys_brk((void *)(base + 4096));
    if (regrown != base + 4096)
        return fail("regrow");
    if (heap[0] != 0 || heap[4095] != 0)
        return fail("zero-fill");
    u_puts("[brk ] brk zero-fill PASS\n");

    long far = sys_brk((void *)(base + 300L * 1024 * 1024));
    if (far != regrown)
        return fail("limit");
    u_puts("[brk ] brk limit PASS\n");
    u_puts("[brk ] BRK_TEST exit 0\n");
    return 0;
}
