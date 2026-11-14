#ifndef ZHAOXU_UNIX_SOCKET_H
#define ZHAOXU_UNIX_SOCKET_H

#include <types.h>

/* AF_UNIX SOCK_STREAM endpoints.
 *
 * A connected pair is two endpoint objects.  Each endpoint owns a 64 KiB
 * receive ring; the peer appends to it.  SCM_RIGHTS descriptors are queued
 * with the byte offset at which they become readable, so recvmsg returns
 * them together with the message that carried them. */
#define UNIX_PATH_MAX 108
#define UNIX_MAX_FDS  8

struct process;
struct unix_socket;

int unix_create(struct unix_socket **out);
int unix_socketpair(struct unix_socket **first, struct unix_socket **second);
int unix_bind(struct unix_socket *socket, const char *path, int length);
int unix_listen(struct unix_socket *socket, int backlog);
int unix_connect(struct unix_socket *socket, const char *path, int length,
                 int nonblock);
struct unix_socket *unix_accept(struct unix_socket *socket, int nonblock);
int unix_send(struct unix_socket *socket, const void *data, u64 length,
              int nonblock);
int unix_recv(struct unix_socket *socket, void *data, u64 capacity,
              int nonblock);
int unix_send_with_fds(struct unix_socket *socket, const void *data,
                       u64 length, const int *fds, int count, int nonblock);
int unix_recv_with_fds(struct unix_socket *socket, void *data, u64 capacity,
                       int *fds, int max_fds, int *fd_count, int nonblock);
short unix_poll(struct unix_socket *socket, short events);
void unix_ref(struct unix_socket *socket);
void unix_release(struct unix_socket *socket);
void unix_set_owner(struct unix_socket *socket, struct process *process);

#endif
