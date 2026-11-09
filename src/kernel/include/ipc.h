#ifndef ZHAOXU_IPC_H
#define ZHAOXU_IPC_H

#include <types.h>

#define IPC_MSG_MAX 64
#define IPC_QUEUE_DEPTH 64
#define IPC_NAME_MAX 32

u64 ipc_listen(const char *name);
u64 ipc_connect(const char *name);
u64 ipc_accept(u64 port);
int ipc_try_accept(u64 port, u64 *channel);
int ipc_send(u64 channel, u64 type, const void *data, u32 length);
int ipc_recv(u64 channel, u64 *type, void *data, u32 capacity);
int ipc_try_recv(u64 channel, u64 *type, void *data, u32 capacity);
int ipc_close(u64 handle);
void ipc_process_exit(u64 pid);

#endif
