/* Resolver entry points; the FreeBSD resolver supplies them in the BSD
 * subsystem, declarations only for the build. */
#ifndef ZHAOXU_RESOLV_H
#define ZHAOXU_RESOLV_H

#define NS_MAXDNAME 1025
#define NS_MAXCDNAME 255
#define NS_MAXLABEL 63
#define NS_MAXMSG 65535
#define NS_PACKETSZ 512

struct __res_state {
    int retrans;
    int retry;
    unsigned long options;
};

int res_init(void);
int res_query(const char *name, int class_, int type, unsigned char *answer,
              int answer_length);
int res_search(const char *name, int class_, int type, unsigned char *answer,
               int answer_length);
int dn_expand(const unsigned char *message, const unsigned char *end,
              const unsigned char *compressed, char *expanded,
              int expanded_length);
int res_mkquery(int operation, const char *name, int class_, int type,
                const void *data, int data_length, const void *newrr,
                unsigned char *buffer, int buffer_length);

#endif
