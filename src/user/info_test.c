/* Zhaoxu OS - task, clock, entropy and information syscalls (89-112).
 *
 * Covers tid identity, exit_group status, getrandom, both clocks, uname,
 * access, readlink, rlimits and the last_error handoff from a legacy call.
 */
#include "userlib.h"

static int fail(const char *reason)
{
    u_puts("[info ] FAIL: ");
    u_puts(reason);
    u_puts("\n");
    return 1;
}

/* Compare two byte buffers. */
static int bytes_differ(const unsigned char *a, const unsigned char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 1;
    }
    return 0;
}

/* Match a NUL-terminated field against a literal. */
static int field_is(const char *field, const char *expect)
{
    for (int i = 0;; i++) {
        if (field[i] != expect[i])
            return 0;
        if (expect[i] == 0)
            return 1;
    }
}

int user_main(long *stack)
{
    (void)stack;

    long tid = sys_gettid();
    if (tid <= 0)
        return fail("gettid");
    int tid_word = -1;
    if (sys_set_tid_address(&tid_word) != tid)
        return fail("set_tid_address");
    u_puts("[info ] tid PASS\n");

    long pid = sys_fork();
    if (pid < 0)
        return fail("fork");
    if (pid == 0) {
        sys_exit_group(7);
        return 3;          /* exit_group must not return */
    }
    int status = -1;
    if (sys_wait(pid, &status) < 0 || status != 7)
        return fail("exit_group");
    u_puts("[info ] exit_group PASS\n");

    unsigned char first[16];
    unsigned char second[16];
    if (sys_getrandom(first, sizeof(first), 0) != (long)sizeof(first))
        return fail("getrandom");
    if (sys_getrandom(second, sizeof(second), 0) != (long)sizeof(second))
        return fail("getrandom-2");
    int nonzero = 0;
    for (int i = 0; i < 16; i++) {
        if (first[i] || second[i])
            nonzero = 1;
    }
    if (!nonzero || !bytes_differ(first, second, 16))
        return fail("random-quality");
    u_puts("[info ] getrandom PASS\n");

    struct zos_timespec mono1;
    struct zos_timespec mono2;
    if (sys_clock_gettime(CLOCK_MONOTONIC, &mono1) != 0)
        return fail("monotonic");
    sys_sleep(30);
    if (sys_clock_gettime(CLOCK_MONOTONIC, &mono2) != 0)
        return fail("monotonic-2");
    if (mono2.seconds < mono1.seconds ||
        (mono2.seconds == mono1.seconds &&
         mono2.seconds * 1000000000LL + mono2.nanoseconds <=
             mono1.seconds * 1000000000LL + mono1.nanoseconds))
        return fail("monotonic-order");
    u_puts("[info ] monotonic PASS\n");

    struct zos_timespec real;
    if (sys_clock_gettime(CLOCK_REALTIME, &real) != 0)
        return fail("realtime");
    /* Between 2020 and 2100: the RTC path produced a sane epoch. */
    if (real.seconds < 1577836800LL || real.seconds > 4102444800LL)
        return fail("realtime-range");
    u_puts("[info ] realtime PASS\n");

    struct zos_utsname name;
    if (sys_uname(&name) != 0)
        return fail("uname");
    if (!field_is(name.sysname, "Zhaoxu") ||
        !field_is(name.machine, "x86_64") || name.release[0] == 0)
        return fail("uname-fields");
    u_puts("[info ] uname PASS\n");

    if (sys_access("/data/ANON_TEST.ZXE", 0) != 0)
        return fail("access-present");
    if (sys_access("/no/such/file", 0) != -2)
        return fail("access-missing");
    if (sys_readlink("/data/ANON_TEST.ZXE", 0, 0) != -38)
        return fail("readlink");
    u_puts("[info ] access PASS\n");

    struct zos_rlimit64 limit;
    if (sys_getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.current != 1024)
        return fail("rlimit");
    if (sys_getrlimit(16, &limit) != -22)
        return fail("rlimit-invalid");
    u_puts("[info ] rlimit PASS\n");

    if (sys_open("/no/such/file", 0) != -1)
        return fail("legacy-open");
    if (sys_last_error() != 5)
        return fail("last_error");
    if (sys_last_error() != 0)
        return fail("last_error-clear");
    u_puts("[info ] last_error PASS\n");

    u_puts("[info ] INFO_TEST exit 0\n");
    return 0;
}
