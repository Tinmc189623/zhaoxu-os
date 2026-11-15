/*
 * Zhaoxu OS - POSIX pipe().
 *
 * One ring buffer is shared by a read end and a write end.  Descriptors live
 * in the per-process zfd table, so scm/dup paths only have to adjust the
 * end counts and the object refcount.  Blocking uses short scheduler sleeps
 * because a pipe has no wakeup primitive in the kernel yet; the timings match
 * the unix_socket transport.
 */
#include <kprintf.h>
#include <mem.h>
#include <pipe.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <types.h>

struct pipe {
    spinlock_t lock;
    int refs;
    int readers;
    int writers;
    u32 read_pos;
    u32 write_pos;
    u32 used;
    u8 *buffer;
};

struct pipe *pipe_create(void)
{
    struct pipe *pipe = kzalloc(sizeof(*pipe));
    if (!pipe)
        return NULL;
    pipe->buffer = kmalloc(PIPE_CAPACITY);
    if (!pipe->buffer) {
        kfree(pipe);
        return NULL;
    }
    pipe->lock.lock = 0;   /* kzalloc already zeroed it */
    pipe->refs = 1;
    pipe->readers = 1;
    pipe->writers = 1;
    return pipe;
}

void pipe_ref(struct pipe *pipe)
{
    if (!pipe)
        return;
    u64 flags = spin_lock_irqsave(&pipe->lock);
    pipe->refs++;
    spin_unlock_irqrestore(&pipe->lock, flags);
}

void pipe_release(struct pipe *pipe)
{
    if (!pipe)
        return;
    u64 flags = spin_lock_irqsave(&pipe->lock);
    int refs = --pipe->refs;
    spin_unlock_irqrestore(&pipe->lock, flags);
    if (refs > 0)
        return;
    kfree(pipe->buffer);
    kfree(pipe);
}

/* A duplicated end (dup/fork) keeps the matching waiter count balanced. */
void pipe_attach(struct pipe *pipe, int end)
{
    if (!pipe)
        return;
    u64 flags = spin_lock_irqsave(&pipe->lock);
    if (end == PIPE_WRITE_END)
        pipe->writers++;
    else
        pipe->readers++;
    spin_unlock_irqrestore(&pipe->lock, flags);
}

void pipe_detach(struct pipe *pipe, int end)
{
    if (!pipe)
        return;
    u64 flags = spin_lock_irqsave(&pipe->lock);
    if (end == PIPE_WRITE_END) {
        if (pipe->writers > 0)
            pipe->writers--;
    } else {
        if (pipe->readers > 0)
            pipe->readers--;
    }
    spin_unlock_irqrestore(&pipe->lock, flags);
}

int pipe_read(struct pipe *pipe, void *buffer, u64 length, int nonblock)
{
    if (!pipe || !buffer || length == 0)
        return length ? -1 : 0;

    for (;;) {
        u64 flags = spin_lock_irqsave(&pipe->lock);
        if (pipe->used) {
            u64 copied = 0;
            u8 *out = buffer;
            while (copied < length && pipe->used) {
                out[copied++] = pipe->buffer[pipe->read_pos];
                pipe->read_pos = (pipe->read_pos + 1) % PIPE_CAPACITY;
                pipe->used--;
            }
            spin_unlock_irqrestore(&pipe->lock, flags);
            return (int)copied;
        }
        int eof = pipe->writers == 0;
        spin_unlock_irqrestore(&pipe->lock, flags);

        if (eof)
            return 0;
        if (nonblock)
            return -1;
        thread_sleep_ms(1);
    }
}

int pipe_write(struct pipe *pipe, const void *buffer, u64 length, int nonblock)
{
    if (!pipe || (!buffer && length))
        return -1;
    if (length == 0)
        return 0;

    const u8 *in = buffer;
    u64 written = 0;
    while (written < length) {
        u64 flags = spin_lock_irqsave(&pipe->lock);
        if (pipe->readers == 0) {
            spin_unlock_irqrestore(&pipe->lock, flags);
            return -1;
        }
        u32 space = PIPE_CAPACITY - pipe->used;
        if (space) {
            u32 chunk = (u32)(length - written);
            if (chunk > space)
                chunk = space;
            for (u32 i = 0; i < chunk; i++) {
                pipe->buffer[pipe->write_pos] = in[written + i];
                pipe->write_pos = (pipe->write_pos + 1) % PIPE_CAPACITY;
            }
            pipe->used += chunk;
            written += chunk;
            spin_unlock_irqrestore(&pipe->lock, flags);
            continue;
        }
        spin_unlock_irqrestore(&pipe->lock, flags);
        if (nonblock)
            return written ? (int)written : -1;
        thread_sleep_ms(1);
    }
    return (int)written;
}

short pipe_poll(struct pipe *pipe, int end, short events)
{
    if (!pipe)
        return 32;   /* POLLNVAL */
    u64 flags = spin_lock_irqsave(&pipe->lock);
    short ready = 0;
    if (end == PIPE_READ_END) {
        if (pipe->used || pipe->writers == 0)
            ready |= 0x001;   /* POLLIN */
    } else {
        if (pipe->readers == 0)
            ready |= 0x010;   /* POLLHUP */
        else if (pipe->used < PIPE_CAPACITY)
            ready |= 0x004;   /* POLLOUT */
    }
    spin_unlock_irqrestore(&pipe->lock, flags);
    return (short)(ready & events);
}
