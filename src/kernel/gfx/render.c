/*
 * Zhaoxu OS - system software render API.
 *
 * This is the only drawing interface used by ADE, DWM and desktop
 * applications: rectangles, Aqua-style rounded shapes, gradients, shadows,
 * TrueType text and the hardware cursor.  The window manager never touches
 * framebuffer pixels itself.
 */
#include <render.h>
#include <string.h>
#include <ttf.h>
#include <types.h>

#ifndef ZOS_USER
#include <boot.h>
#include <mem.h>
#endif

#define RENDER_PAGE_SIZE 4096
#define ROUND_MAX_RADIUS 20
#define CURSOR_W 19
#define CURSOR_H 20
#define CURSOR_SHAPE_W 12

extern char font8x8_basic[128][8];

static const char *const g_cursor_shape[CURSOR_H] = {
    "X           ",
    "XX          ",
    "XoX         ",
    "XooX        ",
    "XoooX       ",
    "XooooX      ",
    "XoooooX     ",
    "XooooooX    ",
    "XoooooooX   ",
    "XooooooooX  ",
    "XoooooXXXXX ",
    "XooXooX     ",
    "XoX XooX    ",
    "XX  XooX    ",
    "X    XooX   ",
    "     XooX   ",
    "      XooX  ",
    "      XooX  ",
    "       XX   ",
    "            ",
};

static struct render_surface g_screen;
static struct render_surface *g_target;
static struct render_rect g_clip;
static u8 g_insets[ROUND_MAX_RADIUS + 1][ROUND_MAX_RADIUS + 1];
static int g_insets_ready;

static u32 g_cursor_x = 0xFFFF;
static u32 g_cursor_y = 0xFFFF;
static int g_cursor_saved;
static u32 g_cursor_save[CURSOR_W * CURSOR_H];

struct render_rect render_rect_make(int x, int y, int w, int h)
{
    struct render_rect r = { x, y, x + w, y + h };
    return r;
}

struct render_rect render_rect_intersect(struct render_rect a,
                                         struct render_rect b)
{
    struct render_rect r;
    r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    if (r.x0 > r.x1)
        r.x0 = r.x1;
    if (r.y0 > r.y1)
        r.y0 = r.y1;
    return r;
}

int render_rect_empty(struct render_rect r)
{
    return r.x0 >= r.x1 || r.y0 >= r.y1;
}

