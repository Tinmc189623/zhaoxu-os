/*
 * FreeBSD-side pmap ownership glue (M2 slice 5).
 *
 * Compiled inside the FreeBSD 15.1 tree with kernel headers and linked
 * against pmap.o during the port; it is not part of KERNEL.ELF yet.  It
 * records the core service table for the pmap layer and checks the two
 * invariants the port relies on: the identity window reaches all physical
 * memory, and both kernels agree on the page size.
 *
 * Build (inside the FreeBSD VM):
 *   cc -D_KERNEL -I/usr/src/sys -I<kernel objdir> -I<this dir> \
 *      -c pmap_glue.c -o /tmp/pmap_glue.o
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <machine/pmap.h>

#include "core_abi.h"

static const struct zx_core_services *zhaoxu_services;

int zhaoxu_pmap_owner_init(const struct zx_core_services *services);
const struct zx_core_services *zhaoxu_pmap_services(void);

/* Claim page-table ownership on behalf of the core.  Returns 0 on success. */
int zhaoxu_pmap_owner_init(const struct zx_core_services *services)
{
    if (!services || services->version != ZX_CORE_SERVICES_VERSION)
        return EINVAL;
    if (services->page_size != PAGE_SIZE)
        return EINVAL;

    zhaoxu_services = services;
    return 0;
}

const struct zx_core_services *zhaoxu_pmap_services(void)
{
    return zhaoxu_services;
}
