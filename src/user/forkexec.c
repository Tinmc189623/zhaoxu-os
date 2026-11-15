/*
 * Zhaoxu OS - POSIX fork/exec regression.
 *
 * The parent forks; the child returns 0, prints its pid and exits 7.  The
 * parent reaps it, then execs /data/HELLO.ZXE with argv, which replaces this
 * image.  Run through FE_BOOT_TEST=1, or by hand from 8sh:
 *
 *   run /data/FORKEXEC.ZXE
 */
#include "userlib.h"

int user_main(long *stack)
{
    (void)stack;

    u_puts("[forkexec] parent pid ");
    u_putn(sys_getpid());
    u_puts("\n");

    long pid = sys_fork();
    if (pid == 0) {
        u_puts("[forkexec] child pid ");
        u_putn(sys_getpid());
        u_puts(", returning 7\n");
        return 7;
    }
    if (pid < 0) {
        u_puts("[forkexec] fork failed: ");
        u_putn(pid);
        u_puts("\n");
        return 1;
    }

    int status = -1;
    long waited = sys_wait(pid, &status);
    u_puts("[forkexec] wait returned ");
    u_putn(waited);
    u_puts(", child status ");
    u_putn(status);
    u_puts("\n");

    char *argv[] = { "hello-from-exec", 0 };
    long result = sys_execv("/data/HELLO.ZXE", argv);
    u_puts("[forkexec] exec failed: ");
    u_putn(result);
    u_puts("\n");
    return 2;
}
