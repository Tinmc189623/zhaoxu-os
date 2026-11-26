/*
 * Zhaoxu OS - pseudo-terminal pairs.
 *
 * Each pair is two pipe rings plus the terminal state an interactive shell
 * reads through ioctl.  The terminal application owns the master pair and
 * hands the slave pair to the shell through dup2().
 */
#include <mem.h>
#include <pipe.h>
#include <pty.h>
#include <string.h>
#include <types.h>
#include <zfd.h>

#include "process_internal.h"

struct pty {
    int used;
    struct pipe *in;      /* master -> slave bytes */
    struct pipe *out;     /* slave -> master bytes */
    struct tty_termios termios;
    struct tty_winsize win;
};

static struct pty g_ptys[PTY_MAX];

/* Pair lookup by either ring, so ioctl works on both ends. */
static struct pty *pty_for(struct pipe *object)
{
    if (!object)
        return NULL;
    for (int i = 0; i < PTY_MAX; i++) {
        if (g_ptys[i].used &&
            (g_ptys[i].in == object || g_ptys[i].out == object))
            return &g_ptys[i];
    }
    return NULL;
}

struct tty_termios *pty_termios_of(struct pipe *object)
{
    struct pty *pty = pty_for(object);
    return pty ? &pty->termios : NULL;
}

struct tty_winsize *pty_winsize_of(struct pipe *object)
{
    struct pty *pty = pty_for(object);
    return pty ? &pty->win : NULL;
}

/* Allocate one pair and four descriptors in the caller's zfd table. */
int pty_create(struct process *process, u64 user_fds)
{
    if (!process || !user_fds)
        return -1;

    struct pty *slot = NULL;
    for (int i = 0; i < PTY_MAX; i++) {
        if (!g_ptys[i].used) {
            slot = &g_ptys[i];
            break;
        }
    }
    if (!slot)
        return -1;

    struct pipe *in = pipe_create();
    struct pipe *out = pipe_create();
    if (!in || !out) {
        if (in)
            pipe_release(in);
        if (out)
            pipe_release(out);
        return -1;
    }
    pipe_mark_pty(in);
    pipe_mark_pty(out);

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->in = in;
    slot->out = out;
    slot->termios.c_lflag = TTY_ICANON | TTY_ECHO | TTY_ISIG;
    slot->termios.c_cc[TTY_CC_VERASE] = 0x7F;
    slot->termios.c_cc[TTY_CC_VMIN] = 1;
    slot->win.row = 25;
    slot->win.col = 80;

    int fds[4];
    fds[0] = zfd_alloc_end(process, ZFD_TYPE_PIPE, in, 0, PIPE_READ_END);
    fds[1] = zfd_alloc_end(process, ZFD_TYPE_PIPE, in, 0, PIPE_WRITE_END);
    fds[2] = zfd_alloc_end(process, ZFD_TYPE_PIPE, out, 0, PIPE_READ_END);
    fds[3] = zfd_alloc_end(process, ZFD_TYPE_PIPE, out, 0, PIPE_WRITE_END);
    for (int i = 0; i < 4; i++) {
        if (fds[i] < 0) {
            for (int j = 0; j < i; j++)
                zfd_close(process, fds[j]);
            slot->used = 0;
            pipe_release(in);
            pipe_release(out);
            return -1;
        }
    }

    if (process_user_write(process, user_fds, fds, sizeof(fds)) != 0) {
        for (int i = 0; i < 4; i++)
            zfd_close(process, fds[i]);
        slot->used = 0;
        pipe_release(in);
        pipe_release(out);
        return -1;
    }
    return 0;
}
