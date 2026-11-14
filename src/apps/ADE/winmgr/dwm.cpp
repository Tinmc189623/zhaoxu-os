/*
 * Zhaoxu OS - DWM window manager.
 *
 * DWM owns window chrome, z-order, dragging, focus and composition.  It does
 * not paint application pages: every pixel goes through the system render
 * API, and application content is produced by the ade_app paint callback
 * registered outside ADE.  Desktop chrome (wallpaper, menu bar, dock) is
 * delegated back to ADE through dwm_ops.
 */
extern "C" {
#include <console.h>
#include <drivers.h>
#include <kprintf.h>
#include <string.h>
#include <timer.h>
#include <types.h>
}

#include "../ade.h"
#include "../ade_internal.h"
#include "../compositor/compositor.h"
#include "dwm.h"

namespace {

constexpr int kMaxDamage = 16;

struct damage_list {
    struct render_rect items[kMaxDamage];
    int count;
};

struct render_surface *g_screen;
struct render_surface *g_back;
struct dwm_ops g_ops_storage;
const struct dwm_ops *g_ops;
struct dwm_window g_windows[DWM_MAX_WINDOWS];
int g_focus = -1;
int g_next_z = 1;
int g_title_h;
damage_list g_damage;

int g_drag_index = -1;
int g_prev_buttons;
int g_last_tick;
int g_last_app_tick;
char g_cursor_name[32];

struct render_rect window_damage_rect(const struct dwm_window *window){
    int pad = dwm_ui(14);
    return render_rect_make(window->x - pad, window->y - pad,
                            window->w + pad * 2, window->h + pad * 2);
}

void damage_reset(void)
{
    g_damage.count = 0;
}

void damage_add_raw(struct render_rect r)
{
    if (!g_screen)
        return;
    r = render_rect_intersect(
        r, render_rect_make(0, 0, g_screen->width, g_screen->height));
    if (render_rect_empty(r))
        return;

    for (int i = 0; i < g_damage.count; i++) {
        struct render_rect *item = &g_damage.items[i];
        if (r.x0 <= item->x1 + 2 && item->x0 <= r.x1 + 2 &&
            r.y0 <= item->y1 + 2 && item->y0 <= r.y1 + 2) {
            if (r.x0 < item->x0) item->x0 = r.x0;
            if (r.y0 < item->y0) item->y0 = r.y0;
            if (r.x1 > item->x1) item->x1 = r.x1;
            if (r.y1 > item->y1) item->y1 = r.y1;
            return;
        }
    }

    if (g_damage.count < kMaxDamage) {
        g_damage.items[g_damage.count++] = r;
        return;
    }

    struct render_rect *box = &g_damage.items[0];
    if (r.x0 < box->x0) box->x0 = r.x0;
    if (r.y0 < box->y0) box->y0 = r.y0;
    if (r.x1 > box->x1) box->x1 = r.x1;
    if (r.y1 > box->y1) box->y1 = r.y1;
    g_damage.count = 1;
}

void damage_rect(int x, int y, int w, int h)
{
    damage_add_raw(render_rect_make(x, y, w, h));
}

void menu_rect(struct render_rect *r)
{
    *r = render_rect_make(0, 0, g_screen ? g_screen->width : 0,
                          g_ops ? g_ops->menu_h : 0);
}

void dock_rect(struct render_rect *r)
{
    int h = g_ops ? g_ops->dock_h : 0;
    *r = render_rect_make(0, (g_screen ? g_screen->height : 0) - h,
                          g_screen ? g_screen->width : 0, h);
}

void traffic_lights(const struct dwm_window *window, int focused)
{
    int radius = dwm_ui(7);
    int cy = window->y + g_title_h / 2;
    int x = window->x + dwm_ui(20);
    static const u32 colors[3] = { 0xE9685C, 0xF0BE4E, 0x62C34E };
    static const u32 borders[3] = { 0xA53A30, 0xA87B24, 0x3D8A2E };

    for (int i = 0; i < 3; i++) {
        int cx = x + i * dwm_ui(20);
        render_rounded_rect(cx - radius, cy - radius, radius * 2, radius * 2,
                            radius, colors[i], focused ? 255 : 180);
        render_border(cx - radius, cy - radius, radius * 2, radius * 2,
                      borders[i]);
    }
}

void paint_window(const struct dwm_window *window, struct render_rect damage)
{
    if (!window->open || !window->app)
        return;

    int focused = g_focus >= 0 && &g_windows[g_focus] == window;
    render_set_clip(damage);
    render_soft_shadow(window->x, window->y, window->w, window->h,
                       dwm_ui(9));
    render_rounded_rect(window->x, window->y, window->w, window->h,
                        dwm_ui(12), focused ? 0x4A6072 : 0x71808D, 255);
    render_rounded_rect(window->x + 1, window->y + 1, window->w - 2,
                        window->h - 2, dwm_ui(11), 0xF4F7FA, 255);

    int radius = dwm_ui(10);
    render_set_clip(render_rect_intersect(
        damage, render_rect_make(window->x + 1, window->y + 1,
                                 window->w - 2, g_title_h - 2)));
    /* Keep the gradient inside the horizontal radius so the rounded top
     * corners show the frame colour instead of a square gradient corner. */
    render_vgrad(window->x + radius, window->y + 1,
                 window->w - radius * 2, g_title_h - 2,
                 focused ? 0xFDFEFF : 0xF4F7F9,
                 focused ? 0xC9DBEC : 0xD8E0E7);
    render_blend_rect(window->x + radius, window->y + 2,
                      window->w - radius * 2, g_title_h / 2,
                      0xFFFFFF, focused ? 70 : 40);
    render_hline(window->x + 1, window->y + g_title_h - 1, window->w - 2,
                 focused ? 0x8EA2B6 : 0xB6C1CB);

    traffic_lights(window, focused);
    render_text(window->app->name, window->x + dwm_ui(80),
                window->y + (g_title_h - render_line_height()) / 2 + 1,
                focused ? 0x1B2C3D : 0x5A6875, 1);

    struct render_rect content = dwm_content_rect(window);
    render_set_clip(render_rect_intersect(damage, content));
    struct ade_view view = { content.x0, content.y0, content.x1 - content.x0,
                             content.y1 - content.y0, focused };
    if (window->app->paint)
        window->app->paint(&view);
    render_set_clip(damage);
}

void paint_rect(struct render_rect damage)
{
    if (render_rect_empty(damage) || !g_screen)
        return;

    render_set_target(g_back);
    render_set_clip(damage);

    if (g_ops && g_ops->wallpaper)
        render_blit(g_ops->wallpaper, damage);

    if (g_ops) {
        struct render_rect menu;
        struct render_rect dock;
        menu_rect(&menu);
        dock_rect(&dock);
        if (render_rect_intersects(damage, menu) && g_ops->paint_menubar) {
            render_set_clip(render_rect_intersect(damage, menu));
            g_ops->paint_menubar();
        }
        if (render_rect_intersects(damage, dock) && g_ops->paint_dock) {
            render_set_clip(render_rect_intersect(damage, dock));
            g_ops->paint_dock();
        }
    }

    for (int z = 1; z <= g_next_z; z++) {
        for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
            const struct dwm_window *window = &g_windows[i];
            if (!window->open || window->z != z)
                continue;
            if (!render_rect_intersects(damage, window_damage_rect(window)))
                continue;
            paint_window(window, damage);
        }
    }
}

