#ifndef ZHAOXU_PIPE_H
#define ZHAOXU_PIPE_H

#include <types.h>

/* Byte-stream pipe shared by two zfd descriptors.  The buffer is a single
 * ring allocated at pipe() time; readers and writers block with 1 ms
 * scheduler sleeps, matching the unix_socket transport. */
#define PIPE_CAPACITY 65536
#define PIPE_READ_END 0
#define PIPE_WRITE_END 1

struct pipe;

struct pipe *pipe_create(void);
void pipe_ref(struct pipe *pipe);
void pipe_release(struct pipe *pipe);
void pipe_attach(struct pipe *pipe, int end);
void pipe_detach(struct pipe *pipe, int end);
int pipe_read(struct pipe *pipe, void *buffer, u64 length, int nonblock);
int pipe_write(struct pipe *pipe, const void *buffer, u64 length,
               int nonblock);
short pipe_poll(struct pipe *pipe, int end, short events);
void pipe_mark_pty(struct pipe *pipe);
int pipe_is_pty(const struct pipe *pipe);

#endif
