/*
 * Zhaoxu OS - mapping, sizing and durability calls.
 *
 * mmap is backed by the kernel's memfd objects: a descriptor that already
 * denotes a memfd can be mapped, and anonymous mappings are refused because
 * the kernel has no anonymous-memory entry point.  Descriptor writes land
 * in the VFS immediately, so fsync is a synchronous no-op by construction.
 */
#include "posix_glue.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

static uint64_t page_round(uint64_t value)
{
    return (value + 4095u) & ~(uint64_t)4095u;
}

/* Map a memfd descriptor; the VFS descriptor range cannot be mapped. */
void *mmap(void *address, size_t length, int protection, int flags, int fd,
           off_t offset)
{
    (void)address;
    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (flags & MAP_ANONYMOUS) {
        errno = ENODEV;
        return MAP_FAILED;
    }
    struct zos_mmap_arguments arguments;
    arguments.address = 0;
    arguments.length = page_round(length);
    arguments.protection = (uint64_t)protection;
    arguments.flags = (uint64_t)flags;
    arguments.fd = fd;
    arguments.offset = (uint64_t)offset;
    long result = zos_syscall3(ZOS_MMAP, (long)(uintptr_t)&arguments, 0, 0);
    if (result < 0) {
        errno = ENODEV;
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)result;
}

int munmap(void *address, size_t length)
{
    long result = zos_syscall3(ZOS_MUNMAP, (long)(uintptr_t)address,
                               (long)length, 0);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int ftruncate(int fd, off_t length)
{
    long result = zos_syscall3(ZOS_FTRUNCATE, fd, (long)length, 0);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* Preallocation is a resize for a memfd; other descriptors stay untouched. */
int posix_fallocate(int fd, off_t offset, off_t length)
{
    if (offset < 0 || length <= 0)
        return EINVAL;
    long result = zos_syscall3(ZOS_FTRUNCATE, fd, (long)(offset + length), 0);
    return result < 0 ? EINVAL : 0;
}

/* Writes are committed to the VFS before they return. */
int fsync(int fd)
{
    (void)fd;
    return 0;
}

int fdatasync(int fd)
{
    return fsync(fd);
}

void sync(void)
{
}

int flock(int fd, int operation)
{
    (void)fd;
    (void)operation;
    errno = ENOSYS;
    return -1;
}

/* Every page is resident; there is no swap to lock against. */
int mlock(const void *address, size_t length)
{
    (void)address;
    (void)length;
    return 0;
}

int mlockall(int flags)
{
    (void)flags;
    return 0;
}

int munlock(const void *address, size_t length)
{
    (void)address;
    (void)length;
    return 0;
}

int munlockall(void)
{
    return 0;
}