void paint(void)
{
    if (!g_screen || !g_back || g_damage.count == 0)
        return;

    render_set_target(g_back);
    dwc_cursor_restore();

    struct render_rect items[kMaxDamage];
    int count = g_damage.count;
    for (int i = 0; i < count; i++)
        items[i] = g_damage.items[i];
    for (int i = 0; i < count; i++)
        paint_rect(items[i]);
    damage_reset();

    int mx = mouse_x();
    int my = mouse_y();
    int ready = mouse_ready();
    if (ready)
        dwc_cursor_draw(mx, my);

    for (int i = 0; i < count; i++)
        dwc_present(items[i]);
    if (ready)
        dwc_present_cursor(mx, my);
    else
        dwc_hide_cursor();
}

int window_at(int x, int y)
{
    int best = -1;
    int best_z = 0;
    for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
        const struct dwm_window *window = &g_windows[i];
        if (!window->open)
            continue;
        if (x < window->x || y < window->y ||
            x >= window->x + window->w || y >= window->y + window->h)
            continue;
        if (window->z > best_z) {
            best = i;
            best_z = window->z;
        }
    }
    return best;
}

void bring_to_front(int index)
{
    if (index < 0 || index >= DWM_MAX_WINDOWS)
        return;

    int top_z = 0;
    for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
        if (i != index && g_windows[i].open && g_windows[i].z > top_z)
            top_z = g_windows[i].z;
    }

    int already_top = g_windows[index].z > top_z;
    int already_focused = g_focus == index;
    if (already_top && already_focused)
        return;

    int old_focus = g_focus;
    g_windows[index].z = ++g_next_z;
    if (g_next_z > 900) {
        for (int i = 0; i < DWM_MAX_WINDOWS; i++)
            g_windows[i].z /= 2;
        g_next_z /= 2;
    }
    g_focus = index;

    damage_add_raw(window_damage_rect(&g_windows[index]));
    if (old_focus >= 0 && old_focus != index && g_windows[old_focus].open)
        damage_add_raw(window_damage_rect(&g_windows[old_focus]));
}

