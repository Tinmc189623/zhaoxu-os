#ifndef ZHAOXU_SYS_TERMIOS_H
#define ZHAOXU_SYS_TERMIOS_H

/* POSIX termios for the ZOS tty.  newlib's generic termios.h includes this
 * header, but newlib only ships one for a few targets; the bit values and
 * c_cc indices below match the Linux numbers ncurses and nano expect.  The
 * first 36 bytes match kernel/include/tty.h's struct tty_termios. */
#include <sys/types.h>

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define NCCS 19

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
};

/* c_cc indices */
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

/* c_iflag */
#define IGNBRK  0x0001
#define BRKINT  0x0002
#define IGNPAR  0x0004
#define PARMRK  0x0008
#define INPCK   0x0010
#define ISTRIP  0x0020
#define INLCR   0x0040
#define IGNCR   0x0080
#define ICRNL   0x0100
#define IXON    0x0400
#define IXANY   0x0800
#define IXOFF   0x1000

/* c_oflag */
#define OPOST   0x0001
#define ONLCR   0x0004

/* c_cflag */
#define CSIZE   0x0030
#define CS8     0x0030
#define CREAD   0x0080
#define HUPCL   0x0400
#define CLOCAL  0x0800

/* c_lflag */
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080
#define ECHOCTL 0x0200
#define ECHOKE  0x0800
#define IEXTEN  0x8000

/* tcsetattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* Baud rates (the console ignores them, but ncurses stores them) */
#define B0      0
#define B50     1
#define B75     2
#define B110    3
#define B134    4
#define B150    5
#define B200    6
#define B300    7
#define B600    8
#define B1200   9
#define B1800   10
#define B2400   11
#define B4800   12
#define B9600   13
#define B19200  14
#define B38400  15

#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

int tcgetattr(int fd, struct termios *termios);
int tcsetattr(int fd, int action, const struct termios *termios);
int tcflush(int fd, int queue);
int tcdrain(int fd);
int tcflow(int fd, int action);
speed_t cfgetospeed(const struct termios *termios);
speed_t cfgetispeed(const struct termios *termios);
int cfsetospeed(struct termios *termios, speed_t speed);
int cfsetispeed(struct termios *termios, speed_t speed);

#endif
