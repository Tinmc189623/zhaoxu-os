/*
 * Zhaoxu OS - Desktop Window Compositor.
 *
 * The Desktop Window Manager paints layers into the system-RAM back buffer;
 * this component presents only settled damage rectangles to the framebuffer
 * and keeps the cursor out of the composed buffer so a moving pointer never
 * forces a full window repaint.  Cursor pixels come from a PNG + text theme
 * (cursor.theme) and are blended by the compositor; without a theme the
 * built-in arrow from the render API is used.
 */
extern "C" {
#include <kprintf.h>
#include <types.h>
#include <userlib.h>
}

#include "compositor.h"
extern "C" {
#include "cursor.h"
}

namespace {

struct render_surface *g_screen;
struct render_surface *g_back;
int g_cursor_presented;
int g_cursor_x;
int g_cursor_y;
int g_cursor_theme_ready;
const struct cursor_def *g_cursor;
int g_cursor_in_buffer;
int g_cursor_saved;
int g_save_x;
int g_save_y;
int g_save_w;
int g_save_h;
int g_last_frame;
struct render_rect g_present_rect;
u32 g_save_pixels[CURSOR_MAX_SIZE * CURSOR_MAX_SIZE];

/* Convert a native framebuffer pixel into logical 0xRRGGBB form. */
u32 logical_color(const struct render_surface *surface, u32 raw)
{
    if (!surface)
        return raw & 0xFFFFFF;
    if (surface->format == 0)
        return ((raw & 0xFF) << 16) | (raw & 0xFF00) |
               ((raw >> 16) & 0xFF);
    return raw & 0xFFFFFF;
}

int current_frame(void)
{
    if (!g_cursor)
        return 0;
    return cursor_frame_index(g_cursor, (u64)sys_uptime());
}

struct render_rect cursor_rect(int x, int y)
{
    if (g_cursor_theme_ready && g_cursor) {
        int index = current_frame();
        const struct cursor_frame *frame = &g_cursor->frames[index];
        return render_rect_make(x - g_cursor->hotspot_x,
                                y - g_cursor->hotspot_y,
                                frame->width, frame->height);
    }
    return render_cursor_bounds(x, y);
}

/* Save the pixels the cursor is about to cover, in logical form, so the
 * compositor can restore them byte-exactly on either framebuffer format. */
void save_background(int x, int y, int width, int height)
{
    struct render_surface *target = render_target();
    g_save_x = x;
    g_save_y = y;
    g_save_w = width;
    g_save_h = height;
    for (int row = 0; row < height; row++) {
        int py = y + row;
        for (int column = 0; column < width; column++) {
            int px = x + column;
            u32 saved = 0;
            if (target && px >= 0 && py >= 0 && px < target->width &&
                py < target->height) {
                u32 raw = target->pixels[py * target->stride + px];
                saved = 0xFF000000u | logical_color(target, raw);
            }
            g_save_pixels[row * width + column] = saved;
        }
    }
    g_cursor_saved = 1;
}

} // namespace

int dwc_init(struct render_surface *screen)
{
    g_screen = screen;
    g_back = screen ? render_surface_create(screen->width, screen->height)
                    : nullptr;
    if (!g_back)
        g_back = screen;
    render_set_target(g_back);
    g_cursor_presented = 0;
    g_cursor_x = 0;
    g_cursor_y = 0;
    g_cursor_theme_ready = 0;
    g_cursor = nullptr;
    g_cursor_in_buffer = 0;
    g_cursor_saved = 0;
    g_last_frame = -1;

    static const char *const directories[] = {
        "/usr/share/cursors/Aqua",
        "/mnt/usr/share/cursors/Aqua",
        "/usr/share/cursors/default",
    };
    for (unsigned i = 0;
         i < sizeof(directories) / sizeof(directories[0]); i++) {
        if (cursor_theme_load(directories[i]) == 0) {
            g_cursor_theme_ready = 1;
            g_cursor = cursor_find("arrow");
            break;
        }
    }
    if (g_cursor_theme_ready && g_cursor) {
        kprintf("[dwc] cursor theme %s, %d cursor(s), %dx%d arrow\n",
                cursor_theme_name(), cursor_theme_count(),
                g_cursor->frames[0].width, g_cursor->frames[0].height);
    } else {
        kprintf("[dwc] no PNG cursor theme; using the built-in arrow\n");
    }
    if (g_back && g_back != g_screen)
        kprintf("[dwc] Desktop Window Compositor back buffer %dx%d\n",
                g_screen->width, g_screen->height);
    else
        kprintf("[dwc] Desktop Window Compositor direct mode\n");
    return g_back ? 0 : -1;
}

