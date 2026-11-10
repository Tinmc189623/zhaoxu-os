/*
 * csh - spawn a ring 3 program and wait for it.
 */
#include "csh.h"

void csh_run(int argc, char **argv)
{
    if (argc < 2) {
        csh_error("usage: run </path/to/program>");
        return;
    }

    long pid = sys_spawn(argv[1]);
    if (pid <= 0) {
        csh_error("cannot spawn program");
        return;
    }

    int status = 0;
    if (sys_wait(pid, &status) < 0) {
        csh_error("cannot wait for program");
        return;
    }

    csh_out("exited with ");
    csh_number(status);
    csh_out("\n");
}
