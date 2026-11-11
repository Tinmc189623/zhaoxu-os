/*
 * ext4 host harness - block I/O, memory and logging for the OS-independent
 * ext4 core in src/fs/ext4.
 *
 * The harness hands the core one regular file as disk 0.  Every core accessor
 * passes the volume base and the in-volume offset as two separate arguments,
 * so both are added here: a volume formatted at a non-zero LBA reads and
 * writes exactly the bytes that belong to it.
 */
#define _GNU_SOURCE
#include "ext4_env.h"
#include "ext4.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fd = -1;

static int host_read(int disk, u64 base, u64 offset, void *buffer, u32 length)
{
    (void)disk;
    return pread(g_fd, buffer, length, (off_t)(base + offset)) ==
                   (ssize_t)length
               ? 0
               : -1;
}

static int host_write(int disk, u64 base, u64 offset, const void *buffer,
                      u32 length)
{
    (void)disk;
    return pwrite(g_fd, buffer, length, (off_t)(base + offset)) ==
                   (ssize_t)length
               ? 0
               : -1;
}

static int host_flush(int disk)
{
    (void)disk;
    return fsync(g_fd) == 0 ? 0 : -1;
}

static void *host_alloc(u32 bytes)
{
    return calloc(1, bytes);
}

static void host_free(void *pointer)
{
    free(pointer);
}

static void host_log(const char *text)
{
    fputs(text, stderr);
}

static int host_disks(void)
{
    return 1;
}

static u64 host_sectors(int disk)
{
    (void)disk;
    return 0;
}

int host_env_open(const char *image)
{
    g_fd = open(image, O_RDWR);
    if (g_fd < 0)
        return -1;
    struct ext4_env env = {
        .read = host_read,
        .write = host_write,
        .flush = host_flush,
        .alloc = host_alloc,
        .free = host_free,
        .log = host_log,
        .disk_count = host_disks,
        .disk_sectors = host_sectors,
    };
    ext4_env_set(&env);
    return 0;
}

/* Host side of <kprintf.h>: the core only formats its log lines through
 * kvsnprintf, the other two exist so the header's prototypes stay honest. */
int kvsnprintf(char *out, usize capacity, const char *format, va_list args)
{
    return vsnprintf(out, capacity, format, args);
}

int ksnprintf(char *out, usize capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vsnprintf(out, capacity, format, args);
    va_end(args);
    return result;
}

int kprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vfprintf(stdout, format, args);
    va_end(args);
    return result;
}
