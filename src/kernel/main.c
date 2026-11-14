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
#include <hpet.h>
#include <idt.h>
#include <intc.h>
#include <io.h>
#include <kprintf.h>
#include <mem.h>
#include <mmu.h>
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
    kprintf("Zhaoxu OS %s \"%s\" / %s kernel %s (build %s)\n",
            ZHAOXU_VERSION, ZHAOXU_CODENAME, ZHAOXU_KERNEL,
            ZHAOXU_KERNEL_VERSION, ZHAOXU_BUILD);
    console_set_color(COLOR_DARK_GRAY, COLOR_BLACK);
    kprintf("Microkernel: drivers and applications run in ring 3\n");
    console_reset_color();
    kprintf("\n");

    mmu_init();
    hpet_init();
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
    intc_x86_init();
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

    kprintf("[vfs ] probing removable filesystems\n");
    media_probe_all();

    if (g_boot.wallpaper_phys && g_boot.wallpaper_size)
        kprintf("[boot] default wallpaper %llu bytes from the boot volume\n",
                g_boot.wallpaper_size);

#if PROC_SELFTEST == 1
    process_selftest();
#elif PROC_SELFTEST == 2
    ipc_selftest();
#endif

#if M0_BOOT_TEST == 1
    {
        int is_dir = 0;
        u64 size = 0;
        int status = vfs_stat("/data/NET_TEST.ZXE", &is_dir, &size);
        kprintf("[m0 ] stat /data/NET_TEST.ZXE -> %d, dir %d, %llu bytes\n",
                status, is_dir, size);
        status = vfs_stat("/data/HELLO.ZXE", &is_dir, &size);
        kprintf("[m0 ] stat /data/HELLO.ZXE -> %d, dir %d, %llu bytes\n",
                status, is_dir, size);
        status = vfs_stat("/bin/8sh", &is_dir, &size);
        kprintf("[m0 ] stat /bin/8sh -> %d, dir %d, %llu bytes\n",
                status, is_dir, size);
    }
    kprintf("[m0 ] running /data/NET_TEST.ZXE\n");
    u64 m0_pid = process_spawn("/data/NET_TEST.ZXE", 0, NULL);
    if (m0_pid) {
        int m0_status = -1;
        process_wait(m0_pid, &m0_status);
        kprintf("[m0 ] NET_TEST exit %d\n", m0_status);
    } else {
        kprintf("[m0 ] NET_TEST spawn failed\n");
    }
    for (;;)
        hlt();
#endif

    /* M1: the KMS service owns virtio-gpu scanout and runs before the
     * desktop.  Without a virtio-gpu it exits immediately. */
    u64 display_pid = process_spawn("/bin/display", 0, NULL);
    if (display_pid)
        kprintf("[display] Ring3 KMS service pid %llu\n", display_pid);
    else
        kprintf("[display] /bin/display unavailable\n");

    if (g_boot.framebuffer_base) {
        console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
        kprintf("\nBoot complete. Starting Aqua Desktop Environment.\n\n");
        console_reset_color();

        u64 desktop_pid = process_spawn("/bin/aqua", 0, NULL);
        if (desktop_pid) {
            int status = -1;
            process_wait(desktop_pid, &status);
            kprintf("[aqua] desktop exited with %d\n", status);
        } else {
            kprintf("[aqua] /bin/aqua unavailable\n");
        }
        kprintf("[aqua] desktop stopped; halting.\n");
        for (;;)
            hlt();
    }

    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("\nBoot complete. No framebuffer; starting 8sh.\n\n");
    console_reset_color();

    /* M2 smoke: the Wayland server is a service; the shm demo client runs
     * once so the wire/SCM_RIGHTS path is exercised without a GUI. */
    u64 wayland_pid = process_spawn("/bin/wayland", 0, NULL);
    if (wayland_pid) {
        timer_sleep_ms(100);
        u64 demo_pid = process_spawn("/bin/wldemo", 0, NULL);
        if (demo_pid) {
            int demo_status = -1;
            process_wait(demo_pid, &demo_status);
            kprintf("[wayland] wldemo exit %d\n", demo_status);
        } else {
            kprintf("[wayland] /bin/wldemo unavailable\n");
        }
    } else {
        kprintf("[wayland] /bin/wayland unavailable\n");
    }

    for (;;) {
        u64 shell_pid = process_spawn("/bin/8sh", 0, NULL);
        if (!shell_pid) {
            console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
            kprintf("[8sh ] /bin/8sh unavailable; halting.\n");
            console_reset_color();
            for (;;)
                hlt();
        }
        int status = -1;
        process_wait(shell_pid, &status);
        kprintf("[8sh ] exited with %d\n", status);
    }
}
