/* Zhaoxu OS - desktop Welcome application (outside ADE). */
extern "C" {
#include <cpu.h>
#include <kprintf.h>
#include <smp.h>
#include <types.h>
#include <version.h>
}

#include "../../ADE/ade.h"

namespace {

void paint(const struct ade_view *view)
{
    int x = view->x + ade_ui(30);
    int y = view->y + ade_ui(24);
    int line = ade_line_height();

    ade_text("Zhaoxu OS", x, y, 0x123A5A, 2);
    y += line * 2 + ade_ui(2);
    ade_text("兆旭操作系统 · ADE 桌面环境（Aqua）", x, y, 0x2B6FA8, 1);
    y += line + ade_ui(14);

    char text[160];
    ksnprintf(text, sizeof(text), "版本 %s \"%s\" · 内核 %s Kernel %s",
              ZHAOXU_VERSION, ZHAOXU_CODENAME, ZHAOXU_KERNEL,
              ZHAOXU_KERNEL_VERSION);
    ade_text(text, x, y, 0x2A4258, 1);
    y += line + ade_ui(6);
    ade_text("窗口管理器 DWM · 系统渲染 API · 应用与桌面分离",
             x, y, 0x4A627A, 1);
    y += line + ade_ui(6);
    ksnprintf(text, sizeof(text), "在线处理器 %u · 字体由系统提供",
              smp_cpu_count());
    ade_text(text, x, y, 0x4A627A, 1);
    y += line + ade_ui(16);

    ade_hline(x, y, view->w - ade_ui(60), 0xD2DDE7);
    y += ade_ui(14);

    ade_text("桌面已经就绪", x, y, 0x123A5A, 1);
    y += line + ade_ui(4);
    ade_text("· 点击 Dock 图标打开应用，拖动标题栏移动窗口。",
             x, y, 0x2A4258, 1);
    y += line + ade_ui(2);
    ade_text("· 点击窗口左侧红色按钮关闭，ESC 也可关闭当前窗口。",
             x, y, 0x2A4258, 1);
    y += line + ade_ui(2);
    ade_text("· 应用位于 src/apps/UserApps，只通过 ade.h 使用桌面 API。",
             x, y, 0x2A4258, 1);
    y += line + ade_ui(12);
    ade_text("Aqua · DWM · ADE", x, y, 0x2B6FA8, 1);
}

} // namespace

extern const struct ade_app welcome_app = {
    "welcome", "欢迎", "Z",
    "<svg viewBox=\"0 0 100 100\"><rect x=\"6\" y=\"6\" width=\"88\" "
    "height=\"88\" rx=\"22\" fill=\"#2F7FD0\"/><path d=\"M30 28 L70 28 "
    "L70 39 L43 61 L70 61 L70 72 L30 72 L30 61 L57 39 L30 39 Z\" "
    "fill=\"#FFFFFF\"/></svg>",
    0x2F7FD0, 700, 430,
    nullptr, paint, nullptr, nullptr, nullptr,
};