bool close_button_at(const struct dwm_window *window, int x, int y)
{
    int radius = dwm_ui(7) + dwm_ui(3);
    int cx = window->x + dwm_ui(20);
    int cy = window->y + g_title_h / 2;
    int dx = x - cx;
    int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

/* Cursor shape for a window under the pointer.  Chrome owns its own hit
 * zones; the app gets a chance to describe the content with cursor_at(). */
const char *window_cursor(int index, int x, int y)
{
    const struct dwm_window *window = &g_windows[index];
    if (close_button_at(window, x, y))
        return "hand";
    if (y < window->y + g_title_h)
        return "arrow";

    struct render_rect content = dwm_content_rect(window);
    if (x < content.x0 || x >= content.x1 || y < content.y0 ||
        y >= content.y1)
        return "arrow";
    if (window->app && window->app->cursor_at) {
        struct ade_view view = { content.x0, content.y0,
                                 content.x1 - content.x0,
                                 content.y1 - content.y0,
                                 g_focus >= 0 && &g_windows[g_focus] == window };
        const char *name = window->app->cursor_at(&view, x, y);
        if (name && name[0])
            return name;
    }
    return "arrow";
}

/* Desktop chrome comes first (the menu bar is never covered by windows),
 * then the topmost window, then the dock.  Windows paint above the dock, so
 * a window that overlaps it must win the hit test. */
const char *cursor_for(int x, int y)
{
    if (g_ops && y >= 0 && y < g_ops->menu_h)
        return ade_menubar_hit(x, y) ? "hand" : "arrow";

    int index = window_at(x, y);
    if (index >= 0)
        return window_cursor(index, x, y);

    if (ade_dock_hit(x, y) >= 0)
        return "hand";
    return "arrow";
}

/* Swap the compositor cursor only when the shape actually changes; the
 * idle path repaints the pointer afterwards. */
void update_cursor_shape(int x, int y)
{
    const char *name = cursor_for(x, y);
    if (strcmp(name, g_cursor_name) == 0)
        return;
    strncpy(g_cursor_name, name, sizeof(g_cursor_name) - 1);
    g_cursor_name[sizeof(g_cursor_name) - 1] = 0;
    dwc_cursor_set(g_cursor_name);
}

void close_window(int index)
{
    if (index < 0 || index >= DWM_MAX_WINDOWS)
        return;
    const struct ade_app *app = g_windows[index].app;
    struct render_rect closed = window_damage_rect(&g_windows[index]);
    g_windows[index].open = 0;
    g_windows[index].z = 0;
    g_windows[index].app = nullptr;
    if (g_focus == index)
        g_focus = -1;
    g_drag_index = -1;
    damage_add_raw(closed);
    if (app && app->close)
        app->close();
}

void handle_press(int mx, int my, int buttons)
{
    if (g_ops && my < g_ops->menu_h) {
        if (g_ops->menu_click)
            g_ops->menu_click(mx, my);
        return;
    }

    int index = window_at(mx, my);
    if (index >= 0) {
        struct dwm_window *window = &g_windows[index];
        if (close_button_at(window, mx, my)) {
            close_window(index);
            return;
        }

        bring_to_front(index);
        if (my < window->y + g_title_h) {
            g_drag_index = index;
            window->drag_dx = mx - window->x;
            window->drag_dy = my - window->y;
        } else if (window->app && window->app->click) {
            struct render_rect content = dwm_content_rect(window);
            if (mx >= content.x0 && mx < content.x1 &&
                my >= content.y0 && my < content.y1) {
                struct ade_view view = { content.x0, content.y0,
                                         content.x1 - content.x0,
                                         content.y1 - content.y0, 1 };
                window->app->click(&view, mx, my, buttons);
            }
        }
        return;
    }

    if (g_ops && g_ops->dock_click)
        g_ops->dock_click(mx, my);
}

void handle_key(int key)
{
    if (key < 0)
        return;
    if (key == 27) {
        close_window(g_focus);
        return;
    }
    if (g_focus >= 0 && g_windows[g_focus].open) {
        const struct ade_app *app = g_windows[g_focus].app;
        if (app && app->key)
            app->key(key);
    }
}

} // namespace

