/*
 * Zhaoxu OS - socket, address and resolver glue.
 *
 * The kernel exposes AF_UNIX stream endpoints; every other domain is
 * rejected with the real errno instead of a stand-in success.  Name
 * resolution supports numeric literals only until the FreeBSD network
 * stack is reachable from Ring 3.
 */
#include "posix_glue.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

int h_errno;

/* Create an AF_UNIX stream socket; other domains do not exist yet. */
int socket(int domain, int type, int protocol)
{
    if (domain != AF_UNIX || type != SOCK_STREAM || protocol != 0) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    long result = zos_syscall3(ZOS_SOCKET, domain, type, protocol);
    if (result < 0) {
        errno = EMFILE;
        return -1;
    }
    return (int)result;
}

int bind(int fd, const struct sockaddr *address, socklen_t length)
{
    long result = zos_syscall3(ZOS_BIND, fd, (long)(uintptr_t)address,
                               (long)length);
    if (result < 0) {
        errno = EADDRINUSE;
        return -1;
    }
    return 0;
}

int listen(int fd, int backlog)
{
    long result = zos_syscall3(ZOS_LISTEN, fd, backlog, 0);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int accept(int fd, struct sockaddr *address, socklen_t *length)
{
    long result = zos_syscall3(ZOS_ACCEPT, fd, (long)(uintptr_t)address,
                               (long)(uintptr_t)length);
    if (result < 0) {
        errno = ECONNABORTED;
        return -1;
    }
    return (int)result;
}

int connect(int fd, const struct sockaddr *address, socklen_t length)
{
    long result = zos_syscall3(ZOS_CONNECT, fd, (long)(uintptr_t)address,
                               (long)length);
    if (result < 0) {
        errno = ECONNREFUSED;
        return -1;
    }
    return 0;
}

/* Build a kernel message header for a single buffer. */
static ssize_t net_single(long call, int fd, void *buffer, size_t length,
                          int flags, const struct sockaddr *address,
                          socklen_t address_length, int receiving)
{
    if (flags != 0) {
        errno = EOPNOTSUPP;
        return -1;
    }
    struct iovec vector;
    vector.iov_base = buffer;
    vector.iov_len = length;
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_name = (void *)address;
    message.msg_namelen = address_length;
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    long result = zos_syscall3(call, fd, (long)(uintptr_t)&message, flags);
    if (result < 0) {
        errno = receiving ? EAGAIN : EPIPE;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t send(int fd, const void *buffer, size_t length, int flags)
{
    return net_single(ZOS_SENDMSG, fd, (void *)buffer, length, flags, NULL, 0,
                      0);
}

ssize_t recv(int fd, void *buffer, size_t length, int flags)
{
    return net_single(ZOS_RECVMSG, fd, buffer, length, flags, NULL, 0, 1);
}

ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length)
{
    return net_single(ZOS_SENDMSG, fd, (void *)buffer, length, flags, address,
                      address_length, 0);
}

ssize_t recvfrom(int fd, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length)
{
    socklen_t length_in = address_length ? *address_length : 0;
    return net_single(ZOS_RECVMSG, fd, buffer, length, flags, address,
                      length_in, 1);
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
{
    if (!message) {
        errno = EINVAL;
        return -1;
    }
    long result = zos_syscall3(ZOS_SENDMSG, fd, (long)(uintptr_t)message,
                               flags);
    if (result < 0) {
        errno = EPIPE;
        return -1;
    }
    return (ssize_t)result;
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags)
{
    if (!message) {
        errno = EINVAL;
        return -1;
    }
    long result = zos_syscall3(ZOS_RECVMSG, fd, (long)(uintptr_t)message,
                               flags);
    if (result < 0) {
        errno = EAGAIN;
        return -1;
    }
    return (ssize_t)result;
}

/* The kernel does not export endpoint names or socket options yet. */
int getsockname(int fd, struct sockaddr *address, socklen_t *length)
{
    (void)fd;
    (void)address;
    (void)length;
    errno = EOPNOTSUPP;
    return -1;
}

int getpeername(int fd, struct sockaddr *address, socklen_t *length)
{
    return getsockname(fd, address, length);
}

int shutdown(int fd, int how)
{
    (void)fd;
    (void)how;
    errno = ENOSYS;
    return -1;
}

int setsockopt(int fd, int level, int name, const void *value,
               socklen_t length)
{
    (void)fd;
    (void)level;
    (void)name;
    (void)value;
    (void)length;
    errno = ENOSYS;
    return -1;
}

/* Parse a dotted-quad IPv4 literal. */
static int inet_pton4(const char *text, unsigned char *out)
{
    int parts = 0;
    while (parts < 4) {
        int value = 0, digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 3) {
            value = value * 10 + (*text - '0');
            text++;
            digits++;
        }
        if (!digits || value > 255)
            return 0;
        out[parts++] = (unsigned char)value;
        if (parts == 4)
            break;
        if (*text != '.')
            return 0;
        text++;
    }
    return *text == '\0';
}

/* Parse an IPv6 literal with "::" compression. */
static int inet_pton6(const char *text, unsigned char *out)
{
    unsigned char buffer[16];
    int filled = 0, compressed = -1;
    memset(buffer, 0, sizeof(buffer));
    if (*text == ':') {
        if (text[1] != ':')
            return 0;
        compressed = 0;
        text += 2;
    }
    while (*text) {
        if (filled >= 16)
            return 0;
        int value = 0, digits = 0;
        while (((*text >= '0' && *text <= '9') ||
                (*text >= 'a' && *text <= 'f') ||
                (*text >= 'A' && *text <= 'F')) && digits < 4) {
            int digit = (*text <= '9') ? *text - '0' :
                        (*text <= 'F' ? *text - 'A' + 10 : *text - 'a' + 10);
            value = value * 16 + digit;
            text++;
            digits++;
        }
        if (*text == '.') {
            unsigned char v4[4];
            if (!inet_pton4(text, v4))
                return 0;
            if (filled > 12)
                return 0;
            buffer[filled++] = v4[0];
            buffer[filled++] = v4[1];
            buffer[filled++] = v4[2];
            buffer[filled++] = v4[3];
            text += strlen(text);
            break;
        }
        if (!digits)
            return 0;
        buffer[filled++] = (unsigned char)(value >> 8);
        buffer[filled++] = (unsigned char)(value & 0xFF);
        if (*text == '\0')
            break;
        if (*text != ':')
            return 0;
        text++;
        if (*text == ':') {
            if (compressed >= 0)
                return 0;
            compressed = filled;
            text++;
        }
    }
    if (compressed >= 0) {
        int tail = filled - compressed;
        memmove(buffer + 16 - tail, buffer + compressed, (size_t)tail);
        memset(buffer + compressed, 0, (size_t)(16 - tail - compressed));
    } else if (filled != 16) {
        return 0;
    }
    memcpy(out, buffer, 16);
    return 1;
}

int inet_pton(int family, const char *text, void *out)
{
    if (!text || !out)
        return 0;
    if (family == AF_INET)
        return inet_pton4(text, (unsigned char *)out);
    if (family == AF_INET6)
        return inet_pton6(text, (unsigned char *)out);
    return 0;
}

const char *inet_ntop(int family, const void *address, char *text,
                      socklen_t length)
{
    const unsigned char *bytes = address;
    int written;
    if (family == AF_INET) {
        written = snprintf(text, length, "%u.%u.%u.%u", bytes[0], bytes[1],
                           bytes[2], bytes[3]);
    } else if (family == AF_INET6) {
        written = snprintf(text, length, "%x:%x:%x:%x:%x:%x:%x:%x",
                           (bytes[0] << 8) | bytes[1],
                           (bytes[2] << 8) | bytes[3],
                           (bytes[4] << 8) | bytes[5],
                           (bytes[6] << 8) | bytes[7],
                           (bytes[8] << 8) | bytes[9],
                           (bytes[10] << 8) | bytes[11],
                           (bytes[12] << 8) | bytes[13],
                           (bytes[14] << 8) | bytes[15]);
    } else {
        return NULL;
    }
    return (written >= 0 && (socklen_t)written < length) ? text : NULL;
}

in_addr_t inet_addr(const char *text)
{
    struct in_addr address;
    if (!inet_pton(AF_INET, text, &address))
        return INADDR_NONE;
    return address.s_addr;
}

char *inet_ntoa(struct in_addr address)
{
    static char text[16];
    if (!inet_ntop(AF_INET, &address, text, sizeof(text)))
        return NULL;
    return text;
}

uint32_t htonl(uint32_t value)
{
    return __builtin_bswap32(value);
}

uint32_t ntohl(uint32_t value)
{
    return __builtin_bswap32(value);
}

uint16_t htons(uint16_t value)
{
    return __builtin_bswap16(value);
}

uint16_t ntohs(uint16_t value)
{
    return __builtin_bswap16(value);
}

/* Numeric-only resolution; DNS needs the FreeBSD resolver service. */
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    if (!result)
        return EAI_SYSTEM;
    *result = NULL;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    if (node) {
        struct sockaddr_in v4;
        struct sockaddr_in6 v6;
        struct sockaddr *address = NULL;
        socklen_t address_length = 0;
        if ((family == AF_UNSPEC || family == AF_INET) &&
            inet_pton(AF_INET, node, &v4.sin_addr)) {
            v4.sin_family = AF_INET;
            v4.sin_port = service ? htons((uint16_t)strtoul(service, NULL, 10)) : 0;
            address = (struct sockaddr *)&v4;
            address_length = sizeof(v4);
        } else if ((family == AF_UNSPEC || family == AF_INET6) &&
                   inet_pton(AF_INET6, node, &v6.sin6_addr)) {
            v6.sin6_family = AF_INET6;
            v6.sin6_port = service ? htons((uint16_t)strtoul(service, NULL, 10)) : 0;
            address = (struct sockaddr *)&v6;
            address_length = sizeof(v6);
        } else {
            errno = ENOSYS;
            return EAI_SYSTEM;
        }
        struct addrinfo *entry = calloc(1, sizeof(*entry));
        if (!entry)
            return EAI_MEMORY;
        entry->ai_family = address->sa_family;
        entry->ai_socktype = hints ? hints->ai_socktype : SOCK_STREAM;
        entry->ai_protocol = hints ? hints->ai_protocol : 0;
        entry->ai_addrlen = address_length;
        entry->ai_addr = malloc(address_length);
        if (!entry->ai_addr) {
            free(entry);
            return EAI_MEMORY;
        }
        memcpy(entry->ai_addr, address, address_length);
        *result = entry;
        return 0;
    }
    errno = ENOSYS;
    return EAI_SYSTEM;
}

void freeaddrinfo(struct addrinfo *result)
{
    while (result) {
        struct addrinfo *next = result->ai_next;
        free(result->ai_addr);
        free(result);
        result = next;
    }
}

const char *gai_strerror(int error)
{
    switch (error) {
    case 0: return "no error";
    case EAI_NONAME: return "name does not resolve";
    case EAI_AGAIN: return "temporary failure in name resolution";
    case EAI_FAIL: return "non-recoverable failure in name resolution";
    case EAI_FAMILY: return "address family not supported";
    case EAI_SOCKTYPE: return "socket type not supported";
    case EAI_SERVICE: return "service not supported";
    case EAI_MEMORY: return "out of memory";
    case EAI_SYSTEM: return "system error";
    default: return "unknown error";
    }
}

int getnameinfo(const void *address, unsigned int address_length, char *host,
                unsigned int host_length, char *service,
                unsigned int service_length, int flags)
{
    (void)address_length;
    (void)service;
    (void)service_length;
    (void)flags;
    const struct sockaddr *name = address;
    if (host && host_length) {
        if (name->sa_family == AF_INET) {
            inet_ntop(AF_INET, &((const struct sockaddr_in *)name)->sin_addr,
                      host, host_length);
            return 0;
        }
        if (name->sa_family == AF_INET6) {
            inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)name)->sin6_addr,
                      host, host_length);
            return 0;
        }
    }
    errno = ENOSYS;
    return EAI_SYSTEM;
}

