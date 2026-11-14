/*
 * Zhaoxu OS - descriptor I/O syscalls for the Wayland transport.
 *
 * The user ABI mirrors the Linux x86_64 layouts for sockaddr_un, iovec,
 * msghdr and cmsghdr so a stock libwayland client can use its own headers
 * once the rest of the environment exists.  Descriptors above ZFD_BASE name
 * socket or memfd objects; the legacy VFS descriptors stay untouched.
 */
#include "process_internal.h"
#include "io_syscall.h"

#include <epoll.h>
#include <eventfd.h>
#include <mem.h>
#include <memfd.h>
#include <poll.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <timerfd.h>
#include <types.h>
#include <unix_socket.h>
#include <zfd.h>

#define ZAF_UNIX 1
#define ZSOCK_STREAM 1
#define ZSOL_SOCKET 1
#define ZSCM_RIGHTS 1

#define ZF_DUPFD 0
#define ZF_GETFD 1
#define ZF_SETFD 2
#define ZF_GETFL 3
#define ZF_SETFL 4

#define ZO_NONBLOCK 0x800
#define ZMSG_DONTWAIT 0x40

#define ZEFD_SEMAPHORE 1
#define ZTFD_TIMER_ABSTIME 1
#define ZEPOLL_CTL_ADD 1
#define ZEPOLL_CTL_DEL 2
#define ZEPOLL_CTL_MOD 3

struct ztimespec {
    i64 seconds;
    i64 nanoseconds;
};

struct zitimerspec {
    struct ztimespec interval;
    struct ztimespec value;
};

struct zepoll_event {
    u32 events;
    u64 data;
} __attribute__((packed));

#define ZMSG_MAX 65536u
#define ZIOV_MAX 16
#define ZCMSG_MAX 256u
#define ZCMSG_ALIGN(value) (((value) + 7u) & ~7u)

struct ziovec {
    void *base;
    u64 length;
};

struct zmsghdr {
    void *name;
    u32 name_length;
    u32 padding0;
    struct ziovec *iov;
    u64 iov_length;
    void *control;
    u64 control_length;
    u32 flags;
    u32 padding1;
};

struct zcmsghdr {
    u64 length;
    int level;
    int type;
};

struct zmmap_arguments {
    u64 address;
    u64 length;
    u64 protection;
    u64 flags;
    i64 fd;
    u64 offset;
};

static int copy_from_user(struct process *process, void *destination,
                          u64 source, u64 length)
{
    if (user_range_ok(process, source, length) != 1)
        return -1;
    if (length)
        memcpy(destination, (const void *)(uintptr_t)source, (usize)length);
    return 0;
}

static int copy_string_from_user(struct process *process, char *destination,
                                 usize capacity, u64 source)
{
    if (!source || capacity == 0)
        return -1;
    for (usize i = 0; i < capacity; i++) {
        if (user_range_ok(process, source + i, 1) != 1)
            return -1;
        char character = *(const char *)(uintptr_t)(source + i);
        destination[i] = character;
        if (character == 0)
            return 0;
    }
    destination[capacity - 1] = 0;
    return 0;
}

/* Parse a sockaddr_un: 16-bit family followed by the path bytes.  A
 * filesystem path ends at the first NUL; an abstract path starts with NUL
 * and keeps its full length. */
static int copy_sockaddr(struct process *process, u64 address, u64 length,
                         char *path, int *path_length)
{
    if (length < 2 || length > 2 + UNIX_PATH_MAX)
        return -1;
    u8 raw[2 + UNIX_PATH_MAX];
    if (copy_from_user(process, raw, address, length) != 0)
        return -1;

    u16 family;
    memcpy(&family, raw, sizeof(family));
    if (family != ZAF_UNIX)
        return -1;

    int count = (int)(length - 2);
    const char *name = (const char *)(raw + 2);
    if (name[0] != 0) {
        int end = 0;
        while (end < count && name[end])
            end++;
        count = end;
    }
    if (count <= 0 || count > UNIX_PATH_MAX)
        return -1;
    memcpy(path, name, (usize)count);
    *path_length = count;
    return 0;
}

