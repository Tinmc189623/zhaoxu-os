/* FreeBSD cdefs additions on top of newlib's header. */
#ifndef ZHAOXU_SYS_CDEFS_PORT_H
#define ZHAOXU_SYS_CDEFS_PORT_H

#include_next <sys/cdefs.h>

/* BSD array-parameter annotation newlib does not provide. */
#ifndef __min_size
#define __min_size(x) (x)
#endif

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

#ifndef __BEGIN_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#endif

#endif
