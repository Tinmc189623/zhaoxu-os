/*
 * Zhaoxu OS - newlib libgloss system-call layer.
 *
 * newlib calls these bare functions from libc.a.  They translate into the
 * ZOS int 0x80 ABI; the numbers must stay in sync with
 * kernel/proc/syscall.c.  This file deliberately does not include
 * user/userlib.h: newlib owns struct termios, stat and errno, and including
 * both would collide on those names.
 */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define ZOS_EXIT     0
#define ZOS_WRITE    1
#define ZOS_GETPID   2
#define ZOS_OPEN     3
#define ZOS_READ     4
#define ZOS_CLOSE    5
#define ZOS_WAIT     8
#define ZOS_FORK     72
#define ZOS_EXEC     73
#define ZOS_PIPE     74
#define ZOS_DUP2     75
#define ZOS_IOCTL    76

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
    (void)fd;
    (void)offset;
    (void)whence;
    errno = ESPIPE;
    return (off_t)-1;
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
    (void)path;
    if (!status)
        return -1;
    for (size_t i = 0; i < sizeof(*status); i++)
        ((unsigned char *)status)[i] = 0;
    status->st_mode = S_IFREG;
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
