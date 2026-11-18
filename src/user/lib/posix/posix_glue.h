#ifndef ZOS_POSIX_GLUE_H
#define ZOS_POSIX_GLUE_H

/* Shared ZOS syscall numbers and the int 0x80 helper used by the newlib
 * libgloss files.  Numbers must match kernel/proc/syscall.c. */
#include <stdint.h>

#define ZOS_EXIT     0
#define ZOS_WRITE    1
#define ZOS_GETPID   2
#define ZOS_SLEEP    22
#define ZOS_POLL     50
#define ZOS_GETPPID  78
#define ZOS_OPEN     3
#define ZOS_READ     4
#define ZOS_CLOSE    5
#define ZOS_WAIT     8
#define ZOS_READDIR  29
#define ZOS_UNLINK   33
#define ZOS_LSEEK    53
#define ZOS_FORK     72
#define ZOS_EXEC     73
#define ZOS_PIPE     74
#define ZOS_DUP2     75
#define ZOS_IOCTL    76
#define ZOS_STAT     77
#define ZOS_TCGETS   0x5401
#define ZOS_SBRK_BYTES (16u * 1024u * 1024u)

static inline long zos_syscall3(long number, long a, long b, long c)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return result;
}

#endif
