/* Zhaoxu OS - ADE "About" panel (desktop chrome, not an application). */
#include <version.h>

#include "ade.h"
#include "ade_internal.h"

namespace {

void about_paint(const struct ade_view *view)
{
    int x = view->x;
    int y = view->y;
    int w = view->w;

    ade_fill(x, y, w, view->h, 0xF7FAFD);
    ade_vgrad(x, y, w, ade_ui(64), 0x1E6FB8, 0x2E86C8);
    ade_text("关于 Zhaoxu OS", x + ade_ui(20), y + ade_ui(18), 0xFFFFFF, 2);
    ade_fill(x + ade_ui(20), y + ade_ui(86), w - ade_ui(40), 1, 0xD7E3EF);

    int line = y + ade_ui(110);
    int step = ade_ui(36);
    ade_text("Zhaoxu OS " ZHAOXU_VERSION " build " ZHAOXU_BUILD " \"" ZHAOXU_CODENAME "\"",
             x + ade_ui(24), line, 0x16293C, 2);
    line += step;
    ade_text("内核：" ZHAOXU_KERNEL " Kernel " ZHAOXU_KERNEL_VERSION "（微内核）",
             x + ade_ui(24), line, 0x36495C, 1);
    line += step;
    ade_text("桌面：Aqua Desktop Environment · 窗口管理器：Desktop Window Manager",
             x + ade_ui(24), line,
             0x36495C, 1);
    line += step;
    ade_text("驱动与应用运行在 Ring 3", x + ade_ui(24), line, 0x36495C, 1);
    line += step;
    ade_text(ZOS_API_BANNER, x + ade_ui(24), line, 0x51606F, 1);
}

struct ade_app g_about = {
    "about", "关于", "i", nullptr, 0x1E6FB8, 540, 340,
    nullptr, about_paint, nullptr, nullptr, nullptr, nullptr, 1, nullptr
};

} // namespace

const struct ade_app *ade_about_app(void)
{
    g_about.icon_svg = ade_icon_about();
    return &g_about;
}
