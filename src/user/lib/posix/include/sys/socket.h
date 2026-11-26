/* Socket surface matching the kernel's AF_UNIX ABI.  INET domains are
 * declared here but rejected by socket() until the BSD stack lands. */
#ifndef ZHAOXU_SYS_SOCKET_H
#define ZHAOXU_SYS_SOCKET_H

#include <sys/types.h>

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char padding[126];
};

#define MSG_PEEK 0x02
#define MSG_DONTWAIT 0x40

int mlock(const void *address, size_t length);
int munlock(const void *address, size_t length);
int mlockall(int flags);
int munlockall(void);

#define MCL_CURRENT 1
#define MCL_FUTURE 2

struct cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

#define CMSG_ALIGN(length) \
    (((length) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(length) (CMSG_ALIGN(length) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_LEN(length) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (length))
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_FIRSTHDR(msg) \
    ((msg)->msg_controllen >= sizeof(struct cmsghdr) \
         ? (struct cmsghdr *)(msg)->msg_control : (struct cmsghdr *)0)
#define CMSG_NXTHDR(msg, cmsg) \
    (((cmsg)->cmsg_len < sizeof(struct cmsghdr) || \
      (unsigned char *)(cmsg) + CMSG_ALIGN((cmsg)->cmsg_len) >= \
          (unsigned char *)(msg)->msg_control + (msg)->msg_controllen) \
         ? (struct cmsghdr *)0 \
         : (struct cmsghdr *)((unsigned char *)(cmsg) + \
                              CMSG_ALIGN((cmsg)->cmsg_len)))

struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    socklen_t msg_controllen;
    int msg_flags;
};

#define AF_UNSPEC 0
#define AF_UNIX 1
#define PF_UNIX AF_UNIX
#define AF_LOCAL AF_UNIX

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 0x800
#define SOCK_CLOEXEC 0x80000

#define SOL_SOCKET 1
#define SO_REUSEADDR 4
#define SO_SNDTIMEO 21
#define SO_RCVTIMEO 20
#define SO_MARK 36

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int fds[2]);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
int connect(int fd, const struct sockaddr *address, socklen_t length);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length);
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length);
ssize_t sendmsg(int fd, const struct msghdr *message, int flags);
ssize_t recvmsg(int fd, struct msghdr *message, int flags);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int name, const void *value,
               socklen_t length);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getpeername(int fd, struct sockaddr *address, socklen_t *length);

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#endif
