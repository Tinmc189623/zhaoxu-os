/* Zhaoxu OS - system monitor application (outside ADE). */
extern "C" {
#include <ahci.h>
#include <boot.h>
#include <cpu.h>
#include <kprintf.h>
#include <mem.h>
#include <smp.h>
#include <thread.h>
#include <timer.h>
#include <types.h>
}

#include "../../ADE/ade.h"

namespace {

void bar(int x, int y, int w, int h, u32 percent, u32 color)
{
    ade_round(x, y, w, h, h / 2, 0xDCE6EF);
    if (percent > 100)
        percent = 100;
    int filled = (int)((u64)(w - 2) * percent / 100);
    if (filled > 0)
        ade_round(x + 1, y + 1, filled, h - 2, (h - 2) / 2, color);
}

void paint(const struct ade_view *view)
{
    int x = view->x + ade_ui(22);
    int y = view->y + ade_ui(18);
    int line = ade_line_height();
    int width = view->w - ade_ui(44);
    char text[128];

    struct pmm_stats physical;
    pmm_get_stats(&physical);
    u64 heap_total = 0, heap_used = 0, heap_free = 0;
    heap_get_stats(&heap_total, &heap_used, &heap_free);

    ade_text("内存", x, y, 0x123A5A, 1);
    y += line + ade_ui(6);

    u32 used_pct = physical.managed_pages
                       ? (u32)(physical.used_pages * 100 /
                               physical.managed_pages)
                       : 0;
    ksnprintf(text, sizeof(text), "物理 %llu / %llu MiB",
              (physical.used_pages * PAGE_SIZE) >> 20,
              (physical.managed_pages * PAGE_SIZE) >> 20);
    ade_text(text, x, y, 0x5A6E82, 1);
    y += line + ade_ui(2);
    bar(x, y, width, ade_ui(12), used_pct, 0x2F7FD0);
    y += ade_ui(24);

    u32 heap_pct = heap_total ? (u32)(heap_used * 100 / heap_total) : 0;
    ksnprintf(text, sizeof(text), "内核堆 %llu / %llu KiB",
              heap_used >> 10, heap_total >> 10);
    ade_text(text, x, y, 0x5A6E82, 1);
    y += line + ade_ui(2);
    bar(x, y, width, ade_ui(12), heap_pct, 0x2E9E6B);
    y += ade_ui(26);

    ade_hline(x, y, width, 0xD5E0EA);
    y += ade_ui(12);

    const struct cpu_info *cpu = cpu_get_info();
    const struct boot_info *boot = boot_get_info();
    u64 seconds = timer_ticks() / (timer_hz() ? timer_hz() : 1);

    ade_text("系统", x, y, 0x123A5A, 1);
    y += line + ade_ui(2);
    ksnprintf(text, sizeof(text), "CPU  %s", cpu->brand);
    ade_text(text, x, y, 0x5A6E82, 1);
    y += line + ade_ui(1);
    ksnprintf(text, sizeof(text), "在线 %u / 逻辑 %u · 抢占 1 kHz",
              smp_cpu_count(), cpu->logical_cpus ? cpu->logical_cpus : 1);
    ade_text(text, x, y, 0x5A6E82, 1);
    y += line + ade_ui(1);
    ksnprintf(text, sizeof(text), "运行 %llu:%02llu:%02llu",
              seconds / 3600, (seconds / 60) % 60, seconds % 60);
    ade_text(text, x, y, 0x5A6E82, 1);
    y += line + ade_ui(1);
    ksnprintf(text, sizeof(text), "显示 %ux%u · AHCI %d 盘",
              boot->width, boot->height, ahci_disk_count());
    ade_text(text, x, y, 0x5A6E82, 1);
    y += line + ade_ui(8);

    ade_text("每 CPU tick", x, y, 0x123A5A, 1);
    y += line + ade_ui(2);
    int px = x;
    for (u32 i = 0; i < smp_cpu_count() && i < 8; i++) {
        ksnprintf(text, sizeof(text), "c%u %llu", i, sched_cpu_ticks(i));
        px += ade_text(text, px, y, 0x17435F, 1) + ade_ui(14);
    }
}

void tick(void)
{
    ade_invalidate();
}

} // namespace

extern const struct ade_app monitor_app = {
    "monitor", "监视器", "M",
    "<svg viewBox=\"0 0 100 100\"><rect x=\"6\" y=\"6\" width=\"88\" "
    "height=\"88\" rx=\"22\" fill=\"#2D8FB8\"/><rect x=\"24\" y=\"28\" "
    "width=\"52\" height=\"34\" rx=\"4\" fill=\"#FFFFFF\"/><rect x=\"33\" "
    "y=\"67\" width=\"34\" height=\"6\" rx=\"3\" fill=\"#FFFFFF\"/>"
    "<rect x=\"44\" y=\"60\" width=\"12\" height=\"8\" fill=\"#FFFFFF\"/>"
    "</svg>",
    0x2D8FB8, 640, 470,
    nullptr, paint, nullptr, nullptr, tick,
};
