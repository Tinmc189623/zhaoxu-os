#ifndef ZHAOXU_PERSONALITY_FREEBSD_H
#define ZHAOXU_PERSONALITY_FREEBSD_H

#include <personality.h>
#include <personality/freebsd/core_abi.h>
#include <types.h>

/*
 * FreeBSD personality binding contract (M2).
 *
 * The FreeBSD objects must not link the core's symbols directly: both trees
 * define kmalloc, printf, thread primitives and so on.  The core therefore
 * exports one versioned service table (defined in personality/freebsd/
 * core_abi.h, which the FreeBSD build includes too), and the machine port
 * calls through it.
 */

#define FREEBSD_CORE_SERVICES_VERSION ZX_CORE_SERVICES_VERSION
#define FREEBSD_IDENTITY_LIMIT ZX_IDENTITY_LIMIT

struct boot_info;

#if FREEBSD_PERSONALITY
/* Table instance, valid after freebsd_personality_start(). */
const struct zx_core_services *freebsd_core_services(void);

/* Core-side pmap preparation: verifies the granted VM window is usable. */
int freebsd_pmap_init(const struct boot_info *bi);
#endif

#endif
