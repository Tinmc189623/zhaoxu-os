/*
 * Zhaoxu OS - scatter/gather reads and writes.
 *
 * The VFS syscalls move one contiguous buffer at a time, so these walk the
 * vector in order.  A short transfer stops the walk and returns the bytes
 * moved so far, which is what a caller of readv/writev must handle anyway.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

/* Write every vector element in order. */
ssize_t writev(int fd, const struct iovec *vectors, int count)
{
    if (!vectors || count <= 0) {
        errno = EINVAL;
        return -1;
    }
    ssize_t total = 0;
    for (int i = 0; i < count; i++) {
        const char *base = vectors[i].iov_base;
        size_t length = vectors[i].iov_len;
        while (length > 0) {
            ssize_t moved = write(fd, base, length);
            if (moved < 0)
                return total > 0 ? total : -1;
            if (moved == 0)
                return total;
            base += moved;
            length -= (size_t)moved;
            total += moved;
        }
    }
    return total;
}

/* Read across the vector until it is full or the descriptor ends. */
ssize_t readv(int fd, const struct iovec *vectors, int count)
{
    if (!vectors || count <= 0) {
        errno = EINVAL;
        return -1;
    }
    ssize_t total = 0;
    for (int i = 0; i < count; i++) {
        char *base = vectors[i].iov_base;
        size_t length = vectors[i].iov_len;
        while (length > 0) {
            ssize_t moved = read(fd, base, length);
            if (moved < 0)
                return total > 0 ? total : -1;
            if (moved == 0)
                return total;
            base += moved;
            length -= (size_t)moved;
            total += moved;
        }
    }
    return total;
}
