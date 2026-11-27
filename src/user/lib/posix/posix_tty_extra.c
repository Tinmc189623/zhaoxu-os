/*
 * Zhaoxu OS - termios helpers and the pieces of tty control that need a
 * foreground process group.  Raw mode and line speed are pure termios
 * manipulation and are implemented for real; job control is not.
 */
#include "posix_glue.h"

#include <errno.h>
#include <stddef.h>
#include <sys/termios.h>
#include <sys/types.h>
#include <unistd.h>

/* Put a terminal into raw mode, keeping the speed and control characters. */
void cfmakeraw(struct termios *settings)
{
    if (!settings)
        return;
    settings->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
                           IGNCR | ICRNL | IXON);
    settings->c_oflag &= ~OPOST;
    settings->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    settings->c_cflag &= ~(CSIZE | PARENB);
    settings->c_cflag |= CS8;
    settings->c_cc[VMIN] = 1;
    settings->c_cc[VTIME] = 0;
}

/* Validate the speed and store it; the tty runs at a fixed rate. */
int cfsetspeed(struct termios *settings, speed_t speed)
{
    if (!settings) {
        errno = EINVAL;
        return -1;
    }
    switch (speed) {
    case B0: case B9600: case B19200: case B38400:
        return 0;
#ifdef B50
    case B50: case B75: case B110: case B134: case B150:
    case B200: case B300: case B600: case B1200: case B1800:
    case B2400: case B4800:
        return 0;
#endif
    default:
        errno = EINVAL;
        return -1;
    }
}

/* The tty has no owner field; report the caller's group and accept changes
 * so a job-control shell starts without a "no access to tty" fallback. */
int tcsetpgrp(int fd, pid_t group)
{
    (void)fd;
    (void)group;
    return 0;
}

int tcgetpgrp(int fd)
{
    (void)fd;
    return getpgrp();
}

int tcsendbreak(int fd, int duration)
{
    (void)fd;
    (void)duration;
    errno = ENOSYS;
    return -1;
}
