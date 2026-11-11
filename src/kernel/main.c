/*
 * Zhaoxu OS - kernel entry.
 *
 * The kernel core owns CPU setup, memory, scheduling, IPC and the VFS.  All
 * hardware devices are owned by the Ring3 driver service (/bin/drivers);
 * the kernel only exchanges requests through the dev shared pages.
 */
#include <apic.h>
#include <boot.h>
#include <console.h>
#include <cpu.h>
#include <dev.h>
#include <fs_rpc.h>
#include <gdt.h>
#include <idt.h>
#include <io.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <smp.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>
#include <vfs.h>
#include <version.h>

static struct boot_info g_boot;

#ifndef PROC_SELFTEST
#define PROC_SELFTEST 0
#endif

#if PROC_SELFTEST == 1
static void process_selftest(void)
{
    u64 first = process_spawn("/data/HELLO.ZXE", 0, NULL);
    u64 second = process_spawn("/data/HELLO.ZXE", 0, NULL);
    int first_status = -1;
    int second_status = -1;

    if (first)
        process_wait(first, &first_status);
    if (second)
        process_wait(second, &second_status);

    kprintf("[test] pid %llu status %d, pid %llu status %d\n",
            first, first_status, second, second_status);
}
#elif PROC_SELFTEST == 2
static void ipc_selftest(void)
{
    u64 pong = process_spawn("/data/PONG.ZXE", 0, NULL);
    thread_sleep_ms(20);
    u64 ping = process_spawn("/data/PING.ZXE", 0, NULL);
    int pong_status = -1;
    int ping_status = -1;

    if (ping)
        process_wait(ping, &ping_status);
    if (pong)
        process_wait(pong, &pong_status);

    kprintf("[test] ipc pong=%d ping=%d\n", pong_status, ping_status);
}
#endif

const struct boot_info *boot_get_info(void)
{
    return &g_boot;
}

void kernel_main(struct boot_info *boot)
{
    if (!boot || boot->magic != BOOT_MAGIC) {
        for (;;)
            hlt();
    }
    g_boot = *boot;
    g_boot.rsdp = apic_locate_rsdp(g_boot.rsdp);

    gdt_init();
    idt_init();
    pic_init();

    pmm_init(&g_boot);
    vmm_init(&g_boot);
    heap_init();

    dev_memory_init();
    console_init(&g_boot);

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Zhaoxu OS %s \"%s\"\n", ZHAOXU_VERSION, ZHAOXU_CODENAME);
    console_set_color(COLOR_DARK_GRAY, COLOR_BLACK);
    kprintf("Microkernel: drivers and applications run in ring 3\n");
    console_reset_color();
    kprintf("\n");

    cpu_init();
    timer_init(1000);
    process_init();

    kprintf("[boot] higher-half kernel at 0x%llx (phys 0x%llx, %llu KiB)\n",
            g_boot.kernel_virt_base, g_boot.kernel_phys_base,
            g_boot.kernel_size / 1024);
    kprintf("[boot] memory map: %llu bytes, %llu bytes/entry, ver %u\n",
            g_boot.memory_map_size, g_boot.memory_map_desc_size,
            g_boot.memory_map_desc_version);

    smp_init(g_boot.rsdp);
    if (apic_lapic_hz())
        apic_timer_start(APIC_VECTOR_TIMER, timer_hz());
    interrupts_enable();

    vfs_init();

    kprintf("[dev ] starting Ring3 driver service\n");
    if (dev_start() == 0)
        kprintf("[dev ] driver service ready\n");
    else
        kprintf("[dev ] driver service unavailable\n");

    kprintf("[fs  ] starting Ring3 ext4 service\n");
    if (fs_service_start() == 0)
        kprintf("[fs  ] ext4 service ready\n");
    else
        kprintf("[fs  ] ext4 service unavailable\n");

    struct pmm_stats pmm;
    pmm_get_stats(&pmm);
    kprintf("[pmm ] used %llu MiB / free %llu MiB\n",
            (pmm.used_pages * PAGE_SIZE) >> 20,
            (pmm.free_pages * PAGE_SIZE) >> 20);

    timer_sleep_ms(100);
    kprintf("[time] ticks after 100 ms: %llu\n", timer_ticks());

    kprintf("\n[vfs ] mounting storage through the driver service\n");
    if (vfs_mount_data() == 0)
        kprintf("[vfs ] /mnt mounted (%s \"%s\")\n", vfs_data_fs(),
                vfs_data_label());
    else if (vfs_data_fallback())
        kprintf("[vfs ] /mnt -> embedded data mirror (no volume attached)\n");
    else
        kprintf("[vfs ] /mnt unavailable, using ramfs only\n");

#if PROC_SELFTEST == 1
    process_selftest();
#elif PROC_SELFTEST == 2
    ipc_selftest();
#endif

    if (g_boot.framebuffer_base) {
        console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
        kprintf("\nBoot complete. Starting ADE desktop.\n\n");
        console_reset_color();

        u64 ade_pid = process_spawn("/bin/ade", 0, NULL);
        if (ade_pid) {
            int status = -1;
            process_wait(ade_pid, &status);
            kprintf("[ade ] ring3 desktop exited with %d\n", status);
        } else {
            kprintf("[ade ] /bin/ade unavailable\n");
        }
        kprintf("[ade ] desktop stopped; halting.\n");
        for (;;)
            hlt();
    }

    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("\nBoot complete. No framebuffer; starting csh.\n\n");
    console_reset_color();
    for (;;) {
        u64 csh_pid = process_spawn("/bin/csh", 0, NULL);
        if (!csh_pid) {
            console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
            kprintf("[csh ] /bin/csh unavailable; halting.\n");
            console_reset_color();
            for (;;)
                hlt();
        }
        int status = -1;
        process_wait(csh_pid, &status);
        kprintf("[csh ] exited with %d\n", status);
    }
}
