/*
 * Zhaoxu OS - per-process descriptor table for sockets and memory files.
 *
 * Descriptors start at ZFD_BASE so the process-global VFS descriptors keep
 * their historical numbers.  The table itself lives in struct process; the
 * global lock only serialises SCM_RIGHTS installs into another process's
 * table.  Object release happens after the lock is dropped so a release can
 * take the socket or memfd lock without a lock-order cycle.
 */
#include "../proc/process_internal.h"

#include <memfd.h>
#include <spinlock.h>
#include <string.h>
#include <types.h>
#include <unix_socket.h>
#include <zfd.h>

static spinlock_t g_zfd_lock = SPINLOCK_INIT;

static void object_ref(int type, void *object)
{
    if (!object)
        return;
    if (type == ZFD_TYPE_SOCKET)
        unix_ref((struct unix_socket *)object);
    else if (type == ZFD_TYPE_MEMFD)
        memfd_ref((struct memfd *)object);
}

static void object_release(int type, void *object)
{
    if (!object)
        return;
    if (type == ZFD_TYPE_SOCKET)
        unix_release((struct unix_socket *)object);
    else if (type == ZFD_TYPE_MEMFD)
        memfd_release((struct memfd *)object);
}

struct zfd_entry *zfd_get(struct process *process, int fd)
{
    if (!process || fd < ZFD_BASE || fd >= ZFD_BASE + ZFD_MAX)
        return NULL;
    struct zfd_entry *entry = &process->fds[fd - ZFD_BASE];
    return entry->used ? entry : NULL;
}

/* Install `object` under a fresh descriptor and hand the caller's reference
 * to the table.  The caller must not release it afterwards. */
int zfd_alloc(struct process *process, int type, void *object, u32 flags)
{
    if (!process || !object)
        return -1;

    u64 lock = spin_lock_irqsave(&g_zfd_lock);
    for (int i = 0; i < ZFD_MAX; i++) {
        if (process->fds[i].used)
            continue;
        struct zfd_entry *entry = &process->fds[i];
        memset(entry, 0, sizeof(*entry));
        entry->used = 1;
        entry->type = type;
        entry->flags = flags;
        entry->object = object;
        spin_unlock_irqrestore(&g_zfd_lock, lock);
        return ZFD_BASE + i;
    }
    spin_unlock_irqrestore(&g_zfd_lock, lock);
    return -1;
}

int zfd_close(struct process *process, int fd)
{
    if (!process || fd < ZFD_BASE || fd >= ZFD_BASE + ZFD_MAX)
        return -1;

    u64 lock = spin_lock_irqsave(&g_zfd_lock);
    struct zfd_entry *entry = &process->fds[fd - ZFD_BASE];
    if (!entry->used) {
        spin_unlock_irqrestore(&g_zfd_lock, lock);
        return -1;
    }
    int type = entry->type;
    void *object = entry->object;
    memset(entry, 0, sizeof(*entry));
    spin_unlock_irqrestore(&g_zfd_lock, lock);

    object_release(type, object);
    return 0;
}

/* Duplicate into the same process.  Used by fcntl(F_DUPFD). */
int zfd_dup(struct process *process, int fd, int minimum)
{
    struct zfd_entry *source = zfd_get(process, fd);
    if (!source)
        return -1;

    u64 lock = spin_lock_irqsave(&g_zfd_lock);
    int start = minimum > ZFD_BASE ? minimum : ZFD_BASE;
    if (start < ZFD_BASE || start >= ZFD_BASE + ZFD_MAX) {
        spin_unlock_irqrestore(&g_zfd_lock, lock);
        return -1;
    }
    for (int i = start - ZFD_BASE; i < ZFD_MAX; i++) {
        struct zfd_entry *entry = &process->fds[i];
        if (entry->used)
            continue;
        *entry = *source;
        entry->used = 1;
        spin_unlock_irqrestore(&g_zfd_lock, lock);
        object_ref(source->type, source->object);
        return ZFD_BASE + i;
    }
    spin_unlock_irqrestore(&g_zfd_lock, lock);
    return -1;
}

/* Duplicate `fd` from the caller into `receiver`'s table.  SCM_RIGHTS uses
 * this; the object is shared, not copied.  A transferred socket changes its
 * owner so descriptors sent on it later reach the new holder. */
