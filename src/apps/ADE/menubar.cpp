/* Zhaoxu OS - ADE Aqua menu bar (desktop chrome, separate module). */
extern "C" {
#include <kprintf.h>
#include <rtc.h>
#include <smp.h>
#include <types.h>
#include <userlib.h>
}

#include "ade_internal.h"
#include "winmgr/dwm.h"

namespace {

u32 g_clock_second = 0xFFFFFFFFu;
int g_brand_end;
int g_item_x[4];
int g_item_w[4];

/* Return 0..3 for a menu label, -1 outside every label.  Boundaries match
 * the hit strip used by ade_menubar_click(). */
int item_at(int x)
{
    for (int i = 0; i < 4; i++) {
        int left = i == 0 ? g_brand_end
                          : (g_item_x[i - 1] + g_item_w[i - 1] +
                             g_item_x[i]) / 2;
        int right = i == 3 ? g_item_x[i] + g_item_w[i] + ade_ui(8)
                           : (g_item_x[i] + g_item_w[i] +
                              g_item_x[i + 1]) / 2;
        if (x >= left && x < right)
            return i;
    }
    return -1;
}

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
    /* Let the wallpaper glow through the bar instead of painting an opaque
     * strip; the extra white wash keeps dark menu text readable on any
     * wallpaper. */
    render_blend_rect(0, 0, screen->width, height, 0xF7FAFD, 216);
    render_blend_rect(0, 0, screen->width, height / 2, 0xFFFFFF, 42);
    render_hline(0, height - 1, screen->width, 0x7E93A8);

    int x = ade_ui(14);
    int logo = ade_ui(20);
    int logo_y = (height - logo) / 2;
    render_rounded_rect(x, logo_y, logo, logo, logo / 2, 0x1E6FB8, 255);
    render_rounded_rect(x + 2, logo_y + 2, logo - 4, logo / 2, logo / 3,
                        0xFFFFFF, 110);
    render_text_center("Z", x + logo / 2, logo_y + 1, 0xFFFFFF, 1);
    x += logo + ade_ui(12);

    int brand = render_text_width("Zhaoxu OS", 1);
    render_text("Zhaoxu OS", x, text_y, 0x16293C, 1);
    g_brand_end = x + brand;
    x += brand + ade_ui(20);

    static const char *const labels[4] = {"文件", "编辑", "显示", "帮助"};
    for (int i = 0; i < 4; i++) {
        int width = render_text_width(labels[i], 1);
        g_item_x[i] = x;
        g_item_w[i] = width;
        render_text(labels[i], x, text_y, 0x36495C, 1);
        x += width + ade_ui(18);
    }

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
    ksnprintf(badge, sizeof(badge), "Aqua · Desktop Window Manager · %u CPU",
              smp_cpu_count());
    int badge_w = render_text_width(badge, 1);
    render_text(badge, screen->width - clock_w - badge_w - ade_ui(52),
                text_y, 0x51606F, 1);
}

int ade_menubar_hit(int x, int y)
{
    if (y < 0 || y >= ade_menubar_height())
        return 0;
    return x < g_brand_end || item_at(x) >= 0;
}

void ade_menubar_click(int x, int y)
{
    if (y < 0 || y >= ade_menubar_height())
        return;
    if (x < g_brand_end) {
        ade_open_app(ade_about_app());
        return;
    }
    int index = item_at(x);
    if (index < 0)
        return;
    if (index == 0)
        sys_spawn_arg("/bin/apps/install.zxe", "force");
    else if (index == 1)
        dwm_close_focus();
    else
        ade_open_app(ade_about_app());
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
