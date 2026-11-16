/* Zhaoxu OS - newlib termios glue.
 *
 * The kernel tty speaks the Linux-compatible ioctl numbers defined in
 * user/lib/posix/include/sys/termios.h; these wrappers are what ncurses and
 * nano call. */
#include "posix_glue.h"

#include <errno.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/* Underscored ioctl stub newlib calls with a fixed pointer argument. */
int _ioctl(int fd, unsigned long request, void *argument)
{
    long result = zos_syscall3(ZOS_IOCTL, fd, (long)request,
                               (long)(uintptr_t)argument);
    if (result < 0) {
        errno = ENOTTY;
        return -1;
    }
    return 0;
}

/* isatty(): TCGETS succeeds only for the default console descriptors. */
int _isatty(int fd)
{
    struct termios termios;
    return zos_syscall3(ZOS_IOCTL, fd, ZOS_TCGETS,
                        (long)(uintptr_t)&termios) == 0 ? 1 : 0;
}

/* Varargs ioctl used by ncurses; the third argument is always a pointer. */
int ioctl(int fd, unsigned long request, ...)
{
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    return zos_syscall3(ZOS_IOCTL, fd, (long)request,
                        (long)(uintptr_t)argument) == 0 ? 0 : -1;
}

/* Public isatty alias used by libc. */
int isatty(int fd)
{
    return _isatty(fd);
}

/* Save the current terminal settings. */
int tcgetattr(int fd, struct termios *termios)
{
    return ioctl(fd, TCGETS, termios);
}

/* Apply terminal settings; the kernel ignores the queue action. */
int tcsetattr(int fd, int action, const struct termios *termios)
{
    (void)action;
    return ioctl(fd, TCSETS, (void *)termios);
}

/* The console has a fixed speed, so both directions report B38400. */
speed_t cfgetospeed(const struct termios *termios)
{
    (void)termios;
    return B38400;
}

speed_t cfgetispeed(const struct termios *termios)
{
    (void)termios;
    return B38400;
}

int cfsetospeed(struct termios *termios, speed_t speed)
{
    (void)termios;
    (void)speed;
    return 0;
}

int cfsetispeed(struct termios *termios, speed_t speed)
{
    (void)termios;
    (void)speed;
    return 0;
}

/* The tty has no output queue to flush or drain yet. */
int tcflush(int fd, int queue)
{
    (void)fd;
    (void)queue;
    return 0;
}

int tcdrain(int fd)
{
    (void)fd;
    return 0;
}

int tcflow(int fd, int action)
{
    (void)fd;
    (void)action;
    return 0;
}
