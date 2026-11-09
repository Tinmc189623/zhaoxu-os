#ifndef ZHAOXU_USERLIB_H
#define ZHAOXU_USERLIB_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2
#define SYS_OPEN   3
#define SYS_READ   4
#define SYS_CLOSE  5
#define SYS_UPTIME 6
#define SYS_SPAWN       7
#define SYS_WAIT        8
#define SYS_PORT_LISTEN 9
#define SYS_PORT_CONNECT 10
#define SYS_PORT_ACCEPT 11
#define SYS_PORT_SEND   12
#define SYS_PORT_RECV   13
#define SYS_PORT_CLOSE  14
#define SYS_SHM_CREATE  15
#define SYS_SHM_MAP     16
#define SYS_SHM_UNMAP   17
#define SYS_SHM_CLOSE   18
#define SYS_FB_MAP      19
#define SYS_MOUSE       20
#define SYS_KEY         21
#define SYS_SLEEP       22
#define SYS_CPUS        23
#define SYS_RTC         24
#define SYS_READ_PATH   25
#define SYS_PORT_TRY_ACCEPT 26
#define SYS_PORT_TRY_RECV   27

struct zos_fb_info {
    unsigned long address;
    unsigned long width;
    unsigned long height;
    unsigned long stride;
    unsigned long format;
};

static inline long syscall3(long number, long a, long b, long c)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return result;
}

static inline long syscall4(long number, long a, long b, long c, long d)
{
    register long r10 __asm__("r10") = d;
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10)
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

static inline long sys_spawn(const char *path)
{
    return syscall3(SYS_SPAWN, (long)path, 0, 0);
}

static inline long sys_wait(long pid, int *status)
{
    return syscall3(SYS_WAIT, pid, (long)status, 0);
}

static inline long sys_port_listen(const char *name)
{
    return syscall3(SYS_PORT_LISTEN, (long)name, 0, 0);
}

static inline long sys_port_connect(const char *name)
{
    return syscall3(SYS_PORT_CONNECT, (long)name, 0, 0);
}

static inline long sys_port_accept(long port)
{
    return syscall3(SYS_PORT_ACCEPT, port, 0, 0);
}

static inline long sys_port_send(long channel, long type, const void *data,
                                 long length)
{
    return syscall4(SYS_PORT_SEND, channel, type, (long)data, length);
}

static inline long sys_port_recv(long channel, unsigned long *type, void *data,
                                 long capacity)
{
    return syscall4(SYS_PORT_RECV, channel, (long)type, (long)data, capacity);
}

static inline long sys_port_close(long handle)
{
    return syscall3(SYS_PORT_CLOSE, handle, 0, 0);
}

static inline long sys_shm_create(unsigned long size)
{
    return syscall3(SYS_SHM_CREATE, (long)size, 0, 0);
}

static inline long sys_shm_map(long id)
{
    return syscall3(SYS_SHM_MAP, id, 0, 0);
}

static inline long sys_shm_unmap(long id)
{
    return syscall3(SYS_SHM_UNMAP, id, 0, 0);
}

static inline long sys_shm_close(long id)
{
    return syscall3(SYS_SHM_CLOSE, id, 0, 0);
}

static inline long sys_fb_map(struct zos_fb_info *info)
{
    return syscall3(SYS_FB_MAP, (long)info, 0, 0);
}

static inline long sys_mouse(unsigned long state[3])
{
    return syscall3(SYS_MOUSE, (long)state, 0, 0);
}

static inline long sys_key(void)
{
    return syscall3(SYS_KEY, 0, 0, 0);
}

static inline long sys_sleep(unsigned long milliseconds)
{
    return syscall3(SYS_SLEEP, (long)milliseconds, 0, 0);
}

static inline long sys_cpus(void)
{
    return syscall3(SYS_CPUS, 0, 0, 0);
}

static inline long sys_rtc(void *time)
{
    return syscall3(SYS_RTC, (long)time, 0, 0);
}

static inline long sys_read_path(const char *path, void *buffer,
                                 unsigned long capacity)
{
    return syscall3(SYS_READ_PATH, (long)path, (long)buffer, (long)capacity);
}

static inline long sys_port_try_accept(long port, unsigned long *channel)
{
    return syscall3(SYS_PORT_TRY_ACCEPT, port, (long)channel, 0);
}

static inline long sys_port_try_recv(long channel, unsigned long *type,
                                     void *data, long capacity)
{
    return syscall4(SYS_PORT_TRY_RECV, channel, (long)type, (long)data,
                    capacity);
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
