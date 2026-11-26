#ifndef ZHAOXU_NETINET_IN_H
#define ZHAOXU_NETINET_IN_H
#include <sys/types.h>
typedef unsigned short in_port_t;
typedef unsigned int in_addr_t;
struct in_addr {
    in_addr_t s_addr;
};
struct in6_addr {
    unsigned char s6_addr[16];
};
struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
struct sockaddr_in6 {
    unsigned short sin6_family;
    unsigned short sin6_port;
    unsigned int sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned int sin6_scope_id;
};
#define AF_INET 2
#define AF_INET6 10
#define INADDR_ANY 0
#define INADDR_NONE 0xFFFFFFFFu
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#define IPPROTO_IP 0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_ICMPV6 58
#define SOL_IP 0
#define IPPROTO_IPV6 41
#define IP_TTL 2
#define IP_RECVTTL 12
#define IPV6_UNICAST_HOPS 16
#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_LOOP 19
#define IPV6_ADD_MEMBERSHIP 20
#define IPV6_RECVHOPLIMIT 51
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IP_DROP_MEMBERSHIP 36

struct ip_mreq {
    struct in_addr imr_multiaddr;
    struct in_addr imr_interface;
};
unsigned short htons(unsigned short value);
unsigned short ntohs(unsigned short value);
unsigned int htonl(unsigned int value);
unsigned int ntohl(unsigned int value);
#endif
