#ifndef ZHAOXU_ZFD_H
#define ZHAOXU_ZFD_H

#include <types.h>

/* Per-process descriptor table for the Wayland transport.
 *
 * The legacy VFS owns the small descriptors 3..18 in a process-global table
 * (kernel/fs/vfs.c).  Socket, memfd and dma-buf objects live above this base
 * so both worlds can coexist while the VFS table is migrated.  A descriptor
 * in this range names an object with a refcount; dup/SCM_RIGHTS transfers
 * add a reference instead of copying the object. */
#define ZFD_BASE 64
#define ZFD_MAX 32

#define ZFD_TYPE_SOCKET  1
#define ZFD_TYPE_MEMFD   2
#define ZFD_TYPE_EVENTFD 3
#define ZFD_TYPE_TIMERFD 4
#define ZFD_TYPE_EPOLL   5

#define ZFD_O_NONBLOCK 0x800u

struct process;

struct zfd_entry {
    int used;
    int type;
    u32 flags;
    u64 offset;      /* memfd file position */
    void *object;
};

int zfd_alloc(struct process *process, int type, void *object, u32 flags);
struct zfd_entry *zfd_get(struct process *process, int fd);
int zfd_close(struct process *process, int fd);
int zfd_dup(struct process *process, int fd, int minimum);
int zfd_transfer(struct process *receiver, int fd);
int zfd_read(struct process *process, int fd, void *buffer, u64 length);
int zfd_write(struct process *process, int fd, const void *buffer, u64 length);
int zfd_lseek(struct process *process, int fd, i64 offset, int whence);
int zfd_ftruncate(struct process *process, int fd, u64 size);
short zfd_poll_events(struct process *process, int fd, short events);
int zfd_set_nonblock(struct process *process, int fd, int enabled);
void zfd_process_exit(struct process *process);

#endif
