#ifndef ZHAOXU_SYS_UTSNAME_H
#define ZHAOXU_SYS_UTSNAME_H
#define ZX_UTS_LEN 65
struct utsname {
    char sysname[ZX_UTS_LEN];
    char nodename[ZX_UTS_LEN];
    char release[ZX_UTS_LEN];
    char version[ZX_UTS_LEN];
    char machine[ZX_UTS_LEN];
    char domainname[ZX_UTS_LEN];
};
int uname(struct utsname *buffer);
#endif
