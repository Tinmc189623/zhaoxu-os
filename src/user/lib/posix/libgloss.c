/*
 * Zhaoxu OS - newlib libgloss system-call layer.
 *
 * newlib calls these bare functions from libc.a.  They translate into the
 * ZOS int 0x80 ABI; the numbers must stay in sync with
 * kernel/proc/syscall.c.  This file deliberately does not include
 * user/userlib.h: newlib owns struct termios, stat and errno, and including
 * both would collide on those names.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define ZOS_EXIT     0
#define ZOS_WRITE    1
#define ZOS_GETPID   2
#define ZOS_OPEN     3
#define ZOS_READ     4
#define ZOS_CLOSE    5
#define ZOS_WAIT     8
#define ZOS_LSEEK    53
#define ZOS_FORK     72
#define ZOS_EXEC     73
#define ZOS_PIPE     74
#define ZOS_DUP2     75
#define ZOS_IOCTL    76
#define ZOS_STAT     77
#define ZOS_READDIR  29

#define ZOS_TCGETS   0x5401

#define ZOS_SBRK_BYTES (16u * 1024u * 1024u)

/* Keep the ABI in one place; layout matches user/userlib.h's termios. */
struct zos_termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[19];
};

static long zos_syscall3(long number, long a, long b, long c)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return result;
}

static unsigned char g_sbrk_arena[ZOS_SBRK_BYTES] __attribute__((aligned(16)));
static size_t g_sbrk_used;

int _write(int fd, const void *buffer, size_t length)
{
    long result =
        zos_syscall3(ZOS_WRITE, fd, (long)(uintptr_t)buffer, (long)length);
    if (result < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)result;
}

int _read(int fd, void *buffer, size_t length)
{
    long result =
        zos_syscall3(ZOS_READ, fd, (long)(uintptr_t)buffer, (long)length);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (int)result;
}

int _open(const char *path, int flags, ...)
{
    (void)flags;
    long result = zos_syscall3(ZOS_OPEN, (long)(uintptr_t)path, 0, 0);
    if (result < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)result;
}

int _close(int fd)
{
    return zos_syscall3(ZOS_CLOSE, fd, 0, 0) == 0 ? 0 : -1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    long result = zos_syscall3(ZOS_LSEEK, fd, (long)offset, whence);
    if (result < 0) {
        errno = ESPIPE;
        return (off_t)-1;
    }
    return (off_t)result;
}

int _fstat(int fd, struct stat *status)
{
    if (!status)
        return -1;
    for (size_t i = 0; i < sizeof(*status); i++)
        ((unsigned char *)status)[i] = 0;
    status->st_mode = (fd >= 0 && fd <= 2) ? S_IFCHR : S_IFREG;
    return 0;
}

int _stat(const char *path, struct stat *status)
{
    if (!path || !status)
        return -1;
    int is_dir = 0;
    unsigned long long size = 0;
    if (zos_syscall3(ZOS_STAT, (long)(uintptr_t)path,
                     (long)(uintptr_t)&is_dir,
                     (long)(uintptr_t)&size) != 0) {
        errno = ENOENT;
        return -1;
    }
    for (size_t i = 0; i < sizeof(*status); i++)
        ((unsigned char *)status)[i] = 0;
    status->st_mode = (is_dir ? S_IFDIR : S_IFREG) | 0755;
    status->st_size = (off_t)size;
    return 0;
}

int _isatty(int fd)
{
    struct zos_termios termios;
    return zos_syscall3(ZOS_IOCTL, fd, ZOS_TCGETS,
                        (long)(uintptr_t)&termios) == 0 ? 1 : 0;
}