int zfd_transfer(struct process *receiver, int fd)
{
    struct process *sender = process_current();
    struct zfd_entry *source = zfd_get(sender, fd);
    /* Only memory objects cross SCM_RIGHTS in M0.  Transferring a socket
     * would have to re-home its owner from inside the socket lock; Wayland
     * only passes wl_shm pools (and later dma-bufs). */
    if (!source || !receiver || source->type != ZFD_TYPE_MEMFD)
        return -1;

    u64 lock = spin_lock_irqsave(&g_zfd_lock);
    int slot = -1;
    for (int i = 0; i < ZFD_MAX; i++) {
        if (!receiver->fds[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        spin_unlock_irqrestore(&g_zfd_lock, lock);
        return -1;
    }
    struct zfd_entry *entry = &receiver->fds[slot];
    *entry = *source;
    entry->used = 1;
    entry->offset = 0;
    spin_unlock_irqrestore(&g_zfd_lock, lock);

    object_ref(source->type, source->object);
    return ZFD_BASE + slot;
}

int zfd_read(struct process *process, int fd, void *buffer, u64 length)
{
    struct zfd_entry *entry = zfd_get(process, fd);
    if (!entry)
        return -1;
    int nonblock = (entry->flags & ZFD_O_NONBLOCK) != 0;
    if (entry->type == ZFD_TYPE_SOCKET)
        return unix_recv((struct unix_socket *)entry->object, buffer, length,
                         nonblock);
    if (entry->type == ZFD_TYPE_MEMFD)
        return memfd_read((struct memfd *)entry->object, &entry->offset,
                          buffer, length);
    return -1;
}

int zfd_write(struct process *process, int fd, const void *buffer, u64 length)
{
    struct zfd_entry *entry = zfd_get(process, fd);
    if (!entry)
        return -1;
    int nonblock = (entry->flags & ZFD_O_NONBLOCK) != 0;
    if (entry->type == ZFD_TYPE_SOCKET)
        return unix_send((struct unix_socket *)entry->object, buffer, length,
                         nonblock);
    if (entry->type == ZFD_TYPE_MEMFD)
        return memfd_write((struct memfd *)entry->object, &entry->offset,
                           buffer, length);
    return -1;
}

int zfd_lseek(struct process *process, int fd, i64 offset, int whence)
{
    struct zfd_entry *entry = zfd_get(process, fd);
    if (!entry || entry->type != ZFD_TYPE_MEMFD)
        return -1;
    return memfd_seek((struct memfd *)entry->object, &entry->offset, offset,
                      whence);
}

int zfd_ftruncate(struct process *process, int fd, u64 size)
{
    struct zfd_entry *entry = zfd_get(process, fd);
    if (!entry || entry->type != ZFD_TYPE_MEMFD)
        return -1;
    return memfd_truncate((struct memfd *)entry->object, size);
}

short zfd_poll_events(struct process *process, int fd, short events)
{
    struct zfd_entry *entry = zfd_get(process, fd);
    if (!entry)
        return 32; /* POLLNVAL */
    if (entry->type == ZFD_TYPE_SOCKET)
        return unix_poll((struct unix_socket *)entry->object, events);
    if (entry->type == ZFD_TYPE_MEMFD)
        return memfd_poll((struct memfd *)entry->object, events);
    return 32;
}

int zfd_set_nonblock(struct process *process, int fd, int enabled)
{
    struct zfd_entry *entry = zfd_get(process, fd);
    if (!entry)
        return -1;
    if (enabled)
        entry->flags |= ZFD_O_NONBLOCK;
    else
        entry->flags &= ~ZFD_O_NONBLOCK;
    return 0;
}

/* Close every descriptor before the address space is torn down.  memfd
 * mappings must be unmapped while the page tables still exist. */
void zfd_process_exit(struct process *process)
{
    if (!process)
        return;

    memfd_process_exit(process);

    for (int i = 0; i < ZFD_MAX; i++) {
        int type;
        void *object;
        u64 lock = spin_lock_irqsave(&g_zfd_lock);
        struct zfd_entry *entry = &process->fds[i];
        if (!entry->used) {
            spin_unlock_irqrestore(&g_zfd_lock, lock);
            continue;
        }
        type = entry->type;
        object = entry->object;
        memset(entry, 0, sizeof(*entry));
        spin_unlock_irqrestore(&g_zfd_lock, lock);
        object_release(type, object);
    }
}
