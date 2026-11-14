/*
 * Zhaoxu OS - AF_UNIX SOCK_STREAM.
 *
 * Endpoints are fixed objects; each owns a 64 KiB receive ring that the
 * peer appends to.  Blocking calls sleep one tick and retry because the
 * scheduler has no wait queues yet.  SCM_RIGHTS descriptors are installed
 * in the receiving process's descriptor table and queued with a byte
 * offset, so recvmsg returns them with the message that carried them.
 */
#include "../proc/process_internal.h"

#include <mem.h>
#include <poll.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>
#include <unix_socket.h>
#include <zfd.h>

#define UNIX_MAX_SOCKETS 32
#define UNIX_RX_SIZE (64u * 1024u)
#define UNIX_MAX_PENDING 8
#define UNIX_MAX_BATCHES 8
#define UNIX_MAX_LISTENERS 16

enum unix_state {
    UNIX_INIT = 0,
    UNIX_LISTEN,
    UNIX_CONNECTED,
    UNIX_CLOSED
};

struct unix_batch {
    u64 at;                    /* receive offset when these fds become valid */
    int count;
    int fds[UNIX_MAX_FDS];
};

struct unix_socket {
    int used;
    int refs;
    int state;
    struct process *owner;
    struct unix_socket *peer;
    char path[UNIX_PATH_MAX];
    int path_len;
    int backlog;
    u8 *rx;
    u32 rx_head;
    u32 rx_tail;
    u32 rx_bytes;
    u64 rx_written;
    u64 rx_read;
    struct unix_batch batches[UNIX_MAX_BATCHES];
    int batch_count;
    struct unix_socket *pending[UNIX_MAX_PENDING];
    int pending_count;
};

struct unix_listener {
    int used;
    char path[UNIX_PATH_MAX];
    int path_len;
    struct unix_socket *socket;
};

static struct unix_socket g_sockets[UNIX_MAX_SOCKETS];
static struct unix_listener g_listeners[UNIX_MAX_LISTENERS];
static spinlock_t g_unix_lock = SPINLOCK_INIT;

static struct unix_socket *socket_free_slot(void)
{
    for (int i = 0; i < UNIX_MAX_SOCKETS; i++) {
        if (!g_sockets[i].used)
            return &g_sockets[i];
    }
    return NULL;
}

static struct unix_listener *listener_find(const char *path, int length)
{
    for (int i = 0; i < UNIX_MAX_LISTENERS; i++) {
        struct unix_listener *listener = &g_listeners[i];
        if (!listener->used || listener->path_len != length)
            continue;
        if (memcmp(listener->path, path, (usize)length) == 0)
            return listener;
    }
    return NULL;
}

static void listener_unregister(struct unix_socket *socket)
{
    for (int i = 0; i < UNIX_MAX_LISTENERS; i++) {
        if (g_listeners[i].used && g_listeners[i].socket == socket) {
            memset(&g_listeners[i], 0, sizeof(g_listeners[i]));
            return;
        }
    }
}

int unix_create(struct unix_socket **out)
{
    if (!out)
        return -1;
    u8 *rx = kmalloc(UNIX_RX_SIZE);
    if (!rx)
        return -1;
    memset(rx, 0, UNIX_RX_SIZE);

    u64 lock = spin_lock_irqsave(&g_unix_lock);
    struct unix_socket *socket = socket_free_slot();
    if (!socket) {
        spin_unlock_irqrestore(&g_unix_lock, lock);
        kfree(rx);
        return -1;
    }
    memset(socket, 0, sizeof(*socket));
    socket->used = 1;
    socket->refs = 1;
    socket->state = UNIX_INIT;
    socket->rx = rx;
    spin_unlock_irqrestore(&g_unix_lock, lock);

    *out = socket;
    return 0;
}

/* Create a connected pair owned by the calling process.  Both endpoints are
 * returned with one reference each; the caller installs them as descriptors.
 * socketpair() is what the M0 self-test and libwayland's wakeup path use. */
