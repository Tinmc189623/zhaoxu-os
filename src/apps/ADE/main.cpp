/*
 * Zhaoxu OS - ADE desktop environment entry points and public app API.
 *
 * Desktop chrome lives in wallpaper.cpp / menubar.cpp / dock.cpp, window
 * management in winmgr/dwm.cpp, applications in src/apps/UserApps.
 */
#ifdef ZOS_USER
extern "C" {
#include <kprintf.h>
#include <render.h>
#include <string.h>
#include <ttf.h>
#include <types.h>
#include <userlib.h>
#include <version.h>
#include <zos_user.h>
}
#endif

#include "ade.h"
#include "ade_internal.h"
#include "display/server.h"
#include "winmgr/dwm.h"

namespace {

constexpr int kMaxApps = 8;

const struct ade_app *g_apps[kMaxApps];
int g_app_count;
struct render_surface *g_screen;

} // namespace

struct render_surface *ade_screen(void)
{
    return g_screen;
}

int ade_ui(int value)
{
    int height = g_screen ? g_screen->height : 900;
    int scaled = value * height / 900;
    if (value > 0 && scaled == 0)
        scaled = 1;
    return scaled;
}

int ade_screen_width(void)
{
    return g_screen ? g_screen->width : 0;
}

int ade_screen_height(void)
{
    return g_screen ? g_screen->height : 0;
}

void ade_register_app(const struct ade_app *app)
{
    if (!app || g_app_count >= kMaxApps)
        return;
    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i] == app)
            return;
    }
    g_apps[g_app_count++] = app;
    if (g_screen) {
        int dock = ade_dock_height();
        dwm_invalidate_rect(0, g_screen->height - dock - ade_ui(40),
                            g_screen->width, dock + ade_ui(40));
    }
}

void ade_unregister_app(const struct ade_app *app)
{
    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i] != app)
            continue;
        for (int j = i + 1; j < g_app_count; j++)
            g_apps[j - 1] = g_apps[j];
        g_app_count--;
        g_apps[g_app_count] = nullptr;
        if (g_screen) {
            int dock = ade_dock_height();
            dwm_invalidate_rect(0, g_screen->height - dock - ade_ui(40),
                                g_screen->width, dock + ade_ui(40));
        }
        return;
    }
}

int ade_app_count(void)
{
    return g_app_count;
}

const struct ade_app *ade_app_at(int index)
{
    if (index < 0 || index >= g_app_count)
        return nullptr;
    return g_apps[index];
}

void ade_open_app(const struct ade_app *app)
{
    dwm_open(app);
}

void ade_invalidate(void)
{
    dwm_invalidate_focus();
}

/* ------------------------------------------------------------------ */
/* drawing helpers forwarded to the system render API                  */
/* ------------------------------------------------------------------ */

void ade_fill(int x, int y, int w, int h, u32 rgb)
{
    render_fill_rect(x, y, w, h, rgb);
}

void ade_blend(int x, int y, int w, int h, u32 rgb, u32 alpha)
{
    render_blend_rect(x, y, w, h, rgb, alpha);
}

void ade_round(int x, int y, int w, int h, int radius, u32 rgb)
{
    render_rounded_rect(x, y, w, h, radius, rgb, 255);
}

int ade_text(const char *text, int x, int y, u32 fg, int scale)
{
    return render_text(text, x, y, fg, scale);
}

int ade_text_width(const char *text, int scale)
{
    return render_text_width(text, scale);
}

int ade_text_center(const char *text, int cx, int y, u32 fg, int scale)
{
    return render_text_center(text, cx, y, fg, scale);
}

int ade_line_height(void)
{
    return render_line_height();
}

void ade_hline(int x, int y, int w, u32 rgb)
{
    render_fill_rect(x, y, w, 1, rgb);
}

void ade_vgrad(int x, int y, int w, int h, u32 top, u32 bottom)
{
    render_vgrad(x, y, w, h, top, bottom);
}

void ade_border(int x, int y, int w, int h, u32 rgb)
{
    render_border(x, y, w, h, rgb);
}

/* ------------------------------------------------------------------ */
/* boot entry                                                          */
/* ------------------------------------------------------------------ */

#ifdef ZOS_USER
static void ade_setup(const struct render_surface *screen)
{
    (void)screen;
    struct render_surface *wallpaper = ade_build_wallpaper();

    ade_register_app(ade_about_app());

    struct dwm_ops ops;
    memset(&ops, 0, sizeof(ops));
    ops.wallpaper = wallpaper;
    ops.menu_h = ade_menubar_height();
    ops.dock_h = ade_dock_height();
    ops.paint_menubar = ade_menubar_paint;
    ops.paint_dock = ade_dock_paint;
    ops.dock_click = ade_dock_click;
    ops.dock_hover = ade_dock_hover;
    ops.menu_click = ade_menubar_click;
    ops.tick = ade_menubar_tick;
    ops.poll = ade_display_poll;

    dwm_init(&ops);
}

extern "C" int user_main(long *stack)
{
    (void)stack;

    struct zos_fb_info fb;
    if (sys_fb_map(&fb) < 0) {
        kprintf("[ade ] no framebuffer from kernel\n");
        return 1;
    }

    render_set_page_allocator(zos_alloc_pages);
    render_init_surface((u32 *)(uintptr_t)fb.address, (int)fb.width,
                        (int)fb.height, (int)fb.stride, (u32)fb.format);
    g_screen = render_screen();
    if (!g_screen) {
        kprintf("[ade ] render surface unavailable\n");
        return 2;
    }

    u64 font_size = 0;
    u8 *font = zos_load_font(&font_size);
    int font_status = font ? ttf_load(font, font_size) : -1;
    if (font_status == 0)
        ttf_set_pixel_size(fb.height >= 1000 ? 24 : 20);
    else {
        kprintf("[ade ] no system font (data %llu, load %d)\n", font_size,
                font_status);
    }

    ade_setup(g_screen);
    ade_display_init();
    kprintf("[ade ] Ring3 display server %s (DWM, Aqua), %d app(s)\n",
            ZHAOXU_VERSION, g_app_count);
    kprintf("[ade ] %s\n", ZOS_API_BANNER);
    dwm_run();
    return 0;
}
#endif