long sys_ipc_socket(struct process *process, u64 domain, u64 type,
                    u64 protocol)
{
    if (!process || domain != ZAF_UNIX || type != ZSOCK_STREAM ||
        protocol != 0)
        return -1;
    struct unix_socket *socket = NULL;
    if (unix_create(&socket) != 0)
        return -1;
    int fd = zfd_alloc(process, ZFD_TYPE_SOCKET, socket, 0);
    if (fd < 0) {
        unix_release(socket);
        return -1;
    }
    unix_set_owner(socket, process);
    return fd;
}

long sys_ipc_socketpair(struct process *process, u64 domain, u64 type,
                        u64 protocol, u64 descriptors)
{
    if (!process || domain != ZAF_UNIX || type != ZSOCK_STREAM ||
        protocol != 0)
        return -1;
    if (user_range_ok(process, descriptors, 2 * sizeof(int)) != 1)
        return -1;

    struct unix_socket *first = NULL;
    struct unix_socket *second = NULL;
    if (unix_socketpair(&first, &second) != 0)
        return -1;

    int fd_first = zfd_alloc(process, ZFD_TYPE_SOCKET, first, 0);
    if (fd_first < 0) {
        unix_release(first);
        unix_release(second);
        return -1;
    }
    int fd_second = zfd_alloc(process, ZFD_TYPE_SOCKET, second, 0);
    if (fd_second < 0) {
        zfd_close(process, fd_first);
        unix_release(second);
        return -1;
    }

    int values[2] = { fd_first, fd_second };
    if (process_user_write(process, descriptors, values,
                           sizeof(values)) != 0) {
        zfd_close(process, fd_first);
        zfd_close(process, fd_second);
        return -1;
    }
    return 0;
}

long sys_ipc_bind(struct process *process, u64 fd, u64 address, u64 length)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_SOCKET)
        return -1;
    char path[UNIX_PATH_MAX];
    int path_length = 0;
    if (copy_sockaddr(process, address, length, path, &path_length) != 0)
        return -1;
    return unix_bind((struct unix_socket *)entry->object, path, path_length);
}

long sys_ipc_listen(struct process *process, u64 fd, u64 backlog)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_SOCKET)
        return -1;
    return unix_listen((struct unix_socket *)entry->object, (int)backlog);
}

long sys_ipc_connect(struct process *process, u64 fd, u64 address,
                     u64 length)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_SOCKET)
        return -1;
    char path[UNIX_PATH_MAX];
    int path_length = 0;
    if (copy_sockaddr(process, address, length, path, &path_length) != 0)
        return -1;
    int nonblock = (entry->flags & ZFD_O_NONBLOCK) != 0;
    return unix_connect((struct unix_socket *)entry->object, path,
                        path_length, nonblock);
}

long sys_ipc_accept(struct process *process, u64 fd, u64 address,
                    u64 length_pointer)
{
    (void)address;
    (void)length_pointer;
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_SOCKET)
        return -1;
    int nonblock = (entry->flags & ZFD_O_NONBLOCK) != 0;
    struct unix_socket *accepted =
        unix_accept((struct unix_socket *)entry->object, nonblock);
    if (!accepted)
        return -1;
    int new_fd = zfd_alloc(process, ZFD_TYPE_SOCKET, accepted, 0);
    if (new_fd < 0) {
        unix_release(accepted);
        return -1;
    }
    unix_set_owner(accepted, process);
    return new_fd;
}

