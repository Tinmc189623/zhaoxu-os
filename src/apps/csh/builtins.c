/*
 * csh - built-in commands that only need kernel information syscalls.
 */
#include <version.h>

#include "csh.h"

void csh_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < csh_command_count; i++) {
        csh_out("  ");
        csh_out(csh_commands[i].name);
        csh_out("\t");
        csh_outln(csh_commands[i].help);
    }
}

void csh_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            csh_out(" ");
        csh_out(argv[i]);
    }
    csh_out("\n");
}

void csh_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    csh_outln("Zhaoxu OS " ZHAOXU_VERSION " build " ZHAOXU_BUILD " \"" ZHAOXU_CODENAME "\"");
    csh_outln("Kernel: " ZHAOXU_KERNEL " Kernel " ZHAOXU_KERNEL_VERSION
              " (x86_64, microkernel)");
    csh_outln("Shell: 8sh (ring 3)");
}

void csh_uname(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    csh_outln("Zhaoxu OS " ZHAOXU_VERSION " build " ZHAOXU_BUILD
              " x86_64 ring3 (" ZHAOXU_KERNEL " " ZHAOXU_KERNEL_VERSION ")");
}

void csh_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    long seconds = sys_uptime() / 1000;
    csh_out("up ");
    csh_number(seconds / 3600);
    csh_out("h ");
    csh_number((seconds / 60) % 60);
    csh_out("m ");
    csh_number(seconds % 60);
    csh_outln("s");
}

void csh_cpus(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    csh_out("online CPUs: ");
    csh_number(sys_cpus());
    csh_out("\n");
}

void csh_pid(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    csh_out("pid ");
    csh_number(sys_getpid());
    csh_out("\n");
}

void csh_neofetch(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    csh_outln("      Zhaoxu");
    csh_outln("     /\\    OS:     Zhaoxu OS " ZHAOXU_VERSION " build "
              ZHAOXU_BUILD);
    csh_outln("    /  \\   Kernel: " ZHAOXU_KERNEL " Kernel "
              ZHAOXU_KERNEL_VERSION);
    csh_outln("   /    \\  Shell:  8sh (ring 3)");
    csh_out("  /______\\ Uptime: ");
    csh_number(sys_uptime() / 1000);
    csh_outln("s");
    csh_out("            CPUs:   ");
    csh_number(sys_cpus());
    csh_out("\n");
}

void csh_exit(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sys_exit(0);
}