int render_rect_intersects(struct render_rect a, struct render_rect b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

u32 render_pack(u32 rgb)
{
    if (g_target && g_target->format == 0)
        return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    return rgb & 0xFFFFFF;
}

static u32 unpack(u32 pixel)
{
    if (g_target && g_target->format == 0)
        return ((pixel & 0xFF) << 16) | (pixel & 0xFF00) |
               ((pixel >> 16) & 0xFF);
    return pixel & 0xFFFFFF;
}

u32 render_lerp(u32 a, u32 b, int step, int total)
{
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * step / total;
    int g = ag + (bg - ag) * step / total;
    int bl = ab + (bb - ab) * step / total;
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

static u32 blend_pixel(u32 dst, u32 rgb, u32 alpha)
{
    u32 d = unpack(dst);
    u32 dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    u32 sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    u32 r = (sr * alpha + dr * (255 - alpha)) / 255;
    u32 g = (sg * alpha + dg * (255 - alpha)) / 255;
    u32 b = (sb * alpha + db * (255 - alpha)) / 255;
    return render_pack((r << 16) | (g << 8) | b);
}

static u64 (*g_page_alloc)(u32 pages);

void render_set_page_allocator(u64 (*alloc)(u32 pages))
{
    g_page_alloc = alloc;
}

void render_init_surface(u32 *pixels, int width, int height, int stride,
                         u32 format)
{
    if (!pixels || width <= 0 || height <= 0 || stride <= 0)
        return;
    g_screen.pixels = pixels;
    g_screen.width = width;
    g_screen.height = height;
    g_screen.stride = stride;
    g_screen.format = format;
    g_screen.screen = 1;
    g_target = &g_screen;
    g_clip = render_rect_make(0, 0, width, height);
}

#ifndef ZOS_USER
void render_init(const struct boot_info *bi)
{
    if (!bi || !bi->framebuffer_base)
        return;
    render_set_page_allocator(vmm_alloc_pages);
    render_init_surface((u32 *)(uintptr_t)bi->framebuffer_base,
                        (int)bi->width, (int)bi->height,
                        (int)(bi->pitch / 4), bi->pixel_format);
}
#endif

struct render_surface *render_screen(void)
{
    return g_screen.pixels ? &g_screen : NULL;
}

struct render_surface *render_surface_create(int width, int height)
{
    if (width <= 0 || height <= 0)
        return NULL;

    if (!g_page_alloc)
        return NULL;

    u64 pixels = (u64)width * (u64)height;
    u32 bytes = (u32)(sizeof(struct render_surface) + pixels * 4);
    u32 pages = (bytes + RENDER_PAGE_SIZE - 1) / RENDER_PAGE_SIZE;
    u64 address = g_page_alloc(pages);
    if (!address)
        return NULL;

    struct render_surface *surface = (struct render_surface *)(uintptr_t)
        address;
    surface->pixels = (u32 *)(uintptr_t)(address + sizeof(*surface));
    surface->width = width;
    surface->height = height;
    surface->stride = width;
    surface->format = g_screen.format;
    surface->screen = 0;
    for (int i = 0; i < width * height; i++)
        surface->pixels[i] = 0;
    return surface;
}

void render_set_target(struct render_surface *surface)
{
    if (!surface)
        return;
    g_target = surface;
    g_clip = render_rect_make(0, 0, surface->width, surface->height);
}

struct render_surface *render_target(void)
{
    return g_target;
}

void render_set_clip(struct render_rect rect)
{
    g_clip = rect;
    if (!g_target)
        return;
    struct render_rect full = render_rect_make(0, 0, g_target->width,
                                               g_target->height);
    g_clip = render_rect_intersect(g_clip, full);
}

struct render_rect render_clip(void)
{
    return g_clip;
}

void render_fill_rect(int x, int y, int w, int h, u32 rgb)
{
    if (!g_target || w <= 0 || h <= 0)
        return;

    struct render_rect r = render_rect_intersect(
        render_rect_make(x, y, w, h), g_clip);
    if (render_rect_empty(r))
        return;

    u32 pixel = render_pack(rgb);
    for (int row = r.y0; row < r.y1; row++) {
        u32 *line = g_target->pixels + row * g_target->stride + r.x0;
        for (int col = r.x0; col < r.x1; col++)
            *line++ = pixel;
    }
}

void render_blend_rect(int x, int y, int w, int h, u32 rgb, u32 alpha)
{
    if (!g_target || alpha == 0 || w <= 0 || h <= 0)
        return;
    if (alpha >= 255) {
        render_fill_rect(x, y, w, h, rgb);
        return;
    }

    struct render_rect r = render_rect_intersect(
        render_rect_make(x, y, w, h), g_clip);
    if (render_rect_empty(r))
        return;

    for (int row = r.y0; row < r.y1; row++) {
        u32 *line = g_target->pixels + row * g_target->stride + r.x0;
        for (int col = r.x0; col < r.x1; col++) {
            *line = blend_pixel(*line, rgb, alpha);
            line++;
        }
    }
}

static void build_round_insets(void)
{
    if (g_insets_ready)
        return;
    for (int radius = 0; radius <= ROUND_MAX_RADIUS; radius++) {
        for (int row = 0; row < radius; row++) {
            int dy = 2 * radius - 2 * row - 1;
            int limit = 4 * radius * radius - dy * dy;
            int inset = radius;
            for (int col = 0; col < radius; col++) {
                int dx = 2 * radius - 2 * col - 1;
                if (dx * dx <= limit) {
                    inset = col;
                    break;
                }
            }
            g_insets[radius][row] = (u8)inset;
        }
    }
    g_insets_ready = 1;
}

void render_rounded_rect(int x, int y, int w, int h, int radius, u32 rgb,
                         u32 alpha)
{
    if (!g_target || w <= 0 || h <= 0)
        return;
    build_round_insets();

    if (radius > ROUND_MAX_RADIUS)
        radius = ROUND_MAX_RADIUS;
    if (radius * 2 > w || radius * 2 > h)
        radius = (w < h ? w : h) / 2;
    if (radius <= 0) {
        render_blend_rect(x, y, w, h, rgb, alpha);
        return;
    }

    for (int row = 0; row < h; row++) {
        int inset = 0;
        if (row < radius)
            inset = g_insets[radius][row];
        else if (row >= h - radius)
            inset = g_insets[radius][h - 1 - row];
        if (w - inset * 2 <= 0)
            continue;
        render_blend_rect(x + inset, y + row, w - inset * 2, 1, rgb, alpha);
        if (inset > 0) {
            int partial = (int)(alpha * 110 / 255);
            if (partial > 0) {
                render_blend_rect(x + inset - 1, y + row, 1, 1, rgb,
                                  (u32)partial);
                render_blend_rect(x + w - inset, y + row, 1, 1, rgb,
                                  (u32)partial);
            }
        }
    }
}

void render_vgrad(int x, int y, int w, int h, u32 top, u32 bottom)
{
    for (int i = 0; i < h; i++)
        render_fill_rect(x, y + i, w, 1,
                         render_lerp(top, bottom, i, h > 1 ? h - 1 : 1));
}

void render_hline(int x, int y, int w, u32 rgb)
{
    render_fill_rect(x, y, w, 1, rgb);
}

void render_border(int x, int y, int w, int h, u32 rgb)
{
    render_fill_rect(x, y, w, 1, rgb);
    render_fill_rect(x, y + h - 1, w, 1, rgb);
    render_fill_rect(x, y, 1, h, rgb);
    render_fill_rect(x + w - 1, y, 1, h, rgb);
}

void render_soft_shadow(int x, int y, int w, int h, int spread)
{
    for (int i = 0; i < spread; i++) {
        u32 alpha = (u32)(28 - i * 3 > 4 ? 28 - i * 3 : 4);
        int sx = x - i - 2;
        int sy = y - i + 2;
        int sw = w + (i + 2) * 2;
        int sh = h + (i + 2) * 2;
        render_blend_rect(sx, sy, sw, 1, 0x0A1622, alpha);
        render_blend_rect(sx, sy + sh - 1, sw, 1, 0x0A1622, alpha);
        render_blend_rect(sx, sy, 1, sh, 0x0A1622, alpha);
        render_blend_rect(sx + sw - 1, sy, 1, sh, 0x0A1622, alpha);
    }
}

void render_soft_circle(int cx, int cy, int radius, u32 rgb, u32 max_alpha)
{
    if (!g_target || radius <= 0)
        return;
    int x0 = cx - radius < 0 ? 0 : cx - radius;
    int y0 = cy - radius < 0 ? 0 : cy - radius;
    int x1 = cx + radius;
    int y1 = cy + radius;
    if (x1 > g_target->width)
        x1 = g_target->width;
    if (y1 > g_target->height)
        y1 = g_target->height;

    struct render_rect r = render_rect_intersect(
        render_rect_make(x0, y0, x1 - x0, y1 - y0), g_clip);
    if (render_rect_empty(r))
        return;

    i64 r2 = (i64)radius * radius;
    for (int y = r.y0; y < r.y1; y++) {
        i64 dy = y - cy;
        for (int x = r.x0; x < r.x1; x++) {
            i64 dx = x - cx;
            i64 d2 = dx * dx + dy * dy;
            if (d2 >= r2)
                continue;
            u32 alpha = (u32)((r2 - d2) * max_alpha / r2);
            u32 *p = &g_target->pixels[y * g_target->stride + x];
            *p = blend_pixel(*p, rgb, alpha);
        }
    }
}

void render_span(int x0, int x1, int y, u32 rgb)
{
    if (x1 <= x0)
        return;
    render_fill_rect(x0, y, x1 - x0, 1, rgb);
}

void render_blit(struct render_surface *source, struct render_rect rect)
{
    if (!g_target || !source || !source->pixels)
        return;

    struct render_rect r = render_rect_intersect(rect, g_clip);
    struct render_rect src_limit = render_rect_make(0, 0, source->width,
                                                    source->height);
    r = render_rect_intersect(r, src_limit);
    if (render_rect_empty(r))
        return;

    for (int y = r.y0; y < r.y1; y++) {
        u32 *dst = g_target->pixels + y * g_target->stride + r.x0;
        u32 *src = source->pixels + y * source->stride + r.x0;
        for (int x = r.x0; x < r.x1; x++)
            *dst++ = *src++;
    }
}

/* ------------------------------------------------------------------ */
/* text                                                                */
/* ------------------------------------------------------------------ */

int render_line_height(void)
{
    return ttf_available() ? ttf_line_height() : 16;
}

static int draw_mask(int x, int y, const u8 *mask, int stride, int w, int h,
                     u32 fg, int scale)
{
    if (!g_target)
        return 0;
    for (int row = 0; row < h; row++) {
        int py = y + row * scale;
        for (int col = 0; col < w; col++) {
            u32 alpha = mask[row * stride + col];
            if (alpha == 0)
                continue;
            int px = x + col * scale;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++)
                    render_blend_rect(px + dx, py + dy, 1, 1, fg, alpha);
            }
        }
    }
    return w * scale;
}

