#ifndef ZHAOXU_CSH_H
#define ZHAOXU_CSH_H

#include "userlib.h"

#define CSH_MAX_LINE 1024
#define CSH_MAX_ARGS 8

struct csh_command {
    const char *name;
    void (*handler)(int argc, char **argv);
    const char *help;
};

extern const struct csh_command csh_commands[];
extern const int csh_command_count;

/* io.c */
void csh_out(const char *text);
void csh_outln(const char *text);
void csh_number(long value);
void csh_error(const char *text);

/* builtins.c */
void csh_help(int argc, char **argv);
void csh_echo(int argc, char **argv);
void csh_version(int argc, char **argv);
void csh_uname(int argc, char **argv);
void csh_uptime(int argc, char **argv);
void csh_cpus(int argc, char **argv);
void csh_pid(int argc, char **argv);
void csh_neofetch(int argc, char **argv);
void csh_exit(int argc, char **argv);

/* files.c */
void csh_ls(int argc, char **argv);
void csh_cat(int argc, char **argv);

/* run.c */
void csh_run(int argc, char **argv);

/* write.c */
void csh_write(int argc, char **argv);
void csh_rm(int argc, char **argv);

/* install.c */
void csh_disks(int argc, char **argv);
void csh_install(int argc, char **argv);

#endif
