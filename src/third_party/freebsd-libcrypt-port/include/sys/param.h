/* The subset of FreeBSD sys/param.h the crypto sources compile against. */
#ifndef ZHAOXU_PARAM_PORT_H
#define ZHAOXU_PARAM_PORT_H

#include <sys/types.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) (howmany((x), (y)) * (y))
#define rounddown(x, y) (((x) / (y)) * (y))
#define powerof2(x) ((((x) - 1) & (x)) == 0)

#endif
