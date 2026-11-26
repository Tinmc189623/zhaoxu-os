#ifndef ZHAOXU_ARPA_INET_H
#define ZHAOXU_ARPA_INET_H
#include <netinet/in.h>
const char *inet_ntop(int family, const void *address, char *text,
                      unsigned int size);
int inet_pton(int family, const char *text, void *address);
in_addr_t inet_addr(const char *text);
char *inet_ntoa(struct in_addr address);
#endif