long sys_ipc_sendmsg(struct process *process, u64 fd, u64 message,
                     u64 flags)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_SOCKET)
        return -1;

    struct zmsghdr header;
    if (copy_from_user(process, &header, message, sizeof(header)) != 0)
        return -1;
    if (header.iov_length > ZIOV_MAX)
        return -1;

    struct ziovec iov[ZIOV_MAX];
    u64 total = 0;
    for (u64 i = 0; i < header.iov_length; i++) {
        if (copy_from_user(process, &iov[i],
                           (u64)(uintptr_t)&header.iov[i],
                           sizeof(struct ziovec)) != 0)
            return -1;
        if (iov[i].length &&
            user_range_ok(process, (u64)(uintptr_t)iov[i].base,
                          iov[i].length) != 1)
            return -1;
        total += iov[i].length;
        if (total > ZMSG_MAX)
            return -1;
    }
    if (total == 0)
        return -1;

    u8 *data = kmalloc(total);
    if (!data)
        return -1;
    u64 written = 0;
    for (u64 i = 0; i < header.iov_length; i++) {
        if (iov[i].length) {
            memcpy(data + written, iov[i].base, (usize)iov[i].length);
            written += iov[i].length;
        }
    }

    int fds[UNIX_MAX_FDS];
    int fd_count = 0;
    if (header.control_length > ZCMSG_MAX) {
        kfree(data);
        return -1;
    }
    if (header.control_length) {
        u8 control[ZCMSG_MAX];
        if (copy_from_user(process, control, (u64)(uintptr_t)header.control,
                           header.control_length) != 0) {
            kfree(data);
            return -1;
        }
        u64 at = 0;
        while (at + sizeof(struct zcmsghdr) <= header.control_length) {
            struct zcmsghdr cmsg;
            memcpy(&cmsg, control + at, sizeof(cmsg));
            /* Length is user-controlled: compare against the remaining
             * bytes, not at + length, so a wrapped addition cannot walk
             * the parser outside the copied control buffer. */
            u64 remaining = header.control_length - at;
            if (cmsg.length < sizeof(cmsg) || cmsg.length > remaining)
                break;
            if (cmsg.level == ZSOL_SOCKET && cmsg.type == ZSCM_RIGHTS) {
                u64 payload = cmsg.length - sizeof(cmsg);
                u64 count = payload / sizeof(int);
                const int *values = (const int *)(control + at + sizeof(cmsg));
                for (u64 i = 0; i < count && fd_count < UNIX_MAX_FDS; i++)
                    fds[fd_count++] = values[i];
            }
            at += ZCMSG_ALIGN(cmsg.length);
        }
    }

    int nonblock = (entry->flags & ZFD_O_NONBLOCK) != 0 ||
                   (flags & ZMSG_DONTWAIT) != 0;
    long result = unix_send_with_fds((struct unix_socket *)entry->object,
                                     data, total, fds, fd_count, nonblock);
    kfree(data);
    return result;
}