int dwm_ui(int value)
{
    if (!g_screen)
        return value;
    int scaled = value * g_screen->height / 900;
    if (value > 0 && scaled == 0)
        scaled = 1;
    return scaled;
}

int dwm_title_height(void)
{
    return g_title_h;
}

struct render_rect dwm_content_rect(const struct dwm_window *window)
{
    /* Inset the client surface so its square background cannot cover the
     * window's rounded corners (the old 2 px inset left visible "穿模" at
     * the bottom edges).  A small frame padding also reads as Aqua chrome. */
    int pad = dwm_ui(8);
    return render_rect_make(window->x + pad, window->y + g_title_h + dwm_ui(2),
                            window->w - pad * 2,
                            window->h - g_title_h - pad - dwm_ui(2));
}

void dwm_init(const struct dwm_ops *ops)
{
    /* ade_setup() passes a stack-local ops struct; copy it before returning
     * to the caller or the window manager would read a dangling pointer. */
    if (ops)
        g_ops_storage = *ops;
    g_ops = ops ? &g_ops_storage : nullptr;
    g_screen = render_screen();
    if (dwc_init(g_screen) != 0) {
        g_back = g_screen;
    } else {
        g_back = dwc_back();
    }
    render_set_target(g_back);
    g_title_h = dwm_ui(32);
    g_focus = -1;
    g_next_z = 1;
    g_drag_index = -1;
    g_prev_buttons = 0;
    g_last_tick = 0;
    g_last_app_tick = 0;
    strcpy(g_cursor_name, "arrow");

    for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
        g_windows[i].open = 0;
        g_windows[i].z = 0;
    }
    damage_reset();
}

struct dwm_window *dwm_open(const struct ade_app *app)
{
    if (!app || !g_screen)
        return nullptr;

    for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
        if (g_windows[i].open && g_windows[i].app == app) {
            bring_to_front(i);
            return &g_windows[i];
        }
    }

    int slot = -1;
    for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
        if (!g_windows[i].open) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return nullptr;

    struct dwm_window *window = &g_windows[slot];
    window->app = app;
    window->w = app->default_w > 0
                    ? (app->unscaled ? app->default_w : dwm_ui(app->default_w))
                    : dwm_ui(640);
    window->h = app->default_h > 0
                    ? (app->unscaled ? app->default_h : dwm_ui(app->default_h))
                    : dwm_ui(420);

    /* Prefer a clean left-to-right row so the initial welcome/calculator
     * pair does not bury each other; fall back to the old cascade when the
     * next column would not fit. */
    window->x = dwm_ui(150);
    window->y = dwm_ui(90);
    if (slot > 0) {
        int right = 0;
        for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
            const struct dwm_window *other = &g_windows[i];
            if (other == window || !other->open)
                continue;
            if (other->x + other->w > right)
                right = other->x + other->w;
        }
        int gap = dwm_ui(24);
        if (right > 0 &&
            right + gap + window->w <= g_screen->width - dwm_ui(20)) {
            window->x = right + gap;
        } else {
            int offset = slot * dwm_ui(36);
            window->x = dwm_ui(150) + offset;
            window->y = dwm_ui(90) + offset;
        }
    }
    if (window->x + window->w > g_screen->width - dwm_ui(20))
        window->x = g_screen->width - window->w - dwm_ui(20);
    if (window->y + window->h > g_screen->height - dwm_ui(90))
        window->y = g_screen->height - window->h - dwm_ui(90);
    if (window->x < dwm_ui(10))
        window->x = dwm_ui(10);
    if (window->y < g_ops->menu_h + dwm_ui(8))
        window->y = g_ops->menu_h + dwm_ui(8);
    window->drag_dx = 0;
    window->drag_dy = 0;
    window->open = 1;
    window->z = ++g_next_z;
    g_focus = slot;

    if (app->init)
        app->init();
    damage_add_raw(window_damage_rect(window));
    return window;
}

