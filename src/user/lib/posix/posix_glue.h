#ifndef ZOS_POSIX_GLUE_H
#define ZOS_POSIX_GLUE_H

/* Shared ZOS syscall numbers and the int 0x80 helper used by the newlib
 * libgloss files.  Numbers must match kernel/proc/syscall.c. */
#include <stdint.h>

#define ZOS_EXIT     0
#define ZOS_WRITE    1
#define ZOS_GETPID   2
#define ZOS_OPEN     3
#define ZOS_READ     4
#define ZOS_CLOSE    5
#define ZOS_UPTIME   6
#define ZOS_WAIT     8
#define ZOS_SLEEP    22
#define ZOS_CPUS     23
#define ZOS_RTC      24
#define ZOS_READDIR  29
#define ZOS_UNLINK   33
#define ZOS_SOCKET   43
#define ZOS_BIND     44
#define ZOS_LISTEN   45
#define ZOS_ACCEPT   46
#define ZOS_CONNECT  47
#define ZOS_SENDMSG  48
#define ZOS_RECVMSG  49
#define ZOS_POLL     50
#define ZOS_MEMFD_CREATE 51
#define ZOS_FTRUNCATE 52
#define ZOS_LSEEK    53
#define ZOS_MMAP     54
#define ZOS_MUNMAP   55
#define ZOS_FCNTL    56
#define ZOS_EPOLL_CREATE1 65
#define ZOS_EPOLL_CTL 66
#define ZOS_EPOLL_WAIT 67
#define ZOS_REBOOT   69
#define ZOS_FORK     72
#define ZOS_EXEC     73
#define ZOS_PIPE     74
#define ZOS_DUP2     75
#define ZOS_IOCTL    76
#define ZOS_STAT     77
#define ZOS_GETPPID  78
#define ZOS_KLOG     81
#define ZOS_MKDIR    82
#define ZOS_RMDIR    83
#define ZOS_RENAME   84
#define ZOS_TCGETS   0x5401
#define ZOS_SBRK_BYTES (16u * 1024u * 1024u)

/* Kernel-side argument blocks shared with kernel/proc/io_syscall.c. */
struct zos_mmap_arguments {
    uint64_t address;
    uint64_t length;
    uint64_t protection;
    uint64_t flags;
    int64_t fd;
    uint64_t offset;
};

struct zos_epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

struct zos_rtc_time {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
};

static inline long zos_syscall3(long number, long a, long b, long c)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return result;
}

static inline long zos_syscall4(long number, long a, long b, long c, long d)
{
    register long r10 __asm__("r10") = d;
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "rcx", "r11", "memory");
    return result;
}

#endif
