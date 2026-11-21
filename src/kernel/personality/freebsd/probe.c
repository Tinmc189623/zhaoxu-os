/*
 * Compile probe (M2 slice 4).
 *
 * Built inside the FreeBSD tree with kernel headers, never linked into
 * KERNEL.ELF.  It proves three things before the port grows:
 *   1. the FreeBSD kernel headers compile in this environment,
 *   2. core_abi.h coexists with them in one translation unit,
 *   3. the pmap surface the port will touch is visible (kernel_pmap,
 *      pmap_t, vm_page_t).
 *
 * Build (inside the FreeBSD VM, from src):
 *   cc -D_KERNEL -I/usr/src/sys -I<this directory> -c probe.c -o /tmp/probe.o
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <machine/pmap.h>

#include "core_abi.h"

_Static_assert(sizeof(zx_u64) == 8, "core ABI is 64-bit");

int zhaoxu_probe_compile(void);

int zhaoxu_probe_compile(void)
{
    pmap_t kernel = kernel_pmap;
    vm_offset_t offset = 0;
    vm_page_t page = NULL;
    const struct zx_core_services *services = NULL;

    (void)kernel;
    (void)offset;
    (void)page;
    (void)services;
    return 0;
}
