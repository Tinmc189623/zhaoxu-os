/*
 * Zhaoxu OS - POSIX pipe/dup2 regression.
 *
 * Runs three checks and prints one PASS line each:
 *   1. pipe write/read across fork
 *   2. blocking read returns after the writer closes (EOF)
 *   3. dup2 installs a pipe end at a chosen descriptor number
 *
 * Launch with POSIX_BOOT_TEST=1, or from 8sh:
 *   run /data/POSIX_TEST.ZXE
 */
#include "userlib.h"

#define DUP_TARGET 90   /* inside the zfd range 64..95 */

static int pipe_roundtrip(void)
{
    int fds[2];
    if (sys_pipe(fds) != 0) {
        u_puts("[posix] pipe failed\n");
        return -1;
    }

    long pid = sys_fork();
    if (pid == 0) {
        sys_close(fds[0]);
        const char message[] = "hello pipe\n";
        long written = sys_write(fds[1], message, sizeof(message) - 1);
        sys_close(fds[1]);
        return written == (long)(sizeof(message) - 1) ? 0 : 1;
    }
    if (pid < 0)
        return -1;

    sys_close(fds[1]);
    char buffer[32];
    long got = sys_read(fds[0], buffer, sizeof(buffer) - 1);
    sys_close(fds[0]);
    if (got != 11 || buffer[0] != 'h') {
        u_puts("[posix] pipe read failed\n");
        return -1;
    }

    int status = -1;
    sys_wait(pid, &status);
    if (status != 0)
        return -1;
    u_puts("[posix] pipe PASS\n");
    return 0;
}

static int pipe_eof(void)
{
    int fds[2];
    if (sys_pipe(fds) != 0)
        return -1;
    sys_close(fds[1]);
    char byte = 0;
    long got = sys_read(fds[0], &byte, 1);
    sys_close(fds[0]);
    if (got != 0) {
        u_puts("[posix] pipe eof failed\n");
        return -1;
    }
    u_puts("[posix] pipe eof PASS\n");
    return 0;
}

static int dup2_roundtrip(void)
{
    int fds[2];
    if (sys_pipe(fds) != 0)
        return -1;
    if (sys_dup2(fds[0], DUP_TARGET) != DUP_TARGET) {
        u_puts("[posix] dup2 failed\n");
        return -1;
    }
    sys_close(fds[0]);

    const char message[] = "dup2\n";
    sys_write(fds[1], message, sizeof(message) - 1);
    sys_close(fds[1]);

    char buffer[8];
    long got = sys_read(DUP_TARGET, buffer, sizeof(buffer));
    sys_close(DUP_TARGET);
    if (got != 5) {
        u_puts("[posix] dup2 read failed\n");
        return -1;
    }
    u_puts("[posix] dup2 PASS\n");
    return 0;
}

int user_main(long *stack)
{
    (void)stack;

    if (pipe_roundtrip() != 0 || pipe_eof() != 0 || dup2_roundtrip() != 0)
        return 1;
    u_puts("[posix] PIPE, EOF, DUP2 PASS\n");
    return 0;
}
