#ifndef ZHAOXU_TTY_H
#define ZHAOXU_TTY_H

#include <types.h>

/* Minimal POSIX terminal layer for the ring-3 console.
 *
 * fd 0/1/2 with no dup2 redirection resolve to this device: reads pull bytes
 * from the driver key ring, writes go to the kernel console, and ioctl()
 * exposes termios and window size.  The ABI below is mirrored in
 * user/userlib.h; keep both sides identical. */
#define TTY_ICANON 0x0002u
#define TTY_ECHO   0x0008u
#define TTY_ISIG   0x0001u

#define TTY_TCGETS     0x5401u
#define TTY_TCSETS     0x5402u
#define TTY_TIOCGWINSZ 0x5413u
#define TTY_FIONREAD   0x541Bu

#define TTY_CC_VINTR  0
#define TTY_CC_VQUIT  1
#define TTY_CC_VERASE 2
#define TTY_CC_VMIN   6
#define TTY_CC_VTIME  5

struct tty_termios {
    u32 c_iflag;
    u32 c_oflag;
    u32 c_cflag;
    u32 c_lflag;
    u8 c_line;
    u8 c_cc[19];
};

struct tty_winsize {
    u16 row;
    u16 col;
    u16 xpixel;
    u16 ypixel;
};

struct process;

void tty_init(void);
int tty_read(struct process *process, void *buffer, u64 length, int nonblock);
int yuanshuo_tty_ioctl(struct process *process, int fd, u64 request, u64 argument);
int tty_has_input(void);

#endif
