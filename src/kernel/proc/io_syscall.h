#ifndef ZHAOXU_IO_SYSCALL_H
#define ZHAOXU_IO_SYSCALL_H

#include <types.h>

/* Descriptor-based I/O syscalls (socket, memfd, poll).  Implemented in
 * io_syscall.c; syscall.c only routes the numbers and owns the legacy VFS
 * surface. */
struct process;

long sys_ipc_socket(struct process *process, u64 domain, u64 type,
                    u64 protocol);
long sys_ipc_socketpair(struct process *process, u64 domain, u64 type,
                        u64 protocol, u64 descriptors);
long sys_ipc_bind(struct process *process, u64 fd, u64 address, u64 length);
long sys_ipc_listen(struct process *process, u64 fd, u64 backlog);
long sys_ipc_accept(struct process *process, u64 fd, u64 address,
                    u64 length_pointer);
long sys_ipc_connect(struct process *process, u64 fd, u64 address,
                     u64 length);
long sys_ipc_sendmsg(struct process *process, u64 fd, u64 message,
                     u64 flags);
long sys_ipc_recvmsg(struct process *process, u64 fd, u64 message,
                     u64 flags);
long sys_ipc_poll(struct process *process, u64 fds, u64 count, u64 timeout);
long sys_ipc_memfd_create(struct process *process, u64 name, u64 flags);
long sys_ipc_ftruncate(struct process *process, u64 fd, u64 size);
long sys_ipc_lseek(struct process *process, u64 fd, u64 offset, u64 whence);
long sys_ipc_mmap(struct process *process, u64 arguments);
long sys_ipc_munmap(struct process *process, u64 address, u64 length);
long sys_ipc_fcntl(struct process *process, u64 fd, u64 command,
                   u64 argument);
long sys_ipc_read(struct process *process, u64 fd, u64 buffer, u64 length);
long sys_ipc_write(struct process *process, u64 fd, u64 buffer, u64 length);
long sys_ipc_close(struct process *process, u64 fd);

#endif
