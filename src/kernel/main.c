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
#include <fbcon.h>
#include <fs_rpc.h>
#include <gdt.h>
#include <idt.h>
#include <intc.h>
#include <io.h>
#include <klog.h>
#include <panic.h>
#include <kprintf.h>
#include <mem.h>
#include <mmu.h>
#include <personality.h>
#include <process.h>
#include <smp.h>
#include <splash.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <tty.h>
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
    /* The boot animation owns the screen until the Ring3 display service is
     * ready; setup discs skip it (splash_begin checks install_mode). */
    splash_begin(&g_boot);

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Zhaoxu OS %s \"%s\" (build %s)\n",
            ZHAOXU_VERSION, ZHAOXU_CODENAME, ZHAOXU_BUILD);
    console_reset_color();
    kprintf("\n");

#if PANIC_BOOT_TEST == 1
    /* Paint the fatal screen straight from ascii.md and halt.  A real fault
     * cannot be used: the kernel identity-maps physical 0, so a write there
     * would not trap. */
    panic_red_screen(14, "Page Fault", 0, (u64)(usize)&panic_red_screen,
                     0x10000000000ULL, 1);
#endif

    /* Reserve the personality VM region before processes and filesystems
     * start allocating. */
    if (personality_init() != 0)
        kprintf("[per ] personality API unavailable\n");
    else if (personality_start(&g_boot) != 0)
        kprintf("[per ] personality boot handoff failed\n");

#if FREEBSD_PERSONALITY == 1
    if (personality_boot_info() &&
        freebsd_personality_start(personality_boot_info()) != 0)
        kprintf("[bsd ] personality start returned an error\n");
#endif

    mmu_init();
    cpu_init();
    timer_init(1000);
    process_init();
    tty_init();
    splash_stage(10, "Memory and CPU");

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
    splash_stage(30, "Interrupts and CPUs");

#if SPLASH_BOOT_TEST == 1
    /* Exercise the real shape of boot: several quick stages, then a long
     * wait like the driver service.  Freeze afterwards so a screenshot shows
     * that the bar kept creeping during the wait instead of sitting still. */
    for (u32 step = 30; step <= 50; step += 2) {
        splash_stage(step, "Boot animation test");
        timer_sleep_ms(60);
    }
    splash_stage(52, "Driver service");
    timer_sleep_ms(8000);
    splash_finish();
    for (;;)
        hlt();
#endif

    vfs_init();
    splash_stage(45, "Filesystem core");

    /* The bar keeps creeping while these calls wait on Ring3; the stage is
     * announced before the wait so it never looks frozen at the previous
     * milestone. */
    splash_stage(52, "Driver service");
    kprintf("[dev ] starting Ring3 driver service\n");
    if (dev_start() == 0)
        kprintf("[dev ] driver service ready\n");
    else
        kprintf("[dev ] driver service unavailable\n");
    splash_stage(62, "Driver service");

    splash_stage(70, "Storage service");
    kprintf("[fs  ] starting Ring3 ext4 service\n");
    if (fs_service_start() == 0)
        kprintf("[fs  ] ext4 service ready\n");
    else
        kprintf("[fs  ] ext4 service unavailable\n");
    splash_stage(78, "Storage service");

#if PERSONALITY_SELFTEST == 1
    if (personality_selftest() == 0)
        kprintf("[per ] M1 personality selftest PASS\n");
    else
        kprintf("[per ] M1 personality selftest FAIL\n");
    for (;;)
        hlt();
