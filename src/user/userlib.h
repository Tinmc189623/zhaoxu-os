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
#define SYS_SERIAL_WRITE    28
#define SYS_READDIR         29
#define SYS_DISKS           30
#define SYS_INSTALL         31
#define SYS_WRITE_FILE      32
#define SYS_UNLINK          33
#define SYS_DATA_VOLUME     34
#define SYS_DEV_ALLOC       35
#define SYS_BOOT_FONT       36
#define SYS_SPAWN_ARG       37
#define SYS_BLK_COUNT       38
#define SYS_BLK_INFO        39
#define SYS_BLK_READ        40
#define SYS_BLK_WRITE       41
#define SYS_BLK_FLUSH       42
#define SYS_SOCKET          43
#define SYS_BIND            44
#define SYS_LISTEN          45
#define SYS_ACCEPT          46
#define SYS_CONNECT         47
#define SYS_SENDMSG         48
#define SYS_RECVMSG         49
#define SYS_POLL            50
#define SYS_MEMFD_CREATE    51
#define SYS_FTRUNCATE       52
#define SYS_LSEEK           53
#define SYS_MMAP            54
#define SYS_MUNMAP          55
#define SYS_FCNTL           56
#define SYS_SOCKETPAIR      57
#define SYS_BOOT_WALLPAPER  58
#define SYS_MEMFD_PHYS      59
#define SYS_MAP_PHYS        60
#define SYS_EVENTFD         61
#define SYS_TIMERFD_CREATE  62
#define SYS_TIMERFD_SETTIME 63
#define SYS_TIMERFD_GETTIME 64
#define SYS_EPOLL_CREATE1   65
#define SYS_EPOLL_CTL       66
#define SYS_EPOLL_WAIT      67
#define SYS_INSTALL_ERROR   68
#define SYS_REBOOT          69
#define SYS_INSTALL_PROGRESS 70
#define SYS_INSTALL_CONFIG  71
#define SYS_FORK             72
#define SYS_EXEC             73
#define SYS_PIPE             74
#define SYS_DUP2             75
#define SYS_IOCTL            76
#define SYS_STAT             77
#define SYS_GETPPID          78

/* Terminal ABI mirrored from kernel/include/tty.h. */
struct termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[19];
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TCGETS      0x5401
#define TCSETS      0x5402
#define TIOCGWINSZ  0x5413
#define FIONREAD    0x541B

#define ICANON      0x0002
#define ECHO        0x0008
#define ISIG        0x0001

#define VERASE      2
#define VMIN        6
#define VTIME       5

#define AF_UNIX      1
#define SOCK_STREAM  1
#define SOL_SOCKET   1
#define SCM_RIGHTS   1

#define POLLIN   0x001
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020

#define EPOLLIN  0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EFD_SEMAPHORE 1
#define EFD_NONBLOCK  0x800

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define TFD_TIMER_ABSTIME 1

#define O_NONBLOCK 0x800

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

struct iovec {
    void *iov_base;
    unsigned long iov_len;
};

struct msghdr {
    void *msg_name;
    unsigned int msg_namelen;
    unsigned int padding0;
    struct iovec *msg_iov;
    unsigned long msg_iovlen;
    void *msg_control;
    unsigned long msg_controllen;
    unsigned int msg_flags;
    unsigned int padding1;
};

struct cmsghdr {
    unsigned long cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

struct pollfd {
    int fd;
    short events;
    short revents;
};

struct zos_mmap_args {
    unsigned long address;
    unsigned long length;
    unsigned long protection;
    unsigned long flags;
    long fd;
    unsigned long offset;
};

struct zos_timespec {
    long long seconds;
    long long nanoseconds;
};

struct zos_itimerspec {
    struct zos_timespec interval;
    struct zos_timespec value;
};

struct zos_epoll_event {
    unsigned int events;
    unsigned long long data;
} __attribute__((packed));

#define CMSG_ALIGN(length) (((length) + 7ul) & ~7ul)
#define CMSG_LEN(length) (sizeof(struct cmsghdr) + (length))
#define CMSG_SPACE(length) \
    (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(length))
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + sizeof(struct cmsghdr))

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

/* Parent process id recorded when the kernel spawned this process. */
static inline long sys_getppid(void)
{
    return syscall3(SYS_GETPPID, 0, 0, 0);
}

static inline long sys_uptime(void)
{
    return syscall3(SYS_UPTIME, 0, 0, 0);
}

static inline long sys_spawn(const char *path)
{
    return syscall3(SYS_SPAWN, (long)path, 0, 0);
}

static inline long sys_spawn_arg(const char *path, const char *argument)
{
    return syscall3(SYS_SPAWN_ARG, (long)path, (long)argument, 0);
}

static inline long sys_wait(long pid, int *status)
{
    return syscall3(SYS_WAIT, pid, (long)status, 0);
}

