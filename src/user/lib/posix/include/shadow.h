/* Shadow password entries for the login toys. */
#ifndef ZHAOXU_SHADOW_H
#define ZHAOXU_SHADOW_H

struct spwd {
    char *sp_namp;
    char *sp_pwdp;
    long sp_lstchg;
    long sp_min;
    long sp_max;
    long sp_warn;
    long sp_inact;
    long sp_expire;
    unsigned long sp_flag;
};

struct spwd *getspnam(const char *name);

#endif