#endif

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
    splash_stage(90, "Boot volume");

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
        status = vfs_stat("/bin/aqua", &is_dir, &size);
        kprintf("[m0 ] stat /bin/aqua -> %d, dir %d, %llu bytes\n",
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

#if M5_BOOT_TEST == 1
    kprintf("[m5 ] running /data/M5_TEST.ZXE\n");
    u64 m5_pid = process_spawn("/data/M5_TEST.ZXE", 0, NULL);
    if (m5_pid) {
        int m5_status = -1;
        process_wait(m5_pid, &m5_status);
        kprintf("[m5 ] M5_TEST exit %d\n", m5_status);
    } else {
        kprintf("[m5 ] M5_TEST spawn failed\n");
    }
    for (;;)
        hlt();
#endif

#if FT_BOOT_TEST == 1
    kprintf("[ft ] running /mnt/FT_TEST.ZXE\n");
    u64 ft_pid = process_spawn("/mnt/FT_TEST.ZXE", 0, NULL);
    if (ft_pid) {
        int ft_status = -1;
        process_wait(ft_pid, &ft_status);
        kprintf("[ft ] FT_TEST exit %d\n", ft_status);
    } else {
        kprintf("[ft ] FT_TEST spawn failed\n");
    }
    for (;;)
        hlt();
#endif

#if GL_BOOT_TEST == 1
    kprintf("[gl ] running /mnt/GL_TEST.ZXE\n");
    u64 gl_pid = process_spawn("/mnt/GL_TEST.ZXE", 0, NULL);
    if (gl_pid) {
        int gl_status = -1;
        process_wait(gl_pid, &gl_status);
        kprintf("[gl ] GL_TEST exit %d\n", gl_status);
    } else {
        kprintf("[gl ] GL_TEST spawn failed\n");
    }
    for (;;)
        hlt();
#endif

#if VK_BOOT_TEST == 1
    kprintf("[vk ] running /mnt/VK_TEST.ZXE\n");
    u64 vk_pid = process_spawn("/mnt/VK_TEST.ZXE", 0, NULL);
    if (vk_pid) {
        int vk_status = -1;
        process_wait(vk_pid, &vk_status);
        kprintf("[vk ] VK_TEST exit %d\n", vk_status);
    } else {
        kprintf("[vk ] VK_TEST spawn failed\n");
    }
    for (;;)
        hlt();
#endif

#if FE_BOOT_TEST == 1
    kprintf("[fe ] running /data/FORKEXEC.ZXE\n");
    u64 fe_pid = process_spawn("/data/FORKEXEC.ZXE", 0, NULL);
    if (fe_pid) {
        int fe_status = -1;
        process_wait(fe_pid, &fe_status);
        kprintf("[fe ] FORKEXEC exit %d\n", fe_status);
    } else {
        kprintf("[fe ] FORKEXEC spawn failed\n");
    }
    for (;;)
        hlt();
#endif

#if POSIX_BOOT_TEST == 1
    kprintf("[posix] running /data/POSIX_TEST.ZXE\n");
    u64 posix_pid = process_spawn("/data/POSIX_TEST.ZXE", 0, NULL);
    if (posix_pid) {
        int posix_status = -1;
        process_wait(posix_pid, &posix_status);
        kprintf("[posix] POSIX_TEST exit %d\n", posix_status);
    } else {
        kprintf("[posix] POSIX_TEST spawn failed\n");
    }
    for (;;)
        hlt();
#endif

#if BRK_BOOT_TEST == 1
    /* Skipped in install mode so the same ISO still runs Setup. */
    if (!g_boot.install_mode) {
        kprintf("[brk ] running /data/BRK_TEST.ZXE\n");
        u64 brk_pid = process_spawn("/data/BRK_TEST.ZXE", 0, NULL);
        if (brk_pid) {
            int brk_status = -1;
            process_wait(brk_pid, &brk_status);
            kprintf("[brk ] BRK_TEST exit %d\n", brk_status);
        } else {
            kprintf("[brk ] BRK_TEST spawn failed\n");
        }
        for (;;)
            hlt();
    }
#endif

#if ABI_BOOT_TEST == 1
    /* Skipped in install mode so the same ISO still runs Setup. */
    if (!g_boot.install_mode) {
        static const struct {
            const char *path;
            const char *label;
        } abi_tests[] = {
            { "/data/ANON_TEST.ZXE", "ANON_TEST" },
            { "/data/TLS_TEST.ZXE", "TLS_TEST" },
            { "/data/FUTEX_TEST.ZXE", "FUTEX_TEST" },
            { "/data/SIGNAL_TEST.ZXE", "SIGNAL_TEST" },
            { "/data/INFO_TEST.ZXE", "INFO_TEST" },
            { "/data/ELF_TEST.BIN", "ELF_TEST" },
        };
        for (usize i = 0; i < sizeof(abi_tests) / sizeof(abi_tests[0]); i++) {
            kprintf("[abi ] running %s\n", abi_tests[i].path);
            u64 abi_pid = process_spawn(abi_tests[i].path, 0, NULL);
            if (abi_pid) {
                int abi_status = -1;
                process_wait(abi_pid, &abi_status);
                kprintf("[abi ] %s exit %d\n", abi_tests[i].label, abi_status);
            } else {
                kprintf("[abi ] %s spawn failed\n", abi_tests[i].label);
            }
        }
        for (;;)
            hlt();
    }
#endif

#if NLIBC_BOOT_TEST == 1
    kprintf("[nlibc] running /data/NLIB_HELLO.ZXE\n");
    u64 nlibc_pid = process_spawn("/data/NLIB_HELLO.ZXE", 0, NULL);
    if (nlibc_pid) {
        int nlibc_status = -1;
        process_wait(nlibc_pid, &nlibc_status);
        kprintf("[nlibc] NLIB_HELLO exit %d\n", nlibc_status);
    } else {
        kprintf("[nlibc] NLIB_HELLO spawn failed\n");
    }
    kprintf("[nlibc] running /data/NCURSES_HELLO.ZXE\n");
    u64 curses_pid = process_spawn("/data/NCURSES_HELLO.ZXE", 0, NULL);
    if (curses_pid) {
        int curses_status = -1;
        process_wait(curses_pid, &curses_status);
        kprintf("[nlibc] NCURSES_HELLO exit %d\n", curses_status);
    } else {
        kprintf("[nlibc] NCURSES_HELLO spawn failed\n");
    }
    for (;;)
        hlt();
#endif

#if TOYBOX_BOOT_TEST == 1
    kprintf("[klog] %u bytes buffered\n", klog_size());
    kprintf("[toybox] starting /bin/toybox mkpasswd\n");
    {
        char *arguments[8] = { "/bin/toybox", "mkpasswd", "-m", "sha512",
                               "-S", "saltstring", "password", NULL };
        u64 toybox_pid = process_spawn("/bin/toybox", 7, arguments);
        if (toybox_pid) {
            int toybox_status = -1;
            process_wait(toybox_pid, &toybox_status);
            kprintf("[toybox] TOYBOX exit %d\n", toybox_status);
        } else {
            kprintf("[toybox] spawn failed\n");
        }
    }
    {
        /* dmesg reads the kernel log ring through SYS_KLOG (81). */
        char *arguments[3] = { "/bin/toybox", "dmesg", NULL };
        u64 dmesg_pid = process_spawn("/bin/toybox", 2, arguments);
        if (dmesg_pid) {
            int dmesg_status = -1;
            process_wait(dmesg_pid, &dmesg_status);
            kprintf("[toybox] DMESG exit %d\n", dmesg_status);
        } else {
            kprintf("[toybox] dmesg spawn failed\n");
        }
    }
    {
        /* mkdir/rmdir exercise syscalls 82 and 83 on the rootfs. */
        char *arguments[4] = { "/bin/toybox", "mkdir", "/data/NEWDIR", NULL };
        u64 mkdir_pid = process_spawn("/bin/toybox", 3, arguments);
        if (mkdir_pid) {
            int status = -1;
            process_wait(mkdir_pid, &status);
            int is_dir = 0;
            u64 size = 0;
            int stat_result = vfs_stat("/data/NEWDIR", &is_dir, &size);
            kprintf("[mkdir] exit %d stat %d dir %d\n", status, stat_result,
                    is_dir);
        } else {
            kprintf("[mkdir] spawn failed\n");
        }
        char *remove_arguments[4] = { "/bin/toybox", "rmdir", "/data/NEWDIR",
                                      NULL };
        u64 rmdir_pid = process_spawn("/bin/toybox", 3, remove_arguments);
        if (rmdir_pid) {
            int status = -1;
            process_wait(rmdir_pid, &status);
            int is_dir = 0;
            u64 size = 0;
            int stat_result = vfs_stat("/data/NEWDIR", &is_dir, &size);
            kprintf("[rmdir] exit %d stat %d\n", status, stat_result);
        } else {
            kprintf("[rmdir] spawn failed\n");
        }
    }
    {
        /* rename copies a lower-only file up, then moves it. */
        char *arguments[5] = { "/bin/toybox", "mv", "/data/HELLO.TXT",
                               "/data/RENAMED.TXT", NULL };
        u64 rename_pid = process_spawn("/bin/toybox", 4, arguments);
        if (rename_pid) {
            int status = -1;
            process_wait(rename_pid, &status);
            int is_dir = 0;
            u64 size = 0;
            int old_result = vfs_stat("/data/HELLO.TXT", &is_dir, &size);
            int new_result = vfs_stat("/data/RENAMED.TXT", &is_dir, &size);
            kprintf("[rename] exit %d old %d new %d\n", status, old_result,
                    new_result);
        } else {
            kprintf("[rename] spawn failed\n");
        }
    }
    for (;;)
        hlt();
#endif

#if WLSTOCK_BOOT_TEST == 1
    kprintf("[wlstock] starting /bin/wayland\n");
    u64 wlstock_server = process_spawn("/bin/wayland", 0, NULL);
    if (wlstock_server) {
        timer_sleep_ms(100);
        u64 wlstock_client = process_spawn("/data/WLSTOCK.ZXE", 0, NULL);
        if (wlstock_client) {
            int wlstock_status = -1;
            process_wait(wlstock_client, &wlstock_status);
            kprintf("[wlstock] WLSTOCK exit %d\n", wlstock_status);
        } else {
            kprintf("[wlstock] WLSTOCK spawn failed\n");
        }
    } else {
        kprintf("[wlstock] wayland spawn failed\n");
    }
    for (;;)
        hlt();
#endif

    /* Setup media: text-mode setup only.  The keyboard driver feeds the tty
     * the setup program reads; no display service and no compositor run. */
    if (g_boot.install_mode && g_boot.framebuffer_base) {
        splash_finish();
        /* The setup disc has no display service: give the text installer a
         * real screen instead of leaving the framebuffer black. */
        if (fbcon_init(&g_boot)) {
            console_set_sink(fbcon_putc);
            console_set_color_sink(fbcon_set_color);
            console_set_kernel_mute(1);
        }
        console_set_color(COLOR_LIGHT_GRAY, COLOR_BLUE);
        kprintf("\nStarting Setup.\n\n");
        console_reset_color();

        u64 drivers_pid = process_spawn("/bin/drivers.udrv", 0, NULL);
        if (!drivers_pid)
            kprintf("[install] /bin/drivers unavailable; input will not work\n");
        u64 setup_pid = process_spawn("/bin/apps/install.zxe", 0, NULL);
        if (setup_pid) {
            int setup_status = -1;
            process_wait(setup_pid, &setup_status);
            kprintf("[install] setup exited with %d\n", setup_status);
        } else {
            kprintf("[install] /bin/apps/install.zxe unavailable\n");
        }
        kprintf("[install] setup stopped; halting.\n");
        for (;;)
            hlt();
    }

    /* M1: the KMS service owns virtio-gpu scanout and runs before the
     * desktop.  Without a virtio-gpu it exits immediately. */
    splash_stage(96, "Display service");
    u64 display_pid = process_spawn("/bin/display.udrv", 0, NULL);
    if (display_pid)
        kprintf("[display] Ring3 KMS service pid %llu\n", display_pid);
    else
        kprintf("[display] /bin/display unavailable\n");

    if (g_boot.framebuffer_base) {
        /* Hand the screen to Aqua: the splash frame stays until its first
         * compositor present. */
        splash_finish();
        console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
        kprintf("\nBoot complete. Starting Aqua Desktop Environment.\n\n");
        console_reset_color();

        /* Rule 31: Orchestrd is the service manager and starts the desktop.
         * The direct spawn below stays as a fallback for images without it. */
        u64 orchestrd_pid = process_spawn("/bin/orchestrd", 0, NULL);
        if (orchestrd_pid) {
            int status = -1;
            process_wait(orchestrd_pid, &status);
            kprintf("[orchestrd] exited with %d\n", status);
            for (;;)
                hlt();
        }
        kprintf("[orchestrd] /bin/orchestrd unavailable; starting aqua directly\n");

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
    kprintf("\nBoot complete. No framebuffer; starting headless services.\n\n");
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

    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("\nBoot complete. No framebuffer; starting bash.\n\n");
    console_reset_color();

    for (;;) {
        u64 shell_pid = process_spawn("/bin/bash", 0, NULL);
        if (!shell_pid) {
            console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
            kprintf("[bash] /bin/bash unavailable; halting.\n");
            console_reset_color();
            for (;;)
                hlt();
        }
        int status = -1;
        process_wait(shell_pid, &status);
        kprintf("[bash] exited with %d\n", status);
    }
}
