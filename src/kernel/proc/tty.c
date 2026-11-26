/*
 * Zhaoxu OS - POSIX terminal state for the ring-3 console.
 *
 * There is one console, so the tty is a kernel-global device rather than an
 * object on a descriptor.  Canonical mode buffers a line and echoes it;
 * raw mode (used by editors) hands bytes back unchanged and performs no
 * line editing.  Reads sleep on the scheduler while the driver key ring is
 * empty, matching the pipe and socket transports.
 */
#include "process_internal.h"

#include <console.h>
#include <dev.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <tty.h>
#include <types.h>

#define TTY_LINE_MAX 256

static struct {
    spinlock_t lock;
    struct tty_termios termios;
    u8 line[TTY_LINE_MAX];
    u32 line_length;
    int line_ready;
} g_tty;

void tty_init(void)
{
    memset(&g_tty, 0, sizeof(g_tty));
    g_tty.termios.c_lflag = TTY_ICANON | TTY_ECHO | TTY_ISIG;
    g_tty.termios.c_cc[TTY_CC_VERASE] = 0x7F;
    g_tty.termios.c_cc[TTY_CC_VMIN] = 1;
    g_tty.termios.c_cc[TTY_CC_VTIME] = 0;
}

int tty_has_input(void)
{
    return dev_key_has_input();
}

/* Raw mode: one key per read.  Canonical mode: collect until newline, then
 * hand the line back (including the trailing newline). */
int tty_read(struct process *process, void *buffer, u64 length, int nonblock)
{
    (void)process;
    if (!buffer || length == 0)
        return 0;

    u8 *out = buffer;
    for (;;) {
        u64 flags = spin_lock_irqsave(&g_tty.lock);
        int canonical = (g_tty.termios.c_lflag & TTY_ICANON) != 0;
        int echo = (g_tty.termios.c_lflag & TTY_ECHO) != 0;
        if (canonical && g_tty.line_ready) {
            u64 copied = g_tty.line_length;
            if (copied > length)
                copied = length;
            memcpy(out, g_tty.line, (usize)copied);
            g_tty.line_length = 0;
            g_tty.line_ready = 0;
            spin_unlock_irqrestore(&g_tty.lock, flags);
            return (int)copied;
        }
        spin_unlock_irqrestore(&g_tty.lock, flags);

        int key = dev_key_pop();
        if (key < 0) {
            if (nonblock)
                return -1;
            thread_sleep_ms(1);
            continue;
        }

        if (!canonical) {
            out[0] = (u8)key;
            return 1;
        }

        flags = spin_lock_irqsave(&g_tty.lock);
        if (key == '\r')
            key = '\n';
        if (key == g_tty.termios.c_cc[TTY_CC_VERASE] || key == '\b') {
            if (g_tty.line_length) {
                g_tty.line_length--;
                if (echo)
                    console_write("\b \b");
            }
        } else if (key == '\n') {
            if (g_tty.line_length < TTY_LINE_MAX)
                g_tty.line[g_tty.line_length++] = '\n';
            g_tty.line_ready = 1;
            if (echo)
                console_write("\n");
        } else if (key >= 0x20) {
            if (g_tty.line_length < TTY_LINE_MAX) {
                g_tty.line[g_tty.line_length++] = (u8)key;
                if (echo)
                    console_putc((char)key);
            }
        }
        spin_unlock_irqrestore(&g_tty.lock, flags);
    }
}

int yuanshuo_tty_ioctl(struct process *process, int fd, u64 request, u64 argument)
{
    if (fd < 0 || fd > 2)
        return -1;

    switch (request) {
    case TTY_TCGETS: {
        struct tty_termios termios;
        u64 flags = spin_lock_irqsave(&g_tty.lock);
        termios = g_tty.termios;
        spin_unlock_irqrestore(&g_tty.lock, flags);
        return process_user_write(process, argument, &termios,
                                  sizeof(termios)) == 0 ? 0 : -1;
    }
    case TTY_TCSETS: {
        struct tty_termios termios;
        if (user_range_ok(process, argument, sizeof(termios)) != 1)
            return -1;
        memcpy(&termios, (const void *)(uintptr_t)argument, sizeof(termios));
        u64 flags = spin_lock_irqsave(&g_tty.lock);
        g_tty.termios.c_lflag = termios.c_lflag &
            (TTY_ICANON | TTY_ECHO | TTY_ISIG);
        g_tty.termios.c_cc[TTY_CC_VMIN] = termios.c_cc[TTY_CC_VMIN];
        g_tty.termios.c_cc[TTY_CC_VTIME] = termios.c_cc[TTY_CC_VTIME];
        if (!(g_tty.termios.c_lflag & TTY_ICANON)) {
            g_tty.line_length = 0;
            g_tty.line_ready = 0;
        }
        spin_unlock_irqrestore(&g_tty.lock, flags);
        return 0;
    }
    case TTY_TIOCGWINSZ: {
        struct tty_winsize winsize;
        memset(&winsize, 0, sizeof(winsize));
        winsize.row = 25;
        winsize.col = 80;
        return process_user_write(process, argument, &winsize,
                                  sizeof(winsize)) == 0 ? 0 : -1;
    }
    case TTY_FIONREAD: {
        int available = dev_key_has_input() ? 1 : 0;
        return process_user_write(process, argument, &available,
                                  sizeof(available)) == 0 ? 0 : -1;
    }
    default:
        return -1;
    }
}