void *_sbrk(ptrdiff_t increment)
{
    if (increment < 0)
        return (void *)-1;
    size_t need = (size_t)increment;
    if (need > ZOS_SBRK_BYTES - g_sbrk_used) {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *result = &g_sbrk_arena[g_sbrk_used];
    g_sbrk_used += need;
    return result;
}

void _exit(int status)
{
    zos_syscall3(ZOS_EXIT, status, 0, 0);
    for (;;)
        __asm__ volatile("hlt");
}

int _getpid(void)
{
    return (int)zos_syscall3(ZOS_GETPID, 0, 0, 0);
}

int _kill(int pid, int signal)
{
    (void)pid;
    (void)signal;
    errno = ENOSYS;
    return -1;
}

int _fork(void)
{
    long result = zos_syscall3(ZOS_FORK, 0, 0, 0);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (int)result;
}

static int count_argv(char *const argv[])
{
    int count = 0;
    if (argv)
        while (argv[count] && count < 16)
            count++;
    return count;
}

int _execve(const char *path, char *const argv[], char *const envp[])
{
    (void)envp;
    long result = zos_syscall3(ZOS_EXEC, (long)(uintptr_t)path,
                               (long)(uintptr_t)argv, count_argv(argv));
    if (result < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int _wait(int *status)
{
    /* The ZOS wait syscall takes an explicit pid; newlib's _wait has none.
     * Editors do not fork, so report "no child" until waitpid lands. */
    (void)status;
    errno = ECHILD;
    return -1;
}

int _pipe(int fds[2])
{
    long result = zos_syscall3(ZOS_PIPE, (long)(uintptr_t)fds, 0, 0);
    if (result != 0) {
        errno = EMFILE;
        return -1;
    }
    return 0;
}

int _dup2(int oldfd, int newfd)
{
    long result = zos_syscall3(ZOS_DUP2, oldfd, newfd, 0);
    if (result < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)result;
}

int _ioctl(int fd, unsigned long request, void *argument)
{
    long result = zos_syscall3(ZOS_IOCTL, fd, (long)request,
                               (long)(uintptr_t)argument);
    if (result < 0) {
        errno = ENOTTY;
        return -1;
    }
    return 0;
}

int _link(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

int _unlink(const char *path)
{
    (void)path;
    errno = ENOSYS;
    return -1;
}

int _mkdir(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

clock_t _times(void *buffer)
{
    (void)buffer;
    errno = ENOSYS;
    return (clock_t)-1;
}

/* Parts of newlib call the public POSIX names directly; libgloss normally
 * supplies them as aliases of the underscore stubs above. */
_READ_WRITE_RETURN_TYPE read(int fd, void *buffer, size_t length)
{
    return (_READ_WRITE_RETURN_TYPE)_read(fd, buffer, length);
}

_READ_WRITE_RETURN_TYPE write(int fd, const void *buffer, size_t length)
{
    return (_READ_WRITE_RETURN_TYPE)_write(fd, buffer, length);
}

int close(int fd)
{
    return _close(fd);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return _lseek(fd, offset, whence);
}

int fstat(int fd, struct stat *status)
{
    return _fstat(fd, status);
}

int isatty(int fd)
{
    return _isatty(fd);
}

void *sbrk(intptr_t increment)
{
    return _sbrk((ptrdiff_t)increment);
}

/* ncurses calls the varargs ioctl and the termios helpers directly. */
int ioctl(int fd, unsigned long request, ...)
{
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    return _ioctl(fd, request, argument);
}

int tcgetattr(int fd, struct termios *termios)
{
    return ioctl(fd, TCGETS, termios);
}

int tcsetattr(int fd, int action, const struct termios *termios)
{
    (void)action;
    return ioctl(fd, TCSETS, (void *)termios);
}

speed_t cfgetospeed(const struct termios *termios)
{
    (void)termios;
    return B38400;
}

speed_t cfgetispeed(const struct termios *termios)
{
    (void)termios;
    return B38400;
}

int cfsetospeed(struct termios *termios, speed_t speed)
{
    (void)termios;
    (void)speed;
    return 0;
}

int cfsetispeed(struct termios *termios, speed_t speed)
{
    (void)termios;
    (void)speed;
    return 0;
}

int tcflush(int fd, int queue)
{
    (void)fd;
    (void)queue;
    return 0;
}

int tcdrain(int fd)
{
    (void)fd;
    return 0;
}

int tcflow(int fd, int action)
{
    (void)fd;
    (void)action;
    return 0;
}

int open(const char *path, int flags, ...)
{
    return _open(path, flags);
}

int getpid(void)
{
    return _getpid();
}

int kill(int pid, int signal)
{
    return _kill(pid, signal);
}

int stat(const char *path, struct stat *status)
{
    return _stat(path, status);
}

int access(const char *path, int mode)
{
    (void)mode;
    int fd = _open(path, 0);
    if (fd < 0)
        return -1;
    _close(fd);
    return 0;
}

int gettimeofday(struct timeval *time, void *timezone)
{
    (void)timezone;
    if (time) {
        time->tv_sec = 0;
        time->tv_usec = 0;
    }
    return 0;
}

int dup(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

int fcntl(int fd, int command, ...)
{
    (void)fd;
    if (command == F_GETFL)
        return 0;
    errno = ENOSYS;
    return -1;
}

char *getprogname(void)
{
    return (char *)"nano";
}

int getdtablesize(void)
{
    return 128;
}

int getrlimit(int resource, struct rlimit *limit)
{
    (void)resource;
    if (!limit)
        return -1;
    limit->rlim_cur = 128;
    limit->rlim_max = 128;
    return 0;
}

int setrlimit(int resource, const struct rlimit *limit)
{
    (void)resource;
    (void)limit;
    return 0;
}

/* Directory streams over the kernel's readdir text:
 *   "d name\n" and "f name size\n". */
struct zos_dir_stream {
    char *buffer;
    size_t length;
    size_t offset;
    struct dirent entry;
};

DIR *opendir(const char *path)
{
    char *buffer = malloc(65536);
    if (!buffer)
        return NULL;
    long length = zos_syscall3(ZOS_READDIR, (long)(uintptr_t)path,
                               (long)(uintptr_t)buffer, 65536);
    if (length <= 0) {
        free(buffer);
        errno = ENOENT;
        return NULL;
    }
    struct zos_dir_stream *stream = malloc(sizeof(*stream));
    if (!stream) {
        free(buffer);
        return NULL;
    }
    stream->buffer = buffer;
    stream->length = (size_t)length;
    stream->offset = 0;
    return (DIR *)stream;
}

struct dirent *readdir(DIR *directory)
{
    struct zos_dir_stream *stream = (struct zos_dir_stream *)directory;
    if (!stream || stream->offset >= stream->length)
        return NULL;

    size_t start = stream->offset;
    size_t end = start;
    while (end < stream->length && stream->buffer[end] != '\n')
        end++;
    size_t next = end < stream->length ? end + 1 : end;

    char type = stream->buffer[start];
    size_t name_start = start + 2;
    if (name_start > end)
        name_start = end;
    size_t name_length = 0;
    while (name_start + name_length < end &&
           stream->buffer[name_start + name_length] != ' ')
        name_length++;
    if (name_length >= sizeof(stream->entry.d_name))
        name_length = sizeof(stream->entry.d_name) - 1;

    for (size_t i = 0; i < sizeof(stream->entry); i++)
        ((unsigned char *)&stream->entry)[i] = 0;
    for (size_t i = 0; i < name_length; i++)
        stream->entry.d_name[i] = stream->buffer[name_start + i];
    stream->entry.d_name[name_length] = '\0';
    stream->entry.d_type = type == 'd' ? DT_DIR : DT_REG;
    stream->offset = next;
    return &stream->entry;
}

int closedir(DIR *directory)
{
    struct zos_dir_stream *stream = (struct zos_dir_stream *)directory;
    if (!stream)
        return -1;
    free(stream->buffer);
    free(stream);
    return 0;
}
