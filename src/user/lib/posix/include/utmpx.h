/* POSIX utmpx record layout; newlib ships the API without the fields. */
#ifndef ZHAOXU_UTMPX_OVERLAY_H
#define ZHAOXU_UTMPX_OVERLAY_H

#include <sys/types.h>
#include <sys/time.h>

/* Standard database path; login accounting will create it once Thresh exists. */
#define _PATH_UTMPX "/var/run/utmp"

#define EMPTY 0
#define RUN_LVL 1
#define BOOT_TIME 2
#define NEW_TIME 3
#define OLD_TIME 4
#define INIT_PROCESS 5
#define LOGIN_PROCESS 6
#define USER_PROCESS 7
#define DEAD_PROCESS 8

struct exit_status {
    short e_termination;
    short e_exit;
};

struct utmpx {
    short ut_type;
    pid_t ut_pid;
    char ut_line[32];
    char ut_id[4];
    char ut_user[32];
    char ut_host[256];
    struct exit_status ut_exit;
    long ut_session;
    struct timeval ut_tv;
    int32_t ut_addr_v6[4];
    char zx_unused[20];
};

struct utmpx *getutxent(void);
void setutxent(void);
void endutxent(void);

#endif