int unix_socketpair(struct unix_socket **first, struct unix_socket **second)
{
    if (!first || !second)
        return -1;
    struct unix_socket *a = NULL;
    struct unix_socket *b = NULL;
    if (unix_create(&a) != 0)
        return -1;
    if (unix_create(&b) != 0) {
        unix_release(a);
        return -1;
    }

    u64 lock = spin_lock_irqsave(&g_unix_lock);
    a->state = UNIX_CONNECTED;
    b->state = UNIX_CONNECTED;
    a->peer = b;
    b->peer = a;
    a->owner = process_current();
    b->owner = process_current();
    spin_unlock_irqrestore(&g_unix_lock, lock);

    *first = a;
    *second = b;
    return 0;
}

void unix_ref(struct unix_socket *socket)
{
    if (!socket)
        return;
    u64 lock = spin_lock_irqsave(&g_unix_lock);
    socket->refs++;
    spin_unlock_irqrestore(&g_unix_lock, lock);
}

/* Ownership changes only when a descriptor moves between processes.  A plain
 * store avoids taking the socket lock from the descriptor table path. */
void unix_set_owner(struct unix_socket *socket, struct process *process)
{
    if (socket)
        socket->owner = process;
}

int unix_bind(struct unix_socket *socket, const char *path, int length)
{
    if (!socket || !path || length <= 0 || length > UNIX_PATH_MAX)
        return -1;
    u64 lock = spin_lock_irqsave(&g_unix_lock);
    if (socket->state != UNIX_INIT || socket->path_len) {
        spin_unlock_irqrestore(&g_unix_lock, lock);
        return -1;
    }
    memcpy(socket->path, path, (usize)length);
    socket->path_len = length;
    spin_unlock_irqrestore(&g_unix_lock, lock);
    return 0;
}

int unix_listen(struct unix_socket *socket, int backlog)
{
    if (!socket)
        return -1;
    if (backlog <= 0)
        backlog = 1;
    if (backlog > UNIX_MAX_PENDING)
        backlog = UNIX_MAX_PENDING;

    u64 lock = spin_lock_irqsave(&g_unix_lock);
    if (socket->state != UNIX_INIT || socket->path_len <= 0) {
        spin_unlock_irqrestore(&g_unix_lock, lock);
        return -1;
    }
    if (listener_find(socket->path, socket->path_len)) {
        spin_unlock_irqrestore(&g_unix_lock, lock);
        return -1;
    }
    struct unix_listener *listener = NULL;
    for (int i = 0; i < UNIX_MAX_LISTENERS; i++) {
        if (!g_listeners[i].used) {
            listener = &g_listeners[i];
            break;
        }
    }
    if (!listener) {
        spin_unlock_irqrestore(&g_unix_lock, lock);
        return -1;
    }
    memset(listener, 0, sizeof(*listener));
    listener->used = 1;
    memcpy(listener->path, socket->path, (usize)socket->path_len);
    listener->path_len = socket->path_len;
    listener->socket = socket;
    socket->state = UNIX_LISTEN;
    socket->backlog = backlog;
    spin_unlock_irqrestore(&g_unix_lock, lock);
    return 0;
}

int unix_connect(struct unix_socket *socket, const char *path, int length,
                 int nonblock)
{
    if (!socket || !path || length <= 0 || length > UNIX_PATH_MAX)
        return -1;

    struct unix_socket *server = NULL;
    if (unix_create(&server) != 0)
        return -1;

    for (;;) {
        u64 lock = spin_lock_irqsave(&g_unix_lock);
        struct unix_listener *listener = listener_find(path, length);
        if (!listener || listener->socket->state != UNIX_LISTEN ||
            socket->state == UNIX_CONNECTED || socket->state == UNIX_CLOSED) {
            spin_unlock_irqrestore(&g_unix_lock, lock);
            unix_release(server);
            return -1;
        }
        if (listener->socket->pending_count >= listener->socket->backlog) {
            spin_unlock_irqrestore(&g_unix_lock, lock);
            if (nonblock) {
                unix_release(server);
                return -1;
            }
            thread_sleep_ms(1);
            continue;
        }

        server->state = UNIX_CONNECTED;
        server->owner = listener->socket->owner;
        server->peer = socket;
        socket->state = UNIX_CONNECTED;
        socket->peer = server;
        listener->socket->pending[listener->socket->pending_count++] = server;
        spin_unlock_irqrestore(&g_unix_lock, lock);
        return 0;
    }
}

