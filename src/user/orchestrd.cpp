// Zhaoxu OS - Orchestrd, the PID 1 service manager (rule 31, launchd role).
//
// The kernel starts this program instead of the desktop.  Orchestrd owns the
// service lifecycle: it starts the Aqua desktop service, waits for it, and
// restarts it when it exits.  Thresh and Voyage join the same table once they
// exist.

extern "C" {
#include <userlib.h>
}

extern "C" int user_main(long *stack)
{
    (void)stack;

    for (;;) {
        long desktop = sys_spawn("/bin/aqua");
        if (desktop > 0) {
            int status = 0;
            sys_wait(desktop, &status);
        }
        sys_sleep(500);
    }
}