struct render_surface *dwc_screen(void)
{
    return g_screen;
}

struct render_surface *dwc_back(void)
{
    return g_back;
}

/* Copy one finished rectangle from the system-RAM back buffer to the
 * framebuffer.  Presenting only settled pixels keeps windows from flashing
 * while they are being composed. */
void dwc_present(struct render_rect rect)
{
    if (!g_back || g_back == g_screen || render_rect_empty(rect))
        return;
    render_set_target(g_screen);
    render_set_clip(render_rect_make(0, 0, g_screen->width,
                                     g_screen->height));
    render_blit(g_back, rect);
    render_set_target(g_back);
}

void dwc_cursor_set(const char *name)
{
    if (!g_cursor_theme_ready) {
        g_cursor = nullptr;
        return;
    }
    if (g_cursor_presented)
        dwc_hide_cursor();
    else
        dwc_cursor_restore();
    g_cursor = cursor_find(name);
    g_last_frame = -1;
}

const char *dwc_cursor_theme(void)
{
    return g_cursor_theme_ready ? cursor_theme_name() : "builtin";
}

int dwc_cursor_animate(void)
{
    if (!g_cursor_theme_ready || !g_cursor || g_cursor->frame_count <= 1)
        return 0;
    return current_frame() != g_last_frame;
}

/* Put the pixels under the previous cursor back into the back buffer. */
void dwc_cursor_restore(void)
{
    if (g_cursor_theme_ready && g_cursor) {
        if (g_cursor_saved) {
            render_image_copy(g_save_pixels, g_save_w, g_save_h,
                              g_save_x, g_save_y);
            g_cursor_saved = 0;
            g_cursor_in_buffer = 0;
        }
        return;
    }
    if (g_cursor_in_buffer) {
        render_cursor_restore();
        g_cursor_in_buffer = 0;
    }
}

void dwc_cursor_draw(int x, int y)
{
    if (!g_cursor_theme_ready || !g_cursor) {
        render_cursor(x, y);
        g_cursor_in_buffer = 1;
        return;
    }
    if (g_cursor_saved)
        dwc_cursor_restore();
    int index = current_frame();
    const struct cursor_frame *frame = &g_cursor->frames[index];
    int left = x - g_cursor->hotspot_x;
    int top = y - g_cursor->hotspot_y;
    save_background(left, top, frame->width, frame->height);
    render_image_blend(frame->pixels, frame->width, frame->height,
                       left, top);
    g_last_frame = index;
    g_cursor_in_buffer = 1;
}

void dwc_present_cursor(int x, int y)
{
    struct render_rect current = cursor_rect(x, y);
    if (g_cursor_presented)
        dwc_present(g_present_rect);
    dwc_present(current);
    g_present_rect = current;
    g_cursor_presented = 1;
    g_cursor_x = x;
    g_cursor_y = y;
}

void dwc_hide_cursor(void)
{
    dwc_cursor_restore();
    if (g_cursor_presented) {
        dwc_present(g_present_rect);
        g_cursor_presented = 0;
    }
}

int dwc_cursor_presented(void)
{
    return g_cursor_presented;
}

int dwc_cursor_x(void)
{
    return g_cursor_x;
}

int dwc_cursor_y(void)
{
    return g_cursor_y;
}
