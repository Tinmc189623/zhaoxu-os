#ifndef ZHAOXU_PTY_H
#define ZHAOXU_PTY_H

#include <pipe.h>
#include <tty.h>
#include <types.h>

/* Pseudo-terminal pair for the desktop Terminal (rule 1: an interactive
 * shell needs a terminal, not the console).  The byte transport reuses the
 * pipe rings; this layer adds the terminal state bash asks for through
 * ioctl: termios and window size.  There is no line discipline yet, so echo
 * and canonical editing are the terminal application's job. */
#define PTY_MAX 8

struct process;

/* Create a pair in the caller's zfd table.  user_fds receives
 * { master_read, slave_write, slave_read, master_write }; the terminal keeps
 * the master pair, the child dup2()s the slave pair onto fd 0/1/2. */
int pty_create(struct process *process, u64 user_fds);

/* Non-NULL only for a pipe object that belongs to a pty pair. */
struct tty_termios *pty_termios_of(struct pipe *object);
struct tty_winsize *pty_winsize_of(struct pipe *object);

#endif
