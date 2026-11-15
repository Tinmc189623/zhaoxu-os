#ifndef ZHAOXU_SYS_IOCTL_H
#define ZHAOXU_SYS_IOCTL_H

/* Terminal ioctls exposed by kernel/proc/tty.c. */
#include <sys/types.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define FIONREAD    0x541B
#define TIOCEXCL    0x540C
#define TIOCNXCL    0x540D

int ioctl(int fd, unsigned long request, ...);

#endif