/* POSIX process control.  fork() returns 0 in the child and the new pid in
 * the parent; execv() replaces the image and only returns on failure. */
static inline long sys_fork(void)
{
    return syscall3(SYS_FORK, 0, 0, 0);
}

static inline long sys_execv(const char *path, char *const argv[])
{
    int argc = 0;
    if (argv) {
        while (argv[argc] && argc < 16)
            argc++;
    }
    return syscall3(SYS_EXEC, (long)path, (long)argv, argc);
}

/* POSIX pipes and descriptor duplication.  Descriptor numbers are in the
 * zfd range (64 and above), matching sockets and memfds. */
static inline long sys_pipe(int fds[2])
{
    return syscall3(SYS_PIPE, (long)fds, 0, 0);
}

static inline long sys_dup2(int oldfd, int newfd)
{
    return syscall3(SYS_DUP2, oldfd, newfd, 0);
}

/* Terminal control for fd 0/1/2; returns -1 when the descriptor has been
 * redirected to a pipe or socket. */
static inline long sys_ioctl(int fd, unsigned long request, void *argument)
{
    return syscall3(SYS_IOCTL, fd, (long)request, (long)argument);
}

static inline int sys_isatty(int fd)
{
    struct termios termios;
    return sys_ioctl(fd, TCGETS, &termios) == 0;
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

static inline long sys_serial_write(const void *data, unsigned long length)
{
    return syscall3(SYS_SERIAL_WRITE, (long)data, (long)length, 0);
}

static inline long sys_readdir(const char *path, void *buffer,
                               unsigned long capacity)
{
    return syscall3(SYS_READDIR, (long)path, (long)buffer, (long)capacity);
}

static inline long sys_disks(void *buffer, unsigned long capacity)
{
    return syscall3(SYS_DISKS, (long)buffer, (long)capacity, 0);
}

static inline long sys_install(long target, long gpt)
{
    return syscall3(SYS_INSTALL, target, gpt, 0);
}

static inline long sys_write_file(const char *path, const void *data,
                                  unsigned long size)
{
    return syscall3(SYS_WRITE_FILE, (long)path, (long)data, (long)size);
}

static inline long sys_unlink(const char *path)
{
    return syscall3(SYS_UNLINK, (long)path, 0, 0);
}

static inline long sys_data_volume(void)
{
    return syscall3(SYS_DATA_VOLUME, 0, 0, 0);
}

static inline unsigned long sys_dev_alloc_page(void)
{
    return (unsigned long)syscall3(SYS_DEV_ALLOC, 0, 0, 0);
}

static inline long sys_boot_font(void *buffer, unsigned long capacity)
{
    return syscall3(SYS_BOOT_FONT, (long)buffer, (long)capacity, 0);
}

static inline long sys_boot_wallpaper(void *buffer, unsigned long capacity)
{
    return syscall3(SYS_BOOT_WALLPAPER, (long)buffer, (long)capacity, 0);
}

/* Display service only: physical address of one memfd page. */
static inline long sys_memfd_phys(long fd, unsigned long index)
{
    return syscall3(SYS_MEMFD_PHYS, fd, index, 0);
}

/* Display service only: map a device MMIO BAR into the caller's address
 * space and return the user virtual address. */
static inline long sys_map_phys(unsigned long physical, unsigned long size)
{
    return syscall3(SYS_MAP_PHYS, (long)physical, (long)size, 0);
}

static inline void sys_exit(int status)
{
    syscall3(SYS_EXIT, status, 0, 0);
    for (;;)
        __asm__ volatile("hlt");
}

/* Trusted whole-device block I/O; only /bin/ext4fs holds the capability. */
static inline long sys_blk_count(void)
{
    return syscall3(SYS_BLK_COUNT, 0, 0, 0);
}

static inline long sys_blk_info(int disk, u64 *sectors, u64 *port)
{
    return syscall3(SYS_BLK_INFO, disk, (long)sectors, (long)port);
}

static inline long sys_blk_read(int disk, u64 lba, u32 count, void *buffer)
{
    return syscall4(SYS_BLK_READ, disk, (long)lba, count, (long)buffer);
}

static inline long sys_blk_write(int disk, u64 lba, u32 count,
                                 const void *buffer)
{
    return syscall4(SYS_BLK_WRITE, disk, (long)lba, count, (long)buffer);
}

static inline long sys_blk_flush(int disk)
{
    return syscall3(SYS_BLK_FLUSH, disk, 0, 0);
}

/* AF_UNIX + memfd + poll (Wayland transport).  Descriptors returned here
 * start at 64 and are closed with the ordinary sys_close(). */
static inline long sys_socket(int domain, int type, int protocol)
{
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

static inline long sys_socketpair(int domain, int type, int protocol,
                                  int descriptors[2])
{
    return syscall4(SYS_SOCKETPAIR, domain, type, protocol,
                    (long)descriptors);
}

static inline long sys_bind(long fd, const struct sockaddr_un *address,
                            unsigned long length)
{
    return syscall3(SYS_BIND, fd, (long)address, length);
}

static inline long sys_listen(long fd, int backlog)
{
    return syscall3(SYS_LISTEN, fd, backlog, 0);
}

static inline long sys_accept(long fd, struct sockaddr_un *address,
                              unsigned int *length)
{
    return syscall3(SYS_ACCEPT, fd, (long)address, (long)length);
}

static inline long sys_connect(long fd, const struct sockaddr_un *address,
                               unsigned long length)
{
    return syscall3(SYS_CONNECT, fd, (long)address, length);
}

static inline long sys_sendmsg(long fd, const struct msghdr *message,
                               int flags)
{
    return syscall3(SYS_SENDMSG, fd, (long)message, flags);
}

static inline long sys_recvmsg(long fd, struct msghdr *message, int flags)
{
    return syscall3(SYS_RECVMSG, fd, (long)message, flags);
}

static inline long sys_poll(struct pollfd *fds, unsigned long count,
                            int timeout)
{
    return syscall3(SYS_POLL, (long)fds, count, timeout);
}

static inline long sys_memfd_create(const char *name, unsigned int flags)
{
    return syscall3(SYS_MEMFD_CREATE, (long)name, flags, 0);
}

static inline long sys_ftruncate(long fd, unsigned long size)
{
    return syscall3(SYS_FTRUNCATE, fd, size, 0);
}

static inline long sys_lseek(long fd, long offset, int whence)
{
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

static inline long sys_mmap(struct zos_mmap_args *arguments)
{
    return syscall3(SYS_MMAP, (long)arguments, 0, 0);
}

static inline long sys_munmap(void *address, unsigned long length)
{
    return syscall3(SYS_MUNMAP, (long)address, length, 0);
}

static inline long sys_fcntl(long fd, int command, long argument)
{
    return syscall3(SYS_FCNTL, fd, command, argument);
}

static inline long sys_eventfd(unsigned int initial, int flags)
{
    return syscall3(SYS_EVENTFD, initial, flags, 0);
}

static inline long sys_timerfd_create(int clock_id, int flags)
{
    return syscall3(SYS_TIMERFD_CREATE, clock_id, flags, 0);
}

static inline long sys_timerfd_settime(int fd, int flags,
                                       const struct zos_itimerspec *new_value,
                                       struct zos_itimerspec *old_value)
{
    return syscall4(SYS_TIMERFD_SETTIME, fd, flags, (long)new_value,
                    (long)old_value);
}

static inline long sys_timerfd_gettime(int fd, struct zos_itimerspec *current)
{
    return syscall3(SYS_TIMERFD_GETTIME, fd, (long)current, 0);
}

static inline long sys_epoll_create1(int flags)
{
    return syscall3(SYS_EPOLL_CREATE1, flags, 0, 0);
}

static inline long sys_epoll_ctl(int epfd, int op, int fd,
                                 struct zos_epoll_event *event)
{
    return syscall4(SYS_EPOLL_CTL, epfd, op, fd, (long)event);
}

static inline long sys_epoll_wait(int epfd, struct zos_epoll_event *events,
                                  int max_events, int timeout)
{
    return syscall4(SYS_EPOLL_WAIT, epfd, (long)events, max_events, timeout);
}

static inline long sys_install_error(char *buffer, unsigned long capacity)
{
    return syscall3(SYS_INSTALL_ERROR, (long)buffer, (long)capacity, 0);
}

/* Installer UI and worker share this page through shm.  The kernel updates
 * done/total after each coarse stage; the worker fills the result fields. */
struct zos_install_progress {
    volatile unsigned int done;
    volatile unsigned int total;
    volatile unsigned int finished;
    volatile int result;
    char error[128];
};

static inline long sys_install_progress(long target, long gpt,
                                        struct zos_install_progress *progress)
{
    return syscall4(SYS_INSTALL_PROGRESS, target, gpt, (long)progress,
                    sizeof(*progress));
}

/* Custom partition layout for SYS_INSTALL_CONFIG; sizes are in MiB. */
struct zos_install_config {
    unsigned long target;
    unsigned long gpt;
    struct zos_install_progress *progress;
    unsigned long progress_capacity;
    unsigned long esp_mib;
    unsigned long data_mib;
};

static inline long sys_install_config(struct zos_install_config *config)
{
    return syscall3(SYS_INSTALL_CONFIG, (long)config, 0, 0);
}

/* Reboot the machine (keyboard-controller reset with a triple-fault fallback). */
static inline void sys_reboot(void)
{
    syscall3(SYS_REBOOT, 0, 0, 0);
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
