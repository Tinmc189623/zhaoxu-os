#include <acpi.h>
#include <ahci.h>
#include <boot.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>
#include <gdt.h>
#include <idt.h>
#include <io.h>
#include <keyboard.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <rtc.h>
#include <shell.h>
#include <string.h>
#include <timer.h>
#include <ttf.h>
#include <types.h>
#include <vfs.h>
#include <version.h>

static struct boot_info g_boot;

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

    console_init(&g_boot);
    console_clear();

    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    kprintf("Zhaoxu OS %s \"%s\"\n", ZHAOXU_VERSION, ZHAOXU_CODENAME);
    console_set_color(COLOR_DARK_GRAY, COLOR_BLACK);
    kprintf("A multi-language operating system: FASM + C + Ada + C++\n");
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

    struct rtc_time now;
    if (rtc_read(&now) == 0) {
        kprintf("[rtc ] %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                now.year, now.month, now.day,
                now.hour, now.minute, now.second);
    }

    timer_init(100);
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

    kprintf("\n[dev ] probing storage and filesystems\n");
    ahci_init();
    vfs_init();
    if (vfs_mount_data() == 0)
        kprintf("[vfs ] /mnt mounted (FAT32 \"%s\")\n", vfs_fat_label());
    else if (vfs_data_fallback())
        kprintf("[vfs ] /mnt -> embedded data mirror (no volume attached)\n");
    else
        kprintf("[vfs ] /mnt unavailable, using ramfs only\n");

    if (vfs_fat_mounted()) {
        int font_status = ttf_autoload("/mnt/WQY.TTC");
        if (font_status == 0)
            kprintf("[ttf ] WQY.TTC loaded: upem %u, %u glyphs, cmap fmt %d\n",
                    ttf_upem(), ttf_num_glyphs(), ttf_cmap_fmt());
        else
            kprintf("[ttf ] CJK font unavailable (%d)\n", font_status);
    } else {
        kprintf("[ttf ] no CJK font (no data volume)\n");
    }
    process_init();

    console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    kprintf("\nBoot complete. Entering Yuanshuo shell.\n\n");
    console_reset_color();

    char *init_argv[] = { (char *)"/bin/init" };
    kprintf("[init] starting /bin/init in ring 3\n");
    int init_status = process_run("/bin/init", 1, init_argv);
    kprintf("[init] /bin/init exited with status %d\n\n", init_status);

    shell_run();
}
