#ifndef ZHAOXU_MEMFD_H
#define ZHAOXU_MEMFD_H

#include <types.h>

/* Anonymous shared-memory file: ftruncate-sized, readable, writable and
 * mmap-able.  Wayland clients use it as the wl_shm backing store; 64 MiB
 * covers a 4K ARGB compositor buffer instead of the old 16 MiB cap. */
#define MEMFD_MAX_BYTES (64u * 1024u * 1024u)

struct process;
struct memfd;

int memfd_create(const char *name, struct memfd **out);
int memfd_truncate(struct memfd *memfd, u64 size);
int memfd_read(struct memfd *memfd, u64 *offset, void *buffer, u64 length);
int memfd_write(struct memfd *memfd, u64 *offset, const void *buffer,
                u64 length);
int memfd_seek(struct memfd *memfd, u64 *offset, i64 delta, int whence);
u64 memfd_mmap(struct memfd *memfd, struct process *process, u64 length,
               u64 offset);
int memfd_munmap(struct process *process, u64 address, u64 length);
short memfd_poll(struct memfd *memfd, short events);
u64 memfd_physical(struct memfd *memfd, u32 index);
void memfd_ref(struct memfd *memfd);
void memfd_release(struct memfd *memfd);
void memfd_process_exit(struct process *process);

#endif