long sys_ipc_recvmsg(struct process *process, u64 fd, u64 message,
                     u64 flags)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_SOCKET)
        return -1;

    struct zmsghdr header;
    if (copy_from_user(process, &header, message, sizeof(header)) != 0)
        return -1;
    if (header.iov_length > ZIOV_MAX)
        return -1;

    struct ziovec iov[ZIOV_MAX];
    u64 capacity = 0;
    for (u64 i = 0; i < header.iov_length; i++) {
        if (copy_from_user(process, &iov[i],
                           (u64)(uintptr_t)&header.iov[i],
                           sizeof(struct ziovec)) != 0)
            return -1;
        if (iov[i].length &&
            user_range_ok(process, (u64)(uintptr_t)iov[i].base,
                          iov[i].length) != 1)
            return -1;
        capacity += iov[i].length;
        if (capacity > ZMSG_MAX)
            return -1;
    }

    u8 *data = capacity ? kmalloc(capacity) : NULL;
    if (capacity && !data)
        return -1;

    int fds[UNIX_MAX_FDS];
    int fd_count = 0;
    int nonblock = (entry->flags & ZFD_O_NONBLOCK) != 0 ||
                   (flags & ZMSG_DONTWAIT) != 0;
    long got = unix_recv_with_fds((struct unix_socket *)entry->object, data,
                                  capacity, fds, UNIX_MAX_FDS, &fd_count,
                                  nonblock);
    if (got < 0) {
        kfree(data);
        return -1;
    }

    u64 copied = 0;
    for (u64 i = 0; i < header.iov_length && copied < (u64)got; i++) {
        u64 chunk = iov[i].length;
        if (chunk > (u64)got - copied)
            chunk = (u64)got - copied;
        if (chunk &&
            process_user_write(process, (u64)(uintptr_t)iov[i].base,
                               data + copied, chunk) != 0) {
            kfree(data);
            return -1;
        }
        copied += chunk;
    }
    kfree(data);

    u64 control_capacity = header.control_length;
    header.name_length = 0;
    header.flags = 0;
    header.control_length = 0;
    if (fd_count > 0) {
        u64 needed = ZCMSG_ALIGN(sizeof(struct zcmsghdr) +
                                 (u64)fd_count * sizeof(int));
        if (!header.control || control_capacity < needed) {
            for (int i = 0; i < fd_count; i++)
                zfd_close(process, fds[i]);
        } else {
            u8 control[ZCMSG_MAX];
            memset(control, 0, sizeof(control));
            struct zcmsghdr cmsg;
            cmsg.length = sizeof(cmsg) + (u64)fd_count * sizeof(int);
            cmsg.level = ZSOL_SOCKET;
            cmsg.type = ZSCM_RIGHTS;
            memcpy(control, &cmsg, sizeof(cmsg));
            memcpy(control + sizeof(cmsg), fds,
                   (usize)fd_count * sizeof(int));
            if (process_user_write(process, (u64)(uintptr_t)header.control,
                                   control, needed) != 0) {
                for (int i = 0; i < fd_count; i++)
                    zfd_close(process, fds[i]);
            } else {
                header.control_length = needed;
            }
        }
    }
    process_user_write(process, message, &header, sizeof(header));
    return got;
}

long sys_ipc_poll(struct process *process, u64 fds, u64 count, u64 timeout)
{
    if (!process || count == 0 || count > 64)
        return -1;
    if (user_range_ok(process, fds, count * sizeof(struct zpollfd)) != 1)
        return -1;

    struct zpollfd items[64];
    if (copy_from_user(process, items, fds,
                       count * sizeof(struct zpollfd)) != 0)
        return -1;

    i64 limit = (i64)timeout;
    u64 start = timer_ticks();
    u64 deadline = 0;
    if (limit > 0)
        deadline = start + ((u64)limit * timer_hz() + 999) / 1000;

    for (;;) {
        int ready = 0;
        for (u64 i = 0; i < count; i++) {
            items[i].revents = zfd_poll_events(process, items[i].fd,
                                               items[i].events);
            if (items[i].revents)
                ready++;
        }
        if (ready > 0 || limit == 0)
            break;
        if (limit > 0 && timer_ticks() >= deadline)
            break;
        thread_sleep_ms(1);
    }

    if (process_user_write(process, fds, items,
                           count * sizeof(struct zpollfd)) != 0)
        return -1;
    int ready = 0;
    for (u64 i = 0; i < count; i++) {
        if (items[i].revents)
            ready++;
    }
    return ready;
}

long sys_ipc_memfd_create(struct process *process, u64 name, u64 flags)
{
    (void)flags;
    char label[32];
    memset(label, 0, sizeof(label));
    if (name)
        copy_string_from_user(process, label, sizeof(label), name);

    struct memfd *memfd = NULL;
    if (memfd_create(label, &memfd) != 0)
        return -1;
    int fd = zfd_alloc(process, ZFD_TYPE_MEMFD, memfd, 0);
    if (fd < 0) {
        memfd_release(memfd);
        return -1;
    }
    return fd;
}

long sys_ipc_ftruncate(struct process *process, u64 fd, u64 size)
{
    return zfd_ftruncate(process, (int)fd, size);
}

long sys_ipc_lseek(struct process *process, u64 fd, u64 offset, u64 whence)
{
    return zfd_lseek(process, (int)fd, (i64)offset, (int)whence);
}

