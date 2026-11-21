/*
 * Zhaoxu OS - FreeBSD personality boot seam and core service table (M2).
 *
 * The core calls freebsd_personality_start() after personality_start() has
 * recorded the boot record.  The machine port provides
 * freebsd_personality_main(); until it exists the weak default says so and
 * leaves the core running normally.
 *
 * Both kernels define kmalloc/printf/scheduler primitives, so the FreeBSD
 * side binds through freebsd_core_services() instead of linking core
 * symbols directly.
 */
#include <boot.h>
#include <kprintf.h>
#include <mem.h>
#include <personality.h>
#include <personality_freebsd.h>
#include <types.h>

#define PMAP_PROBE_PATTERN 0x5A4F5331504D4150ULL  /* "ZOS1PMAP" */

static struct zx_core_services g_services;

const struct zx_core_services *freebsd_core_services(void)
{
    return &g_services;
}

/* The port uses the granted window for its early mappings; prove it works. */
int freebsd_pmap_init(const struct boot_info *bi)
{
    if (!bi || bi->magic != BOOT_MAGIC)
        return -1;
    if (!personality_vm_base() || personality_vm_pages() == 0)
        return -1;
    if (personality_vm_map(0, VMM_WRITABLE) != 0)
        return -1;

    volatile u64 *probe = (volatile u64 *)(uintptr_t)personality_vm_base();
    probe[0] = PMAP_PROBE_PATTERN;
    int ok = probe[0] == PMAP_PROBE_PATTERN;

    personality_vm_unmap(0);
    if (!ok)
        return -1;

    kprintf("[bsd ] pmap window 0x%llx, %u pages, identity %llu GiB\n",
            personality_vm_base(), personality_vm_pages(),
            FREEBSD_IDENTITY_LIMIT >> 30);
    return 0;
}

/* Fill the table once; every entry points at an existing core function. */
static void services_init(const struct boot_info *bi)
{
    g_services.version = ZX_CORE_SERVICES_VERSION;
    g_services.page_size = (zx_u32)PAGE_SIZE;
    g_services.kernel_va_base = bi->kernel_virt_base;
    g_services.kernel_va_end = bi->kernel_virt_base + bi->kernel_size;
    g_services.identity_limit = ZX_IDENTITY_LIMIT;
    g_services.console_write = personality_console_write;
    g_services.vm_base = personality_vm_base;
    g_services.vm_pages = personality_vm_pages;
    g_services.vm_map = personality_vm_map;
    g_services.vm_unmap = personality_vm_unmap;
    g_services.vm_phys = personality_vm_phys;
    g_services.task_create = (void *(*)(const char *, zx_task_fn, void *))
        personality_task_create;
    g_services.trap_register = (int (*)(zx_u8, zx_trap_fn, void *))
        personality_trap_register;
    g_services.timer_register = personality_timer_register;
    g_services.ipc_create = personality_ipc_create;
    g_services.ipc_send = personality_ipc_send;
    g_services.ipc_recv = personality_ipc_recv;
}

/* Replaced by the machine port (M2) once the personality is linked. */
__attribute__((weak))
int freebsd_personality_main(const struct boot_info *bi)
{
    (void)bi;
    kprintf("[bsd ] personality entry not linked\n");
    return -1;
}

/* The core's hand-off point; owned by the glue layer, not by FreeBSD. */
int freebsd_personality_start(const struct boot_info *bi)
{
    kprintf("[bsd ] core services v%u\n", FREEBSD_CORE_SERVICES_VERSION);
    services_init(bi);

    if (freebsd_pmap_init(bi) != 0) {
        kprintf("[bsd ] pmap window FAIL\n");
        return -1;
    }
    kprintf("[bsd ] pmap window PASS\n");

    kprintf("[bsd ] handing off to the FreeBSD personality\n");
    return freebsd_personality_main(bi);
}