static int draw_bitmap_char(u32 codepoint, int x, int y, u32 fg)
{
    if (codepoint < 32 || codepoint >= 128)
        codepoint = '?';
    const u8 *rows = (const u8 *)font8x8_basic[codepoint];
    for (int row = 0; row < 8; row++) {
        u8 bits = rows[row];
        for (int col = 0; col < 8; col++) {
            if (!(bits & (1 << col)))
                continue;
            render_fill_rect(x + col, y + row * 2, 1, 2, fg);
        }
    }
    return 8;
}

static int glyph_advance(u32 codepoint)
{
    if (ttf_available()) {
        struct ttf_glyph glyph;
        if (ttf_render(codepoint, &glyph) == 0 && glyph.adv)
            return glyph.adv;
    }
    return 8;
}

static int draw_codepoint(u32 codepoint, int x, int y, u32 fg, int scale)
{
    if (!ttf_available())
        return draw_bitmap_char(codepoint, x, y, fg);

    struct ttf_glyph glyph;
    if (ttf_render(codepoint, &glyph) != 0 ||
        (glyph.w == 0 && glyph.adv == 0)) {
        if (codepoint == ' ')
            return 5 * scale;
        if (codepoint < 128)
            return draw_bitmap_char(codepoint, x, y, fg);
        return draw_bitmap_char('?', x, y, fg);
    }

    int advance = glyph.adv ? glyph.adv : (glyph.w ? glyph.w : 8);
    int shift = advance > glyph.w ? (advance - glyph.w) / 2 : 0;
    if (glyph.w && glyph.h)
        draw_mask(x + (shift + glyph.bx) * scale, y + glyph.by * scale,
                  glyph.alpha, glyph.w, glyph.w, glyph.h, fg, scale);
    return advance * scale;
}

