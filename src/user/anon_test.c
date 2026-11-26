/* Zhaoxu OS - anonymous mapping regression (SYS_MMAP/SYS_MUNMAP/SYS_MPROTECT).
 *
 * Covers first-fit placement, MAP_FIXED, write/read isolation, mprotect
 * round-trips, MADV_DONTNEED zeroing, unmap/reuse zero-fill, and the
 * 512 MiB size rejection that glibc's arena code depends on.
 */
#include "userlib.h"

#define ANON_WINDOW_BASE   0x0000010400000000UL   /* 1 TiB + 16 GiB */
#define ANON_WINDOW_LIMIT  0x0000011000000000UL   /* 1 TiB + 64 GiB */

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_PRIVATE   0x2
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define MADV_DONTNEED 4

static int fail(const char *reason)
{
    u_puts("[anon] FAIL: ");
    u_puts(reason);
    u_puts("\n");
    return 1;
}

/* Map `length` bytes; fd is -1 and only MAP_ANONYMOUS is set. */
static long anon_map(unsigned long address, unsigned long length,
                     unsigned long prot, unsigned long extra_flags)
{
    struct zos_mmap_args args;
    args.address = address;
    args.length = length;
    args.protection = prot;
    args.flags = MAP_PRIVATE | MAP_ANONYMOUS | extra_flags;
    args.fd = -1;
    args.offset = 0;
    return sys_mmap(&args);
}

int user_main(long *stack)
{
    (void)stack;

    long a = anon_map(0, 3 * 4096, PROT_READ | PROT_WRITE, 0);
    if (a <= 0 || (a & 0xfff))
        return fail("map");
    if ((unsigned long)a < ANON_WINDOW_BASE ||
        (unsigned long)a >= ANON_WINDOW_LIMIT)
        return fail("window");
    u_puts("[anon] map PASS\n");

    unsigned char *first = (unsigned char *)a;
    first[0] = 0x5a;
    first[2 * 4096 + 31] = 0xa5;
    if (first[0] != 0x5a || first[2 * 4096 + 31] != 0xa5)
        return fail("write");
    for (int i = 1; i < 4096; i++) {
        if (first[i] != 0)
            return fail("zero-fill");
    }
    u_puts("[anon] write PASS\n");

    long b = anon_map(0, 4096, PROT_READ | PROT_WRITE, 0);
    if (b <= 0 || b == a)
        return fail("second-map");
    if ((unsigned long)b < (unsigned long)a + 3 * 4096 &&
        (unsigned long)a < (unsigned long)b + 4096)
        return fail("overlap");
    u_puts("[anon] no-overlap PASS\n");

    if (sys_mprotect((void *)a, 2 * 4096, PROT_READ) != 0)
        return fail("mprotect-ro");
    if (first[0] != 0x5a || first[4096] != 0)
        return fail("mprotect-ro-read");
    if (sys_mprotect((void *)a, 2 * 4096, PROT_READ | PROT_WRITE) != 0)
        return fail("mprotect-rw");
    first[4096] = 0x3c;
    if (first[4096] != 0x3c)
        return fail("mprotect-rw-write");
    u_puts("[anon] mprotect PASS\n");

    /* mprotect over an unmapped hole must fail with -ENOMEM. */
    long hole = anon_map(ANON_WINDOW_BASE + 0x10000000UL, 4096,
                         PROT_READ | PROT_WRITE, MAP_FIXED);
    if (hole != (long)(ANON_WINDOW_BASE + 0x10000000UL))
        return fail("fixed");
    if (sys_munmap((void *)hole, 4096) != 0)
        return fail("hole-unmap");
    if (sys_mprotect((void *)hole, 4096, PROT_READ) != -12)
        return fail("hole-mprotect");
    u_puts("[anon] fixed PASS\n");

    if (sys_madvise((void *)a, 3 * 4096, MADV_DONTNEED) != 0)
        return fail("madvise");
    if (first[0] != 0 || first[2 * 4096 + 31] != 0)
        return fail("madvise-zero");
    u_puts("[anon] madvise PASS\n");

    if (sys_munmap((void *)a, 3 * 4096) != 0)
        return fail("unmap");
    long again = anon_map((unsigned long)a, 3 * 4096,
                          PROT_READ | PROT_WRITE, MAP_FIXED);
    if (again != a)
        return fail("remap-fixed");
    for (int i = 0; i < 3 * 4096; i++) {
        if (first[i] != 0)
            return fail("remap-zero");
    }
    if (sys_munmap((void *)again, 3 * 4096) != 0)
        return fail("remap-unmap");
    if (sys_munmap((void *)b, 4096) != 0)
        return fail("unmap-b");
    u_puts("[anon] reuse PASS\n");

    long huge = anon_map(0, 600UL * 1024 * 1024, PROT_READ | PROT_WRITE, 0);
    if (huge != -22)
        return fail("limit");
    u_puts("[anon] limit PASS\n");
    u_puts("[anon] ANON_TEST exit 0\n");
    return 0;
}
