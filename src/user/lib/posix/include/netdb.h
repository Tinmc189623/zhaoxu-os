/* Name-resolution surface: numeric literals resolve for real; DNS waits
 * for the FreeBSD network stack to be reachable from Ring 3. */
#ifndef ZHAOXU_NETDB_H
#define ZHAOXU_NETDB_H

#include <netinet/in.h>

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    unsigned int ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE 0x01
#define AI_CANONNAME 0x02
#define AI_NUMERICHOST 0x04
#define AI_ADDRCONFIG 0x20
#define AI_V4MAPPED 0x40
#define AI_ALL 0x80
#define NI_NUMERICHOST 0x01
#define NI_NUMERICSERV 0x02

#define EAI_NONAME -2
#define EAI_AGAIN -3
#define EAI_FAIL -4
#define EAI_SOCKTYPE -5
#define EAI_FAMILY -6
#define EAI_SERVICE -8
#define EAI_MEMORY -10
#define EAI_SYSTEM -11

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result);
void freeaddrinfo(struct addrinfo *result);
const char *gai_strerror(int error);
int getnameinfo(const void *address, unsigned int address_length, char *host,
                unsigned int host_length, char *service,
                unsigned int service_length, int flags);
struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};
struct hostent *gethostbyname(const char *name);

struct servent {
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
};
struct servent *getservbyport(int port, const char *protocol);
struct servent *getservbyname(const char *name, const char *protocol);
extern int h_errno;

#define HOST_NOT_FOUND 1
#define TRY_AGAIN 2
#define NO_RECOVERY 3
#define NO_DATA 4
const char *hstrerror(int error);

#endif
