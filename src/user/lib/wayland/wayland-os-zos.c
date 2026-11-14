/*
 * Zhaoxu OS - wayland-os entry points for the stock client.
 *
 * The file descriptor flags Linux passes around (SOCK_CLOEXEC,
 * F_DUPFD_CLOEXEC, MSG_CMSG_CLOEXEC) have no equivalent in ZOS: descriptors
 * are private to the process until SCM_RIGHTS moves them.  The helpers keep
 * the upstream call shapes and simply perform the underlying operation.
 */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "wayland-os.h"

#define ZOS_SYS_ACCEPT 46
#define ZOS_SYS_EPOLL_CREATE1 65

static inline long zos_os_call2(long number, long a, long b)
{
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b)
                     : "memory");
    return result;
}

static inline long zos_os_call4(long number, long a, long b, long c, long d)
{
    long result;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "memory");
    return result;
}

int wl_os_socket_cloexec(int domain, int type, int protocol)
{
    return socket(domain, type, protocol);
}

int wl_os_socket_peercred(int sockfd, uid_t *uid, gid_t *gid, pid_t *pid)
{
    (void)sockfd;
    (void)uid;
    (void)gid;
    (void)pid;
    errno = ENOTSUP;
    return -1;
}

int wl_os_dupfd_cloexec(int fd, int minfd)
{
    return fcntl(fd, F_DUPFD, minfd);
}

ssize_t wl_os_recvmsg_cloexec(int sockfd, struct msghdr *msg, int flags)
{
    return recvmsg(sockfd, msg, flags);
}

int wl_os_epoll_create_cloexec(void)
{
    long result = zos_os_call2(ZOS_SYS_EPOLL_CREATE1, 0, 0);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)result;
}

int wl_os_accept_cloexec(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    long length = addrlen ? *addrlen : 0;
    long result =
        zos_os_call4(ZOS_SYS_ACCEPT, sockfd, (long)addr, length,
                     (long)addrlen);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (int)result;
}

void *wl_os_mremap_maymove(int fd, void *old_data, ssize_t *old_size,
                           ssize_t new_size, int prot, int flags)
{
    (void)fd;
    (void)old_data;
    (void)old_size;
    (void)new_size;
    (void)prot;
    (void)flags;
    errno = ENOTSUP;
    return MAP_FAILED;
}
