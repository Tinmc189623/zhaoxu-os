/* csh - command registry. */
#include "csh.h"

const struct csh_command csh_commands[] = {
    { "help",     csh_help,     "show this command list" },
    { "echo",     csh_echo,     "print the given text" },
    { "version",  csh_version,  "version and kernel information" },
    { "uname",    csh_uname,    "system identification" },
    { "uptime",   csh_uptime,   "time since boot" },
    { "cpus",     csh_cpus,     "online CPU count" },
    { "pid",      csh_pid,      "shell process id" },
    { "ls",       csh_ls,       "list a directory (default /)" },
    { "cat",      csh_cat,      "print a file" },
    { "write",    csh_write,    "create or overwrite a file under /mnt" },
    { "rm",       csh_rm,       "remove a file under /mnt" },
    { "run",      csh_run,      "execute a .zxe or ELF64 program in ring 3" },
    { "disks",    csh_disks,    "list AHCI disks and installer roles" },
    { "install",  csh_install,  "install to a blank target disk (MBR/GPT + ext4 root)" },
    { "neofetch", csh_neofetch, "show a system summary" },
    { "exit",     csh_exit,     "leave the shell" },
};

const int csh_command_count = sizeof(csh_commands) / sizeof(csh_commands[0]);