static u32 utf8_next(const u8 **cursor)
{
    u32 codepoint = *(*cursor)++;
    if (codepoint < 0x80)
        return codepoint;

    int extra = 0;
    if ((codepoint & 0xE0) == 0xC0) {
        codepoint &= 0x1F;
        extra = 1;
    } else if ((codepoint & 0xF0) == 0xE0) {
        codepoint &= 0x0F;
        extra = 2;
    } else if ((codepoint & 0xF8) == 0xF0) {
        codepoint &= 0x07;
        extra = 3;
    } else {
        return '?';
    }
    for (int i = 0; i < extra && (**cursor & 0xC0) == 0x80; i++)
        codepoint = (codepoint << 6) | (*(*cursor)++ & 0x3F);
    return codepoint;
}

int render_text_width(const char *text, int scale)
{
    if (!text)
        return 0;
    int width = 0;
    const u8 *cursor = (const u8 *)text;
    while (*cursor)
        width += glyph_advance(utf8_next(&cursor)) * scale;
    return width;
}

int render_text(const char *text, int x, int y, u32 fg, int scale)
{
    if (!text)
        return 0;
    int start = x;
    const u8 *cursor = (const u8 *)text;
    while (*cursor)
        x += draw_codepoint(utf8_next(&cursor), x, y, fg, scale);
    return x - start;
}

int render_text_center(const char *text, int cx, int y, u32 fg, int scale)
{
    int width = render_text_width(text, scale);
    return render_text(text, cx - width / 2, y, fg, scale);
}

/* ------------------------------------------------------------------ */
/* pointer                                                             */
/* ------------------------------------------------------------------ */

void render_cursor_restore(void)
{
    if (!g_cursor_saved || !g_target)
        return;

    int index = 0;
    for (int row = 0; row < CURSOR_H; row++) {
        int py = (int)g_cursor_y + row;
        if (py < 0 || py >= g_target->height)
            continue;
        for (int col = 0; col < CURSOR_W; col++) {
            int px = (int)g_cursor_x + col;
            if (px < 0 || px >= g_target->width)
                continue;
            g_target->pixels[py * g_target->stride + px] =
                g_cursor_save[index++];
        }
    }
    g_cursor_saved = 0;
}

void render_cursor(int x, int y)
{
    if (!g_target)
        return;
    if (g_cursor_saved && x == (int)g_cursor_x && y == (int)g_cursor_y)
        return;
    render_cursor_restore();

    g_cursor_x = (u32)x;
    g_cursor_y = (u32)y;

    int index = 0;
    for (int row = 0; row < CURSOR_H; row++) {
        int py = y + row;
        for (int col = 0; col < CURSOR_W; col++) {
            int px = x + col;
            if (px < 0 || py < 0 || px >= g_target->width ||
                py >= g_target->height)
                continue;
            u32 *p = &g_target->pixels[py * g_target->stride + px];
            g_cursor_save[index++] = *p;
            char shape = col < CURSOR_SHAPE_W ? g_cursor_shape[row][col] : ' ';
            if (shape == 'X')
                *p = render_pack(0x061624);
            else if (shape == 'o')
                *p = render_pack(0xFFFFFF);
        }
    }
    g_cursor_saved = 1;
}
