#ifndef ZHAOXU_USERLIB_H
#define ZHAOXU_USERLIB_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2
#define SYS_OPEN   3
#define SYS_READ   4
#define SYS_CLOSE  5
#define SYS_UPTIME 6

static inline long syscall3(long number, long a, long b, long c)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return result;
}

static inline long sys_write(int fd, const void *buffer, u64 length)
{
    return syscall3(SYS_WRITE, fd, (long)buffer, (long)length);
}

static inline long sys_open(const char *path, int flags)
{
    return syscall3(SYS_OPEN, (long)path, flags, 0);
}

static inline long sys_read(int fd, void *buffer, u64 length)
{
    return syscall3(SYS_READ, fd, (long)buffer, (long)length);
}

static inline long sys_close(int fd)
{
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

static inline long sys_getpid(void)
{
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static inline long sys_uptime(void)
{
    return syscall3(SYS_UPTIME, 0, 0, 0);
}

static inline void sys_exit(int status)
{
    syscall3(SYS_EXIT, status, 0, 0);
    for (;;)
        __asm__ volatile("hlt");
}

void u_puts(const char *text);
void u_putn(long value);
void u_puthex(u64 value);
u64 u_strlen(const char *text);
int u_strcmp(const char *a, const char *b);

int user_main(long *stack);

#endif
