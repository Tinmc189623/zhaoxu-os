/* Zhaoxu OS - thread-local storage regression (SYS_ARCH_PRCTL, 88).
 *
 * Sets the FS base, reads and writes through %fs, survives a sleeping
 * syscall (the scheduler switches threads and must restore the MSR), and
 * rejects kernel addresses and GS.
 */
#include "userlib.h"

static unsigned char tls_block[64] __attribute__((aligned(16)));

static int fail(const char *reason)
{
    u_puts("[tls ] FAIL: ");
    u_puts(reason);
    u_puts("\n");
    return 1;
}

/* Read a 64-bit word through the FS segment. */
static unsigned long read_fs(unsigned long offset)
{
    unsigned long value;
    __asm__ volatile("movq %%fs:(%1), %0" : "=r"(value) : "r"(offset));
    return value;
}

int user_main(long *stack)
{
    (void)stack;

    if (sys_arch_prctl(ARCH_SET_FS, tls_block) != 0)
        return fail("set-fs");
    u_puts("[tls ] arch_prctl SET_FS PASS\n");

    *(volatile unsigned long *)tls_block = 0x1122334455667788UL;
    if (read_fs(0) != 0x1122334455667788UL)
        return fail("fs-read");
    u_puts("[tls ] fs read PASS\n");

    *(volatile unsigned long *)(tls_block + 8) = 0xa1b2c3d4e5f60718UL;
    if (read_fs(8) != 0xa1b2c3d4e5f60718UL)
        return fail("fs-write");
    u_puts("[tls ] fs write PASS\n");

    /* Sleeping hands the CPU to another thread; the base must come back. */
    sys_sleep(30);
    if (read_fs(0) != 0x1122334455667788UL ||
        read_fs(8) != 0xa1b2c3d4e5f60718UL)
        return fail("switch");
    u_puts("[tls ] switch restore PASS\n");

    unsigned long got = 0;
    if (sys_arch_prctl(ARCH_GET_FS, &got) != 0 || got != (unsigned long)tls_block)
        return fail("get-fs");
    u_puts("[tls ] arch_prctl GET_FS PASS\n");

    if (sys_arch_prctl(ARCH_SET_FS, (void *)0xffffffff80000000UL) != -22)
        return fail("kernel-base");
    if (sys_arch_prctl(0x1001, tls_block) != -38)
        return fail("gs");
    u_puts("[tls ] reject PASS\n");

    sys_arch_prctl(ARCH_SET_FS, 0);
    u_puts("[tls ] TLS_TEST exit 0\n");
    return 0;
}
