/* Zhaoxu OS - ADE Aqua menu bar (desktop chrome, separate module). */
extern "C" {
#include <kprintf.h>
#include <rtc.h>
#include <smp.h>
#include <types.h>
}

#include "ade_internal.h"
#include "winmgr/dwm.h"

namespace {

u32 g_clock_second = 0xFFFFFFFFu;

} // namespace

int ade_menubar_height(void)
{
    return ade_ui(30);
}

void ade_menubar_paint(void)
{
    struct render_surface *screen = ade_screen();
    if (!screen)
        return;

    int height = ade_menubar_height();
    int text_y = (height - render_line_height()) / 2 + 1;
    render_vgrad(0, 0, screen->width, height, 0xFBFDFE, 0xD7E3EF);
    render_blend_rect(0, 0, screen->width, height, 0xFFFFFF, 70);
    render_hline(0, height - 1, screen->width, 0x7E93A8);

    int x = ade_ui(14);
    int logo = ade_ui(20);
    int logo_y = (height - logo) / 2;
    render_rounded_rect(x, logo_y, logo, logo, logo / 2, 0x1E6FB8, 255);
    render_rounded_rect(x + 2, logo_y + 2, logo - 4, logo / 2, logo / 3,
                        0xFFFFFF, 110);
    render_text_center("Z", x + logo / 2, logo_y + 1, 0xFFFFFF, 1);
    x += logo + ade_ui(12);

    x += render_text("Zhaoxu OS", x, text_y, 0x16293C, 1) + ade_ui(20);
    x += render_text("文件", x, text_y, 0x36495C, 1) + ade_ui(18);
    x += render_text("编辑", x, text_y, 0x36495C, 1) + ade_ui(18);
    x += render_text("显示", x, text_y, 0x36495C, 1) + ade_ui(18);
    render_text("帮助", x, text_y, 0x36495C, 1);

    struct rtc_time now;
    char clock[16];
    if (rtc_read(&now) == 0) {
        ksnprintf(clock, sizeof(clock), "%02u:%02u:%02u",
                  (unsigned)now.hour, (unsigned)now.minute,
                  (unsigned)now.second);
        g_clock_second = (u32)now.hour * 3600 + (u32)now.minute * 60 +
                         (u32)now.second;
    } else {
        ksnprintf(clock, sizeof(clock), "--:--:--");
    }
    int clock_w = render_text_width(clock, 1);
    render_text(clock, screen->width - clock_w - ade_ui(26), text_y, 0x16293C,
                1);

    char badge[40];
    ksnprintf(badge, sizeof(badge), "ADE · DWM · %u CPU", smp_cpu_count());
    int badge_w = render_text_width(badge, 1);
    render_text(badge, screen->width - clock_w - badge_w - ade_ui(52),
                text_y, 0x51606F, 1);
}

void ade_menubar_tick(void)
{
    struct rtc_time now;
    if (rtc_read(&now) != 0)
        return;
    u32 second = (u32)now.hour * 3600 + (u32)now.minute * 60 +
                 (u32)now.second;
    if (second != g_clock_second) {
        g_clock_second = second;
        struct render_surface *screen = ade_screen();
        if (screen)
            dwm_invalidate_rect(screen->width - ade_ui(240), 0,
                                ade_ui(240), ade_menubar_height());
    }
}