struct hostent *gethostbyname(const char *name)
{
    (void)name;
    h_errno = NO_RECOVERY;
    errno = ENOSYS;
    return NULL;
}

struct servent *getservbyport(int port, const char *protocol)
{
    (void)port;
    (void)protocol;
    errno = ENOSYS;
    return NULL;
}

/* No interface list is exported from Ring 3 yet. */
int getifaddrs(struct ifaddrs **list)
{
    (void)list;
    errno = ENOSYS;
    return -1;
}

const char *hstrerror(int error)
{
    switch (error) {
    case HOST_NOT_FOUND: return "Unknown host";
    case TRY_AGAIN: return "Host name lookup failure";
    case NO_RECOVERY: return "Unknown server error";
    case NO_DATA: return "No address associated with name";
    default: return "Unknown resolver error";
    }
}

/* The resolver service is not exported; these report that plainly. */
int res_mkquery(int operation, const char *name, int class_, int type,
                const unsigned char *data, int data_length,
                const unsigned char *new_value, unsigned char *buffer,
                int buffer_length)
{
    (void)operation; (void)name; (void)class_; (void)type; (void)data;
    (void)data_length; (void)new_value; (void)buffer; (void)buffer_length;
    errno = ENOSYS;
    return -1;
}

int dn_expand(const unsigned char *message, const unsigned char *end,
              const unsigned char *compressed, char *expanded,
              int expanded_length)
{
    (void)message; (void)end; (void)compressed; (void)expanded;
    (void)expanded_length;
    errno = ENOSYS;
    return -1;
}