long sys_ipc_mmap(struct process *process, u64 arguments)
{
    struct zmmap_arguments args;
    if (copy_from_user(process, &args, arguments, sizeof(args)) != 0)
        return -1;
    if (args.length == 0 || args.fd < ZFD_BASE)
        return -1;
    struct zfd_entry *entry = zfd_get(process, (int)args.fd);
    if (!entry || entry->type != ZFD_TYPE_MEMFD)
        return -1;
    u64 address = memfd_mmap((struct memfd *)entry->object, process,
                             args.length, args.offset);
    return address ? (long)address : -1;
}

long sys_ipc_munmap(struct process *process, u64 address, u64 length)
{
    return memfd_munmap(process, address, length);
}

long sys_ipc_fcntl(struct process *process, u64 fd, u64 command, u64 argument)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry)
        return -1;
    switch (command) {
    case ZF_DUPFD:
        return zfd_dup(process, (int)fd, (int)argument);
    case ZF_GETFD:
    case ZF_SETFD:
        return 0;
    case ZF_GETFL:
        return (long)entry->flags;
    case ZF_SETFL:
        return zfd_set_nonblock(process, (int)fd,
                                (argument & ZO_NONBLOCK) != 0);
    default:
        return -1;
    }
}

long sys_ipc_read(struct process *process, u64 fd, u64 buffer, u64 length)
{
    if (length > ZMSG_MAX)
        length = ZMSG_MAX;
    if (user_range_ok(process, buffer, length) != 1)
        return -1;
    return zfd_read(process, (int)fd, (void *)(uintptr_t)buffer, length);
}

long sys_ipc_write(struct process *process, u64 fd, u64 buffer, u64 length)
{
    if (length > ZMSG_MAX)
        length = ZMSG_MAX;
    if (user_range_ok(process, buffer, length) != 1)
        return -1;
    return zfd_write(process, (int)fd, (const void *)(uintptr_t)buffer,
                     length);
}

long sys_ipc_close(struct process *process, u64 fd)
{
    return zfd_close(process, (int)fd);
}

/* Create an eventfd.  EFD_SEMAPHORE selects one-per-read semantics and
 * EFD_NONBLOCK is stored in the descriptor flags for read/write. */
long sys_ipc_eventfd(struct process *process, u64 initial, u64 flags)
{
    struct eventfd *event =
        eventfd_create((u32)initial, (flags & ZEFD_SEMAPHORE) != 0);
    if (!event)
        return -1;
    int fd = zfd_alloc(process, ZFD_TYPE_EVENTFD, event,
                       (flags & ZO_NONBLOCK) ? ZFD_O_NONBLOCK : 0);
    if (fd < 0) {
        eventfd_release(event);
        return -1;
    }
    return fd;
}

/* Create a timerfd.  Both clock ids use the scheduler tick source. */
long sys_ipc_timerfd_create(struct process *process, u64 clock_id, u64 flags)
{
    struct timerfd *timer = timerfd_create((u32)clock_id);
    if (!timer)
        return -1;
    int fd = zfd_alloc(process, ZFD_TYPE_TIMERFD, timer,
                       (flags & ZO_NONBLOCK) ? ZFD_O_NONBLOCK : 0);
    if (fd < 0) {
        timerfd_release(timer);
        return -1;
    }
    return fd;
}

/* Arm or disarm a timerfd.  The user passes the Linux itimerspec layout; the
 * old value is optional and written back when the pointer is non-zero. */
