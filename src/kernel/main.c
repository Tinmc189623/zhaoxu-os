#include <acpi.h>
#include <ahci.h>
#include <apic.h>
#include <boot.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>
#include <gdt.h>
#include <idt.h>
#include <io.h>
#include <iommu.h>
#include <keyboard.h>
#include <kprintf.h>
#include <mem.h>
#include <pci.h>
#include <process.h>
#include <rtc.h>
#include <smp.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <ttf.h>
#include <types.h>
#include <vfs.h>
#include <version.h>

static struct boot_info g_boot;

#ifndef PROC_SELFTEST
#define PROC_SELFTEST 0
#endif

extern void ade_run(void) __attribute__((noreturn));

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

static int load_system_font(void)
{
    /* Fonts belong to the system: MiSans / MiSans Global / Inter are
     * preferred, with the bundled CJK fallback for disk-less boots. */
    static const char *const candidates[] = {
        "/mnt/MiSans.ttf", "/mnt/MiSans-Regular.ttf",
        "/mnt/MiSansGlobal.ttf", "/mnt/Inter.ttf",
        "/mnt/Inter-Regular.ttf", "/mnt/WQY.TTC"
    };

    for (u32 i = 0; i < ARRAY_SIZE(candidates); i++) {
        if (ttf_autoload(candidates[i]) == 0) {
            kprintf("[ttf ] %s loaded: upem %u, %u glyphs, cmap fmt %d\n",
                    candidates[i], ttf_upem(), ttf_num_glyphs(),
                    ttf_cmap_fmt());
            return 0;
        }
    }
    kprintf("[ttf ] no MiSans/Inter/WQY font available\n");
    return -1;
}

const struct boot_info *boot_get_info(void)
{
    return &g_boot;
}

static const char *pixel_format_name(u32 format)
{
    switch (format) {
    case 0: return "RGBX";
    case 1: return "BGRX";
    case 2: return "BitMask";
    case 3: return "BltOnly";
    default: return "Unknown";
    }
}

void kernel_main(struct boot_info *boot)
{
    serial_init(115200);

    if (!boot || boot->magic != BOOT_MAGIC) {
        for (;;)
            hlt();
    }
    g_boot = *boot;
    g_boot.rsdp = apic_locate_rsdp(g_boot.rsdp);

    console_init(&g_boot);
    console_clear();

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Zhaoxu OS %s \"%s\"\n", ZHAOXU_VERSION, ZHAOXU_CODENAME);
    console_set_color(COLOR_DARK_GRAY, COLOR_BLACK);
    kprintf("A multi-language operating system: FASM + C + GAS + SPARK + C++\n");
    kprintf("欢迎使用兆旭操作系统：源朔内核已启用中文渲染\n");
    console_reset_color();
    kprintf("\n");

    gdt_init();
    idt_init();
    pic_init();

    pmm_init(&g_boot);
    vmm_init(&g_boot);
    heap_init();

    cpu_init();
    acpi_init(g_boot.rsdp);
    pcie_init(g_boot.rsdp);
    iommu_init(acpi_dmar_address());

    struct rtc_time now;
    if (rtc_read(&now) == 0) {
        kprintf("[rtc ] %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                now.year, now.month, now.day,
                now.hour, now.minute, now.second);
    }

    timer_init(1000);
    keyboard_init();
    if (mouse_init((int)g_boot.width, (int)g_boot.height) > 0) {
        pic_unmask(12);
        kprintf("[mouse] PS/2 aux port ready, cursor bounds %ux%u\n",
                g_boot.width, g_boot.height);
    } else {
        kprintf("[mouse] no PS/2 mouse detected\n");
    }
    pic_unmask(0);
    pic_unmask(1);
    pic_unmask(2);
    interrupts_enable();

    kprintf("[boot] higher-half kernel at 0x%llx (phys 0x%llx, %llu KiB)\n",
            g_boot.kernel_virt_base, g_boot.kernel_phys_base,
            g_boot.kernel_size / 1024);
    kprintf("[boot] memory map: %llu bytes, %llu bytes/entry, ver %u\n",
            g_boot.memory_map_size, g_boot.memory_map_desc_size,
            g_boot.memory_map_desc_version);
    if (g_boot.framebuffer_base) {
        kprintf("[gop ] %ux%u, %u B/line, %s, fb 0x%llx\n",
                g_boot.width, g_boot.height, g_boot.pitch,
                pixel_format_name(g_boot.pixel_format),
                g_boot.framebuffer_base);
    } else {
        kprintf("[gop ] no framebuffer, serial console only\n");
    }
    kprintf("[irq ] PIC remapped, PIT %u Hz, PS/2 keyboard enabled\n",
            timer_hz());

    struct pmm_stats pmm;
    pmm_get_stats(&pmm);
    kprintf("[pmm ] used %llu MiB / free %llu MiB\n",
            (pmm.used_pages * PAGE_SIZE) >> 20,
            (pmm.free_pages * PAGE_SIZE) >> 20);

    u64 heap_total, heap_used, heap_free;
    void *probe_a = kmalloc(128);
    void *probe_b = kmalloc(4096);
    heap_get_stats(&heap_total, &heap_used, &heap_free);
    kprintf("[heap] total %llu B, used %llu B, block probe %p/%p\n",
            heap_total, heap_used, probe_a, probe_b);
    kfree(probe_a);
    kfree(probe_b);

    timer_sleep_ms(100);
    kprintf("[time] ticks after 100 ms: %llu\n", timer_ticks());

    smp_init(g_boot.rsdp);

    kprintf("\n[dev ] probing storage and filesystems\n");
    ahci_init();
    if (ahci_disk_count() > 0)
        kprintf("[ahci] disk0 port %d, %llu sectors\n",
                ahci_disk_port(0), ahci_disk_sectors(0));
    vfs_init();
    if (vfs_mount_data() == 0)
        kprintf("[vfs ] /mnt mounted (FAT32 \"%s\")\n", vfs_fat_label());
    else if (vfs_data_fallback())
        kprintf("[vfs ] /mnt -> embedded data mirror (no volume attached)\n");
    else
        kprintf("[vfs ] /mnt unavailable, using ramfs only\n");

    if (vfs_fat_mounted()) {
        if (load_system_font() == 0) {
            struct ttf_glyph glyph;
            if (ttf_render(0x4E2D, &glyph) == 0 && glyph.w && glyph.h)
                kprintf("[ttf ] glyph U+4E2D %ux%u adv %u\n",
                        glyph.w, glyph.h, glyph.adv);
            else
                kprintf("[ttf ] glyph U+4E2D raster failed\n");
        }
    } else {
        kprintf("[ttf ] no system font volume\n");
    }
    process_init();
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
            kprintf("[ade ] /bin/ade unavailable, using in-kernel fallback\n");
        }
        ade_run();
    }

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\nNo framebuffer. ADE requires GOP graphics; halting.\n");
    console_reset_color();
    for (;;)
        hlt();
}