struct unix_socket *unix_accept(struct unix_socket *socket, int nonblock)
{
    if (!socket)
        return NULL;
    for (;;) {
        u64 lock = spin_lock_irqsave(&g_unix_lock);
        if (socket->state != UNIX_LISTEN) {
            spin_unlock_irqrestore(&g_unix_lock, lock);
            return NULL;
        }
        if (socket->pending_count > 0) {
            struct unix_socket *accepted = socket->pending[0];
            for (int i = 1; i < socket->pending_count; i++)
                socket->pending[i - 1] = socket->pending[i];
            socket->pending_count--;
            accepted->owner = socket->owner;
            spin_unlock_irqrestore(&g_unix_lock, lock);
            return accepted;
        }
        spin_unlock_irqrestore(&g_unix_lock, lock);
        if (nonblock)
            return NULL;
        thread_sleep_ms(1);
    }
}

/* Append one complete message.  The ring must have room for all of it so a
 * reader never observes a half-written header. */
static int ring_append(struct unix_socket *peer, const void *data, u64 length)
{
    const u8 *source = data;
    for (u64 i = 0; i < length; i++) {
        peer->rx[peer->rx_head] = source[i];
        peer->rx_head = (peer->rx_head + 1) % UNIX_RX_SIZE;
    }
    peer->rx_bytes += (u32)length;
    peer->rx_written += length;
    return 0;
}

int unix_send_with_fds(struct unix_socket *socket, const void *data,
                       u64 length, const int *fds, int count, int nonblock)
{
    if (!socket || !data || length == 0 || length > UNIX_RX_SIZE)
        return -1;
    if (socket->state != UNIX_CONNECTED || !socket->peer)
        return -1;

    u64 lock = 0;
    for (;;) {
        lock = spin_lock_irqsave(&g_unix_lock);
        struct unix_socket *peer = socket->peer;
        if (!peer || peer->state == UNIX_CLOSED) {
            spin_unlock_irqrestore(&g_unix_lock, lock);
            return -1;
        }
        if (peer->rx_bytes + length <= UNIX_RX_SIZE &&
            peer->batch_count + (count ? 1 : 0) <= UNIX_MAX_BATCHES)
            break;
        spin_unlock_irqrestore(&g_unix_lock, lock);
        if (nonblock)
            return -1;
        thread_sleep_ms(1);
    }

    struct process *receiver = socket->peer->owner;
    int transferred[UNIX_MAX_FDS];
    int actual = 0;
    if (count > 0) {
        if (!receiver || count > UNIX_MAX_FDS) {
            spin_unlock_irqrestore(&g_unix_lock, lock);
            return -1;
        }
        for (int i = 0; i < count; i++) {
            int fd = zfd_transfer(receiver, fds[i]);
            if (fd < 0)
                break;
            transferred[actual++] = fd;
        }
        if (actual != count) {
            for (int i = 0; i < actual; i++)
                zfd_close(receiver, transferred[i]);
            spin_unlock_irqrestore(&g_unix_lock, lock);
            return -1;
        }
    }

    ring_append(socket->peer, data, length);
    if (actual > 0) {
        struct unix_socket *peer = socket->peer;
        struct unix_batch *batch = &peer->batches[peer->batch_count++];
        batch->at = peer->rx_written;
        batch->count = actual;
        for (int i = 0; i < actual; i++)
            batch->fds[i] = transferred[i];
    }
    spin_unlock_irqrestore(&g_unix_lock, lock);
    return (int)length;
}

int unix_send(struct unix_socket *socket, const void *data, u64 length,
              int nonblock)
{
    return unix_send_with_fds(socket, data, length, NULL, 0, nonblock);
}