void dwm_invalidate_all(void)
{
    if (!g_screen)
        return;
    damage_add_raw(render_rect_make(0, 0, g_screen->width, g_screen->height));
}

void dwm_invalidate_rect(int x, int y, int w, int h)
{
    damage_rect(x, y, w, h);
}

void dwm_invalidate_focus(void)
{
    if (g_focus < 0 || !g_windows[g_focus].open)
        return;
    struct render_rect r = window_damage_rect(&g_windows[g_focus]);
    damage_add_raw(r);
}

int window_index_for(const struct ade_app *app)
{
    for (int i = 0; i < DWM_MAX_WINDOWS; i++) {
        if (g_windows[i].open && g_windows[i].app == app)
            return i;
    }
    return -1;
}

void dwm_invalidate_app(const struct ade_app *app)
{
    int index = window_index_for(app);
    if (index < 0)
        return;
    damage_add_raw(window_damage_rect(&g_windows[index]));
}

void dwm_close_app(const struct ade_app *app)
{
    int index = window_index_for(app);
    if (index >= 0)
        close_window(index);
}

void dwm_close_focus(void)
{
    if (g_focus >= 0 && g_focus < DWM_MAX_WINDOWS)
        close_window(g_focus);
}

void dwm_run(void)
{
    dwm_invalidate_all();
    paint();

    for (;;) {
        int key = console_read_char();
        if (key >= 0)
            handle_key(key);

        int mx = mouse_x();
        int my = mouse_y();
        int buttons = mouse_ready() ? mouse_buttons() : 0;

        if (mouse_ready())
            update_cursor_shape(mx, my);

        if ((buttons & 1) && !(g_prev_buttons & 1))
            handle_press(mx, my, buttons);

        if (g_drag_index >= 0 && (buttons & 1)) {
            struct dwm_window *window = &g_windows[g_drag_index];
            int nx = mx - window->drag_dx;
            int ny = my - window->drag_dy;
            if (nx < -window->w + dwm_ui(90))
                nx = -window->w + dwm_ui(90);
            if (nx > g_screen->width - dwm_ui(90))
                nx = g_screen->width - dwm_ui(90);
            if (ny < g_ops->menu_h)
                ny = g_ops->menu_h;
            if (ny > g_screen->height - g_ops->dock_h - dwm_ui(16))
                ny = g_screen->height - g_ops->dock_h - dwm_ui(16);
            if (nx != window->x || ny != window->y) {
                damage_add_raw(window_damage_rect(window));
                window->x = nx;
                window->y = ny;
                damage_add_raw(window_damage_rect(window));
            }
        }
        if (!(buttons & 1))
            g_drag_index = -1;
        g_prev_buttons = buttons;

        if (g_ops && g_ops->poll)
            g_ops->poll();

        if (g_ops && g_ops->dock_hover)
            g_ops->dock_hover(mx, my);

        int now = (int)timer_ticks();
        if (now - g_last_tick >= 100) {
            g_last_tick = now;
            if (g_ops && g_ops->tick)
                g_ops->tick();
        }
        if (now - g_last_app_tick >= 500) {
            g_last_app_tick = now;
            if (g_focus >= 0 && g_windows[g_focus].open) {
                const struct ade_app *app = g_windows[g_focus].app;
                if (app && app->tick)
                    app->tick();
            }
        }

        if (g_damage.count > 0) {
            paint();
        } else if (mouse_ready() &&
                   (dwc_cursor_animate() || !dwc_cursor_presented() ||
                    mx != dwc_cursor_x() || my != dwc_cursor_y())) {
            render_set_target(g_back);
            dwc_cursor_restore();
            dwc_cursor_draw(mx, my);
            dwc_present_cursor(mx, my);
        }

        timer_sleep_ms(1);
    }
}
