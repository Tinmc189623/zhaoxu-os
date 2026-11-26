/* Zhaoxu OS - desktop Welcome application (outside ADE). */
extern "C" {
#include <types.h>
}

#include "../../ADE/ade.h"

namespace {

void paint(const struct ade_view *view)
{
    int x = view->x + ade_ui(30);
    int y = view->y + ade_ui(24);
    int line = ade_line_height();

    /* Rule 57: no OS name, kernel or brand on the desktop; the welcome
     * window only explains how to use it. */
    ade_text("桌面已经就绪", x, y, 0x123A5A, 2);
    y += line * 2 + ade_ui(2);
    ade_text("Aqua Desktop Environment", x, y, 0x2B6FA8, 1);
    y += line + ade_ui(14);

    ade_hline(x, y, view->w - ade_ui(60), 0xD2DDE7);
    y += ade_ui(14);

    ade_text("点击 Dock 图标打开应用，拖动标题栏移动窗口。",
             x, y, 0x2A4258, 1);
    y += line + ade_ui(2);
    ade_text("点击窗口左侧红色按钮关闭，ESC 也可关闭当前窗口。",
             x, y, 0x2A4258, 1);
    y += line + ade_ui(2);
    ade_text("更多应用可从 Dock 打开。", x, y, 0x2A4258, 1);
    y += line + ade_ui(12);
}

} // namespace

extern const struct ade_app welcome_app = {
    "welcome", "欢迎", "W",
    "<svg viewBox=\"0 0 100 100\"><rect x=\"6\" y=\"6\" width=\"88\" "
    "height=\"88\" rx=\"22\" fill=\"#2F7FD0\"/><rect x=\"22\" y=\"24\" "
    "width=\"56\" height=\"10\" rx=\"4\" fill=\"#FFFFFF\"/><rect x=\"22\" "
    "y=\"40\" width=\"56\" height=\"36\" rx=\"5\" fill=\"#FFFFFF\" "
    "opacity=\"0.85\"/></svg>",
    0x2F7FD0, 700, 430,
    nullptr, paint, nullptr, nullptr, nullptr,
};
