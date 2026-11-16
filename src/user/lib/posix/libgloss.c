/*
 * Zhaoxu OS - newlib file and process syscall glue.
 *
 * Only descriptor I/O and process control live here.  Terminals, signals,
 * accounts, directory streams, path helpers and limits have their own files
 * under user/lib/posix/ (rules 23/29).
 */
#include "posix_glue.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Write bytes to a descriptor. */
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

/* Read bytes from a descriptor. */
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

/* Open a VFS path. */
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

/* Close a VFS descriptor. */
int _close(int fd)
{
    return zos_syscall3(ZOS_CLOSE, fd, 0, 0) == 0 ? 0 : -1;
}

/* Reposition a VFS descriptor. */
off_t _lseek(int fd, off_t offset, int whence)
{
    long result = zos_syscall3(ZOS_LSEEK, fd, (long)offset, whence);
    if (result < 0) {
        errno = ESPIPE;
        return (off_t)-1;
    }
    return (off_t)result;
}

/* Minimal fstat: the console is a character device, everything else a file. */
int _fstat(int fd, struct stat *status)
{
    if (!status)
        return -1;
    for (size_t i = 0; i < sizeof(*status); i++)
        ((unsigned char *)status)[i] = 0;
    status->st_mode = (fd >= 0 && fd <= 2) ? S_IFCHR : S_IFREG;
    return 0;
}

/* stat() backed by the kernel's is-dir/size syscall. */
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

/* Remove a VFS path. */
int _unlink(const char *path)
{
    if (!path)
        return -1;
    if (zos_syscall3(ZOS_UNLINK, (long)(uintptr_t)path, 0, 0) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

/* Hard links are not supported. */
int _link(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

/* Grow the C heap from a fixed BSS arena; ZOS has no brk syscall yet. */
static unsigned char g_sbrk_arena[ZOS_SBRK_BYTES] __attribute__((aligned(16)));
static size_t g_sbrk_used;

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

/* Leave the process through the kernel exit syscall. */
void _exit(int status)
{
    zos_syscall3(ZOS_EXIT, status, 0, 0);
    for (;;)
        __asm__ volatile("hlt");
}

/* Return the ZOS pid. */
int _getpid(void)
{
    return (int)zos_syscall3(ZOS_GETPID, 0, 0, 0);
}

/* Signals are not delivered yet; report that kill is unsupported. */
int _kill(int pid, int signal)
{
    (void)pid;
    (void)signal;
    errno = ENOSYS;
    return -1;
}

/* Fork this process; the child resumes with 0. */
int _fork(void)
{
    long result = zos_syscall3(ZOS_FORK, 0, 0, 0);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (int)result;
}

/* Count a NULL-terminated argument vector, capped at the kernel limit. */
static int count_argv(char *const argv[])
{
    int count = 0;
    if (argv)
        while (argv[count] && count < 16)
            count++;
    return count;
}

/* Replace this image and pass argv through the kernel exec syscall. */
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

/* newlib's _wait has no pid; the POSIX wrapper lands with waitpid. */
int _wait(int *status)
{
    (void)status;
    errno = ECHILD;
    return -1;
}

/* Create a pipe; the kernel returns two zfd numbers. */
int _pipe(int fds[2])
{
    long result = zos_syscall3(ZOS_PIPE, (long)(uintptr_t)fds, 0, 0);
    if (result != 0) {
        errno = EMFILE;
        return -1;
    }
    return 0;
}

/* Duplicate a descriptor onto a chosen number. */
int _dup2(int oldfd, int newfd)
{
    long result = zos_syscall3(ZOS_DUP2, oldfd, newfd, 0);
    if (result < 0) {
        errno = EBADF;
        return -1;
    }
    return (int)result;
}

/* Public aliases used by parts of newlib and gnulib. */
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

int stat(const char *path, struct stat *status)
{
    return _stat(path, status);
}

int unlink(const char *path)
{
    return _unlink(path);
}

int open(const char *path, int flags, ...)
{
    return _open(path, flags);
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

int getpid(void)
{
    return _getpid();
}

int kill(int pid, int signal)
{
    return _kill(pid, signal);
}

void *sbrk(intptr_t increment)
{
    return _sbrk((ptrdiff_t)increment);
}

int dup2(int oldfd, int newfd)
{
    return _dup2(oldfd, newfd);
}

int dup(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

/* Only F_GETFL is meaningful on the ZOS console descriptors. */
int fcntl(int fd, int command, ...)
{
    (void)fd;
    if (command == F_GETFL)
        return 0;
    errno = ENOSYS;
    return -1;
}