int unix_recv_with_fds(struct unix_socket *socket, void *data, u64 capacity,
                       int *fds, int max_fds, int *fd_count, int nonblock)
{
    if (!socket || !data)
        return -1;
    for (;;) {
        u64 lock = spin_lock_irqsave(&g_unix_lock);
        if (socket->state == UNIX_CLOSED) {
            spin_unlock_irqrestore(&g_unix_lock, lock);
            return 0;
        }
        if (socket->rx_bytes > 0) {
            u64 want = capacity < socket->rx_bytes ? capacity : socket->rx_bytes;
            u8 *destination = data;
            for (u64 i = 0; i < want; i++) {
                destination[i] = socket->rx[socket->rx_tail];
                socket->rx_tail = (socket->rx_tail + 1) % UNIX_RX_SIZE;
            }
            socket->rx_bytes -= (u32)want;
            socket->rx_read += want;

            int produced = 0;
            while (socket->batch_count > 0 &&
                   socket->batches[0].at <= socket->rx_read) {
                struct unix_batch *batch = &socket->batches[0];
                for (int i = 0; i < batch->count; i++) {
                    if (produced < max_fds && fds) {
                        fds[produced++] = batch->fds[i];
                    } else if (socket->owner) {
                        /* Control buffer could not carry every descriptor;
                         * close the surplus instead of leaking it. */
                        zfd_close(socket->owner, batch->fds[i]);
                    }
                }
                for (int i = 1; i < socket->batch_count; i++)
                    socket->batches[i - 1] = socket->batches[i];
                socket->batch_count--;
            }
            if (fd_count)
                *fd_count = produced;
            spin_unlock_irqrestore(&g_unix_lock, lock);
            return (int)want;
        }
        int closed = !socket->peer || socket->peer->state == UNIX_CLOSED;
        spin_unlock_irqrestore(&g_unix_lock, lock);
        if (closed)
            return 0;
        if (nonblock)
            return -1;
        thread_sleep_ms(1);
    }
}

int unix_recv(struct unix_socket *socket, void *data, u64 capacity,
              int nonblock)
{
    return unix_recv_with_fds(socket, data, capacity, NULL, 0, NULL,
                              nonblock);
}

short unix_poll(struct unix_socket *socket, short events)
{
    if (!socket)
        return ZPOLLNVAL;
    short ready = 0;
    u64 lock = spin_lock_irqsave(&g_unix_lock);
    if (socket->state == UNIX_LISTEN) {
        if (socket->pending_count > 0)
            ready |= ZPOLLIN;
    } else if (socket->state == UNIX_CONNECTED) {
        if (socket->rx_bytes > 0)
            ready |= ZPOLLIN;
        if (!socket->peer || socket->peer->state == UNIX_CLOSED)
            ready |= ZPOLLHUP;
        else
            ready |= ZPOLLOUT;
    } else {
        ready |= ZPOLLHUP;
    }
    spin_unlock_irqrestore(&g_unix_lock, lock);
    return ready & (events | ZPOLLHUP | ZPOLLERR);
}

void unix_release(struct unix_socket *socket)
{
    if (!socket)
        return;

    struct unix_socket *orphans[UNIX_MAX_PENDING];
    int orphan_count = 0;

    u64 lock = spin_lock_irqsave(&g_unix_lock);
    if (!socket->used || socket->refs <= 0) {
        spin_unlock_irqrestore(&g_unix_lock, lock);
        return;
    }
    socket->refs--;
    if (socket->refs > 0) {
        spin_unlock_irqrestore(&g_unix_lock, lock);
        return;
    }

    if (socket->state == UNIX_LISTEN)
        listener_unregister(socket);
    if (socket->peer) {
        socket->peer->peer = NULL;
        socket->peer = NULL;
    }
    for (int i = 0; i < socket->pending_count; i++) {
        struct unix_socket *pending = socket->pending[i];
        if (pending->peer)
            pending->peer->peer = NULL;
        pending->peer = NULL;
        pending->state = UNIX_CLOSED;
        orphans[orphan_count++] = pending;
    }
    socket->pending_count = 0;
    socket->state = UNIX_CLOSED;
    u8 *rx = socket->rx;
    socket->rx = NULL;
    socket->used = 0;
    spin_unlock_irqrestore(&g_unix_lock, lock);

    kfree(rx);
    for (int i = 0; i < orphan_count; i++)
        unix_release(orphans[i]);
}