long sys_ipc_timerfd_settime(struct process *process, u64 fd, u64 flags,
                             u64 new_value, u64 old_value)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_TIMERFD)
        return -1;
    if (!new_value)
        return -1;

    struct zitimerspec value;
    if (copy_from_user(process, &value, new_value, sizeof(value)) != 0)
        return -1;

    struct timerfd_spec new_spec = { value.value.seconds,
                                     value.value.nanoseconds };
    struct timerfd_spec new_interval = { value.interval.seconds,
                                         value.interval.nanoseconds };
    struct timerfd_spec old_spec;
    struct timerfd_spec old_interval;
    if (timerfd_settime((struct timerfd *)entry->object,
                        (flags & ZTFD_TIMER_ABSTIME) != 0, new_spec,
                        new_interval, &old_spec, &old_interval) != 0)
        return -1;

    if (old_value) {
        struct zitimerspec result;
        result.interval.seconds = old_interval.seconds;
        result.interval.nanoseconds = old_interval.nanoseconds;
        result.value.seconds = old_spec.seconds;
        result.value.nanoseconds = old_spec.nanoseconds;
        if (process_user_write(process, old_value, &result, sizeof(result)) !=
            0)
            return -1;
    }
    return 0;
}

/* Read the remaining time and interval of a timerfd. */
long sys_ipc_timerfd_gettime(struct process *process, u64 fd, u64 current)
{
    struct zfd_entry *entry = zfd_get(process, (int)fd);
    if (!entry || entry->type != ZFD_TYPE_TIMERFD || !current)
        return -1;
    if (user_range_ok(process, current, sizeof(struct zitimerspec)) != 1)
        return -1;

    struct timerfd_spec value;
    struct timerfd_spec interval;
    if (timerfd_gettime((struct timerfd *)entry->object, &value, &interval) !=
        0)
        return -1;

    struct zitimerspec result;
    result.interval.seconds = interval.seconds;
    result.interval.nanoseconds = interval.nanoseconds;
    result.value.seconds = value.seconds;
    result.value.nanoseconds = value.nanoseconds;
    return process_user_write(process, current, &result, sizeof(result)) == 0
               ? 0
               : -1;
}

/* Create an epoll registration table. */
long sys_ipc_epoll_create1(struct process *process, u64 flags)
{
    (void)flags;
    struct epoll *epoll = epoll_create();
    if (!epoll)
        return -1;
    int fd = zfd_alloc(process, ZFD_TYPE_EPOLL, epoll, 0);
    if (fd < 0) {
        epoll_release(epoll);
        return -1;
    }
    return fd;
}

/* Add, modify or remove an epoll registration. */
long sys_ipc_epoll_ctl(struct process *process, u64 epfd, u64 op, u64 fd,
                       u64 event_pointer)
{
    struct zfd_entry *entry = zfd_get(process, (int)epfd);
    if (!entry || entry->type != ZFD_TYPE_EPOLL)
        return -1;

    struct zepoll_event event;
    memset(&event, 0, sizeof(event));
    if (op != ZEPOLL_CTL_DEL) {
        if (!event_pointer ||
            copy_from_user(process, &event, event_pointer, sizeof(event)) != 0)
            return -1;
    }
    return epoll_ctl(process, (struct epoll *)entry->object, (int)op, (int)fd,
                     event.events, event.data);
}

/* Wait for registered descriptors and copy the ready events back to user
 * memory.  The kernel event layout is the packed 12-byte Linux ABI. */
long sys_ipc_epoll_wait(struct process *process, u64 epfd, u64 events,
                        u64 max_events, u64 timeout)
{
    struct zfd_entry *entry = zfd_get(process, (int)epfd);
    if (!entry || entry->type != ZFD_TYPE_EPOLL)
        return -1;
    if (!events || max_events == 0)
        return -1;
    if (max_events > EPOLL_MAX_ENTRIES)
        max_events = EPOLL_MAX_ENTRIES;
    if (user_range_ok(process, events,
                      max_events * sizeof(struct zepoll_event)) != 1)
        return -1;

    struct zepoll_event output[EPOLL_MAX_ENTRIES];
    memset(output, 0, sizeof(output));
    int ready = epoll_wait(process, (struct epoll *)entry->object,
                           (struct epoll_result *)output, (int)max_events,
                           (int)(i64)timeout);
    if (ready < 0)
        return -1;
    if (ready == 0)
        return 0;
    if (process_user_write(process, events, output,
                           (u64)ready * sizeof(struct zepoll_event)) != 0)
        return -1;
    return ready;
}
