/*
 * Zhaoxu OS - Desktop-Shell and DS-WM
 *
 * Aqua-flavoured desktop session linked into the kernel image.  The scene is
 * composed into a RAM back buffer and only damaged rectangles are copied to
 * the GOP framebuffer, so dragging windows and moving the pointer stay smooth
 * even at 1920x1080.  Windows have soft shadows, rounded corners, translucent
 * title bars and an auto-magnifying dock.  Applications: Welcome, Files,
 * Terminal (runs Command-Shell commands), Calculator, Notes, Monitor and the
 * graphical installer.
 */
extern "C" {
#include <acpi.h>
#include <ahci.h>
#include <boot.h>
#include <console.h>
#include <cpu.h>
#include <drivers.h>
#include <install.h>
#include <keyboard.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <rtc.h>
#include <string.h>
#include <timer.h>
#include <ttf.h>
#include <types.h>
#include <version.h>
#include <vfs.h>
}

extern "C" char font8x8_basic[128][8];
extern "C" int shell_exec_line_captured(char *line);

namespace {

constexpr int kMaxWindows = 7;
constexpr int kMaxEntries = 16;
constexpr int kMaxDamage = 8;
constexpr int kTermLines = 120;
constexpr int kTermCols = 96;
constexpr int kHistory = 12;
constexpr int kNotesSize = 4096;
constexpr int kMaxRadius = 20;

enum window_kind {
    KIND_WELCOME = 0,
    KIND_FILES = 1,
    KIND_TERMINAL = 2,
    KIND_CALC = 3,
    KIND_NOTES = 4,
    KIND_MONITOR = 5,
    KIND_INSTALLER = 6,
};

struct window {
    int x, y, w, h;
    const char *title;
    int kind;
    bool open;
    int z;
};

struct rect {
    int x0, y0, x1, y1;
};

struct file_entry {
    char name[48];
    u64 size;
    int is_dir;
};

struct damage_list {
    rect items[kMaxDamage];
    int count;
};

window g_windows[kMaxWindows];
int g_next_z = 2;
int g_focus = -1;

u32 *g_fb;
u32 *g_back;              /* scene buffer without the pointer */
u32 *g_bg;                /* cached wallpaper */
u32 g_pitch_px;
u32 g_width;
u32 g_height;
u32 g_format;
u32 *g_target;
int g_stride;
bool g_back_valid;

rect g_clip;
damage_list g_damage;

int g_menu_h;
int g_title_h;
int g_dock_h;

/* cursor save-under */
int g_cursor_x = -1;
int g_cursor_y = -1;
int g_cursor_drawn = 0;
u32 g_cursor_save[24 * 24];
constexpr int kCursorW = 19;
constexpr int kCursorH = 20;

/* dock hover */
int g_hover_icon = -1;

/* install state */
const window *g_install_win;
int g_install_phase;
u32 g_install_pct;
char g_install_message[128];

enum install_phase {
    INSTALL_IDLE = 0,
    INSTALL_RUNNING = 1,
    INSTALL_DONE = 2,
    INSTALL_FAILED = 3,
};

file_entry g_files[kMaxEntries];
int g_file_count;
int g_files_selected;

/* terminal */
char g_term[kTermLines][kTermCols + 1];
int g_term_count;
int g_term_row;              /* line being written */
int g_term_col;
char g_term_input[kTermCols + 1];
int g_term_input_len;
char g_term_history[kHistory][kTermCols + 1];
int g_term_history_count;
int g_term_history_pos;

/* notes */
char g_notes[kNotesSize];
int g_notes_len;
char g_notes_name[48];

/* calculator */
i64 g_calc_value;
i64 g_calc_pending;
char g_calc_op;
bool g_calc_fresh;
char g_calc_display[32];

/* clock cache */
u32 g_clock_last;
u32 g_clock_text_width;

/* frame counter for the caret blink */
u32 g_blink_last;
bool g_blink_on = true;

void repaint();
void cursor_restore();
void draw_cursor();

void collect_file(const char *name, u64 size, int is_dir, void *)
{
    if (g_file_count >= kMaxEntries)
        return;
    file_entry &entry = g_files[g_file_count++];
    strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.name[sizeof(entry.name) - 1] = 0;
    entry.size = size;
    entry.is_dir = is_dir;
}
/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

int ui(int value)
{
    int scaled = value * (int)g_height / 900;
    if (value > 0 && scaled == 0)
        scaled = 1;
    return scaled;
}

u32 pack(u32 rgb)
{
    if (g_format == 0)
        return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    return rgb & 0xFFFFFF;
}

u32 unpack(u32 pixel)
{
    if (g_format == 0)
        return ((pixel & 0xFF) << 16) | (pixel & 0xFF00) |
               ((pixel >> 16) & 0xFF);
    return pixel & 0xFFFFFF;
}

u32 blend(u32 dst, u32 rgb, u32 alpha)
{
    u32 d = unpack(dst);
    u32 dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
    u32 sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    u32 r = (sr * alpha + dr * (255 - alpha)) / 255;
    u32 g = (sg * alpha + dg * (255 - alpha)) / 255;
    u32 b = (sb * alpha + db * (255 - alpha)) / 255;
    return pack((r << 16) | (g << 8) | b);
}

u32 lerp(u32 a, u32 b, int step, int total)
{
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (br - ar) * step / total;
    int g = ag + (bg - ag) * step / total;
    int bl = ab + (bb - ab) * step / total;
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

bool rect_intersects(const rect &a, const rect &b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

rect rect_clip(const rect &a, const rect &b)
{
    rect r;
    r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    if (r.x0 > r.x1) r.x0 = r.x1;
    if (r.y0 > r.y1) r.y0 = r.y1;
    return r;
}

void set_clip(const rect &r)
{
    g_clip = r;
}

/* ------------------------------------------------------------------ */
/* damage tracking                                                     */
/* ------------------------------------------------------------------ */

void damage_reset()
{
    g_damage.count = 0;
}

void damage_add(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > (int)g_width)
        x1 = (int)g_width;
    if (y1 > (int)g_height)
        y1 = (int)g_height;
    if (x0 >= x1 || y0 >= y1)
        return;

    for (int i = 0; i < g_damage.count; i++) {
        rect &r = g_damage.items[i];
        if (x0 <= r.x1 && r.x0 <= x1 && y0 <= r.y1 && r.y0 <= y1) {
            if (x0 < r.x0) r.x0 = x0;
            if (y0 < r.y0) r.y0 = y0;
            if (x1 > r.x1) r.x1 = x1;
            if (y1 > r.y1) r.y1 = y1;
            return;
        }
    }
    if (g_damage.count < kMaxDamage) {
        g_damage.items[g_damage.count++] = {x0, y0, x1, y1};
        return;
    }
    /* Fall back to the bounding box of everything, including the new rect. */
    rect &r = g_damage.items[0];
    if (x0 < r.x0) r.x0 = x0;
    if (y0 < r.y0) r.y0 = y0;
    if (x1 > r.x1) r.x1 = x1;
    if (y1 > r.y1) r.y1 = y1;
    g_damage.count = 1;
}

void damage_all()
{
    g_damage.count = 1;
    g_damage.items[0] = {0, 0, (int)g_width, (int)g_height};
}

/* ------------------------------------------------------------------ */
/* drawing primitives (target = back buffer or framebuffer)            */
/* ------------------------------------------------------------------ */

void fill_rect(int x, int y, int w, int h, u32 rgb)
{
    int x0 = x < g_clip.x0 ? g_clip.x0 : x;
    int y0 = y < g_clip.y0 ? g_clip.y0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > g_clip.x1) x1 = g_clip.x1;
    if (y1 > g_clip.y1) y1 = g_clip.y1;
    if (x0 >= x1 || y0 >= y1)
        return;
    u32 pixel = pack(rgb);
    for (int row = y0; row < y1; row++) {
        u32 *line = g_target + (u32)row * (u32)g_stride + (u32)x0;
        for (int col = x0; col < x1; col++)
            *line++ = pixel;
    }
}

void blend_rect(int x, int y, int w, int h, u32 rgb, u32 alpha)
{
    if (alpha == 0 || w <= 0 || h <= 0)
        return;
    if (alpha >= 255) {
        fill_rect(x, y, w, h, rgb);
        return;
    }
    int x0 = x < g_clip.x0 ? g_clip.x0 : x;
    int y0 = y < g_clip.y0 ? g_clip.y0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > g_clip.x1) x1 = g_clip.x1;
    if (y1 > g_clip.y1) y1 = g_clip.y1;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int row = y0; row < y1; row++) {
        u32 *line = g_target + (u32)row * (u32)g_stride + (u32)x0;
        for (int col = x0; col < x1; col++) {
            *line = blend(*line, rgb, alpha);
            line++;
        }
    }
}

/* Precomputed quarter-circle insets for radii up to kMaxRadius. */
u8 g_inset[kMaxRadius + 1][kMaxRadius + 1];

void vgrad(int x, int y, int w, int h, u32 top, u32 bottom)
{
    for (int i = 0; i < h; i++)
        fill_rect(x, y + i, w, 1, lerp(top, bottom, i, h > 1 ? h - 1 : 1));
}

void vgrad_rounded_top(int x, int y, int w, int h, u32 top, u32 bottom,
                       int radius)
{
    if (radius > kMaxRadius)
        radius = kMaxRadius;
    for (int i = 0; i < h; i++) {
        int inset = i < radius ? g_inset[radius][i] : 0;
        if (w - inset * 2 <= 0)
            continue;
        fill_rect(x + inset, y + i, w - inset * 2, 1,
                  lerp(top, bottom, i, h > 1 ? h - 1 : 1));
    }
}

void hline(int x, int y, int w, u32 color)
{
    fill_rect(x, y, w, 1, color);
}

/* Precomputed quarter-circle insets for radii up to kMaxRadius. */
void build_round_table()
{
    for (int r = 0; r <= kMaxRadius; r++) {
        for (int k = 0; k <= r; k++) {
            /* first column that lies inside the circle for this row */
            int dy2 = (2 * r - 2 * k - 1) * (2 * r - 2 * k - 1);
            int limit = 4 * r * r - dy2;
            int inset = r;
            for (int j = 0; j < r; j++) {
                int dx2 = (2 * r - 2 * j - 1) * (2 * r - 2 * j - 1);
                if (dx2 <= limit) {
                    inset = j;
                    break;
                }
            }
            g_inset[r][k] = (u8)inset;
        }
    }
}

void rounded_rect(int x, int y, int w, int h, int radius, u32 rgb, u32 alpha)
{
    if (w <= 0 || h <= 0)
        return;
    if (radius > kMaxRadius)
        radius = kMaxRadius;
    if (radius * 2 > w || radius * 2 > h)
        radius = (w < h ? w : h) / 2;
    if (radius <= 0) {
        blend_rect(x, y, w, h, rgb, alpha);
        return;
    }

    for (int row = 0; row < h; row++) {
        int inset = 0;
        if (row < radius)
            inset = g_inset[radius][row];
        else if (row >= h - radius)
            inset = g_inset[radius][h - 1 - row];
        if (w - inset * 2 <= 0)
            continue;
        if (alpha >= 255) {
            fill_rect(x + inset, y + row, w - inset * 2, 1, rgb);
            /* soften the two boundary pixels */
        } else {
            blend_rect(x + inset, y + row, w - inset * 2, 1, rgb, alpha);
        }
        if (inset > 0) {
            int partial = (int)((alpha * 110) / 255);
            if (partial > 0) {
                blend_rect(x + inset - 1, y + row, 1, 1, rgb, (u32)partial);
                blend_rect(x + w - inset, y + row, 1, 1, rgb, (u32)partial);
            }
        }
    }
}

void border(int x, int y, int w, int h, u32 color)
{
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

void soft_shadow(int x, int y, int w, int h, int spread)
{
    /* Eight one-pixel outlines with decreasing alpha form a cheap soft
     * glow without touching the interior (which the window covers). */
    for (int i = 0; i < spread; i++) {
        u32 alpha = (u32)(28 - i * 3 > 4 ? 28 - i * 3 : 4);
        int sx = x - i - 2;
        int sy = y - i + 2;
        int sw = w + (i + 2) * 2;
        int sh = h + (i + 2) * 2;
        blend_rect(sx, sy, sw, 1, 0x0A1622, alpha);
        blend_rect(sx, sy + sh - 1, sw, 1, 0x0A1622, alpha);
        blend_rect(sx, sy, 1, sh, 0x0A1622, alpha);
        blend_rect(sx + sw - 1, sy, 1, sh, 0x0A1622, alpha);
    }
}

void blit_from_bg(const rect &r)
{
    if (!g_bg)
        return;
    int x0 = r.x0 < 0 ? 0 : r.x0;
    int y0 = r.y0 < 0 ? 0 : r.y0;
    int x1 = r.x1 > (int)g_width ? (int)g_width : r.x1;
    int y1 = r.y1 > (int)g_height ? (int)g_height : r.y1;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++)
            g_target[(u32)y * (u32)g_stride + (u32)x] =
                g_bg[(u32)y * (u32)g_width + (u32)x];
    }
}

void present()
{
    if (!g_back_valid)
        return;
    for (int i = 0; i < g_damage.count; i++) {
        const rect &r = g_damage.items[i];
        int x0 = r.x0 < 0 ? 0 : r.x0;
        int y0 = r.y0 < 0 ? 0 : r.y0;
        int x1 = r.x1 > (int)g_width ? (int)g_width : r.x1;
        int y1 = r.y1 > (int)g_height ? (int)g_height : r.y1;
        if (x0 >= x1 || y0 >= y1)
            continue;
        for (int y = y0; y < y1; y++) {
            u32 *dst = g_fb + (u32)y * g_pitch_px + (u32)x0;
            u32 *src = g_back + (u32)y * (u32)g_width + (u32)x0;
            for (int x = x0; x < x1; x++)
                *dst++ = *src++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* text                                                                */
/* ------------------------------------------------------------------ */

int draw_mask(int x, int y, const u8 *mask, int stride, int w, int h, u32 fg,
              int scale)
{
    for (int row = 0; row < h; row++) {
        int py = y + row * scale;
        for (int col = 0; col < w; col++) {
            u32 alpha = mask[row * stride + col];
            if (alpha == 0)
                continue;
            int px = x + col * scale;
            for (int dy = 0; dy < scale; dy++) {
                if (py + dy < g_clip.y0 || py + dy >= g_clip.y1)
                    continue;
                for (int dx = 0; dx < scale; dx++) {
                    if (px + dx < g_clip.x0 || px + dx >= g_clip.x1)
                        continue;
                    u32 *p = g_target + (u32)(py + dy) * (u32)g_stride +
                             (u32)(px + dx);
                    *p = blend(*p, fg, alpha);
                }
            }
        }
    }
    return w * scale;
}

int draw_bitmap_char(u32 codepoint, int x, int y, u32 fg)
{
    if (codepoint < 32 || codepoint >= 128)
        codepoint = '?';
    const u8 *rows = (const u8 *)font8x8_basic[codepoint];
    for (int row = 0; row < 8; row++) {
        u8 bits = rows[row];
        for (int col = 0; col < 8; col++) {
            /* font8x8_basic (Daniel Hepper) stores bit 0 as the leftmost
             * pixel; reading it MSB-first mirrors every fallback glyph. */
            if (!(bits & (1 << col)))
                continue;
            for (int dy = 0; dy < 2; dy++) {
                int px = x + col;
                int py = y + row * 2 + dy;
                if (px < g_clip.x0 || px >= g_clip.x1 ||
                    py < g_clip.y0 || py >= g_clip.y1)
                    continue;
                u32 *p = g_target + (u32)py * (u32)g_stride + (u32)px;
                *p = blend(*p, fg, 255);
            }
        }
    }
    return 8;
}

int glyph_advance(u32 codepoint)
{
    if (ttf_available()) {
        struct ttf_glyph glyph;
        if (ttf_render(codepoint, &glyph) == 0 && glyph.adv)
            return glyph.adv;
    }
    return 8;
}

int draw_codepoint(u32 codepoint, int x, int y, u32 fg, int scale)
{
    if (!ttf_available())
        return draw_bitmap_char(codepoint, x, y, fg);

    struct ttf_glyph glyph;
    if (ttf_render(codepoint, &glyph) != 0 || (glyph.w == 0 && glyph.adv == 0)) {
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

int draw_text(const char *text, int x, int y, u32 fg, int scale)
{
    const u8 *cursor = (const u8 *)text;
    int start = x;
    while (*cursor) {
        u32 codepoint = *cursor++;
        if (codepoint >= 0x80) {
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
            }
            for (int i = 0; i < extra && (*cursor & 0xC0) == 0x80; i++)
                codepoint = (codepoint << 6) | (*cursor++ & 0x3F);
        }
        x += draw_codepoint(codepoint, x, y, fg, scale);
    }
    return x - start;
}

int text_width(const char *text, int scale)
{
    int width = 0;
    const u8 *cursor = (const u8 *)text;
    while (*cursor) {
        u32 codepoint = *cursor++;
        if (codepoint >= 0x80) {
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
            }
            for (int i = 0; i < extra && (*cursor & 0xC0) == 0x80; i++)
                codepoint = (codepoint << 6) | (*cursor++ & 0x3F);
        }
        width += glyph_advance(codepoint) * scale;
    }
    return width;
}

void draw_text_center(const char *text, int cx, int y, u32 fg, int scale)
{
    draw_text(text, cx - text_width(text, scale) / 2, y, fg, scale);
}

/* ------------------------------------------------------------------ */
/* wallpaper                                                           */
/* ------------------------------------------------------------------ */

void soft_circle(u32 *image, int cx, int cy, int radius, u32 rgb, u32 max_alpha)
{
    int x0 = cx - radius < 0 ? 0 : cx - radius;
    int y0 = cy - radius < 0 ? 0 : cy - radius;
    int x1 = cx + radius;
    int y1 = cy + radius;
    if (x1 > (int)g_width) x1 = (int)g_width;
    if (y1 > (int)g_height) y1 = (int)g_height;
    i64 r2 = (i64)radius * radius;
    for (int y = y0; y < y1; y++) {
        i64 dy = y - cy;
        for (int x = x0; x < x1; x++) {
            i64 dx = x - cx;
            i64 d2 = dx * dx + dy * dy;
            if (d2 >= r2)
                continue;
            u32 alpha = (u32)((r2 - d2) * (i64)max_alpha / r2);
            u32 *p = &image[(u32)y * g_width + (u32)x];
            *p = blend(*p, rgb, alpha);
        }
    }
}

void build_background()
{
    if (!g_bg)
        return;
    for (int y = 0; y < (int)g_height; y++) {
        u32 color;
        if (y < 140)
            color = lerp(0x0D2A4A, 0x1C4E7A, y, 139);
        else
            color = lerp(0x1C4E7A, 0x8CC4E8, y - 140,
                         (int)g_height - 140 > 0 ? (int)g_height - 140 : 1);
        for (int x = 0; x < (int)g_width; x++)
            g_bg[(u32)y * g_width + (u32)x] = pack(color);
    }
    soft_circle(g_bg, (int)g_width / 5, (int)g_height / 4,
                (int)g_width / 3, 0xBFE4FF, 26);
    soft_circle(g_bg, (int)g_width * 4 / 5, (int)g_height * 3 / 5,
                (int)g_width / 4, 0xFFFFFF, 30);
    soft_circle(g_bg, (int)g_width / 2, (int)g_height + ui(120),
                (int)g_width / 3, 0x0A2A48, 40);
}

/* ------------------------------------------------------------------ */
/* menu bar and dock                                                   */
/* ------------------------------------------------------------------ */

void draw_menubar()
{
    vgrad(0, 0, (int)g_width, g_menu_h, 0xFBFDFE, 0xD7E3EF);
    blend_rect(0, 0, (int)g_width, g_menu_h, 0xFFFFFF, 70);
    hline(0, g_menu_h - 1, (int)g_width, 0x7E93A8);

    int text_y = (g_menu_h - ttf_line_height()) / 2 + 1;
    int x = ui(14);

    /* Zhaoxu mark: a small aqua roundel with a Z. */
    int logo = ui(20);
    int logo_y = (g_menu_h - logo) / 2;
    rounded_rect(x, logo_y, logo, logo, logo / 2, 0x1E6FB8, 255);
    rounded_rect(x + 2, logo_y + 2, logo - 4, logo / 2, logo / 3, 0xFFFFFF,
                 110);
    draw_text_center("Z", x + logo / 2, logo_y + 1, 0xFFFFFF, 1);
    x += logo + ui(12);

    x += draw_text("Zhaoxu OS", x, text_y, 0x16293C, 1) + ui(20);
    x += draw_text("文件", x, text_y, 0x36495C, 1) + ui(18);
    x += draw_text("编辑", x, text_y, 0x36495C, 1) + ui(18);
    x += draw_text("显示", x, text_y, 0x36495C, 1) + ui(18);
    draw_text("帮助", x, text_y, 0x36495C, 1);

    struct rtc_time now;
    char clock[16];
    if (rtc_read(&now) == 0) {
        ksnprintf(clock, sizeof(clock), "%02u:%02u:%02u",
                  (unsigned)now.hour, (unsigned)now.minute,
                  (unsigned)now.second);
        g_clock_last = (u32)now.hour * 3600 + (u32)now.minute * 60 +
                       (u32)now.second;
    } else {
        ksnprintf(clock, sizeof(clock), "--:--:--");
    }
    g_clock_text_width = (u32)text_width(clock, 1);
    draw_text(clock, (int)g_width - (int)g_clock_text_width - ui(26), text_y,
              0x16293C, 1);

    if (ttf_available())
        draw_text("中", (int)g_width - (int)g_clock_text_width - ui(56),
                  text_y, 0x51606F, 1);
}

void dock_metrics(int *dx, int *dy, int *dw, int *dh, int *spacing,
                  int *icon)
{
    int count = kMaxWindows;
    *icon = ui(46);
    *spacing = *icon + ui(18);
    *dw = *spacing * count + ui(24);
    *dh = ui(78);
    *dx = ((int)g_width - *dw) / 2;
    *dy = (int)g_height - *dh - ui(14);
}

void draw_icon_glyph(int kind, int cx, int cy, int size, u32 color)
{
    if (kind == KIND_WELCOME) {
        draw_text_center("Z", cx, cy - ttf_line_height() / 2, color, 2);
    } else if (kind == KIND_FILES) {
        int w = size / 2;
        int h = size / 3;
        rounded_rect(cx - w / 2, cy - h / 2 + 2, w, h, 3, color, 255);
        rounded_rect(cx - w / 2, cy - h / 2 - 3, w / 2, h / 2 + 2, 3, color,
                     255);
    } else if (kind == KIND_TERMINAL) {
        draw_text(">", cx - size / 4, cy - ttf_line_height() / 2, color, 1);
        fill_rect(cx - size / 8, cy + size / 5, size / 3, 2, color);
    } else if (kind == KIND_CALC) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                fill_rect(cx - size / 3 + c * size / 4,
                          cy - size / 3 + r * size / 4, size / 8, size / 8,
                          color);
    } else if (kind == KIND_NOTES) {
        for (int i = 0; i < 4; i++)
            fill_rect(cx - size / 3, cy - size / 4 + i * size / 7,
                      size * 2 / 3 - (i == 3 ? size / 4 : 0), 2, color);
    } else if (kind == KIND_MONITOR) {
        fill_rect(cx - size / 3, cy - size / 4, size / 3, size / 2, color);
        rounded_rect(cx - size / 2, cy + size / 3, size, 2, 1, color, 255);
        fill_rect(cx - size / 5, cy - size / 8, 3, size / 3, 0xFFFFFF);
        fill_rect(cx - size / 20, cy - size / 3, 3, size / 2, 0xFFFFFF);
    } else {
        /* installer: downward arrow */
        fill_rect(cx - 2, cy - size / 3, 4, size / 2, color);
        for (int i = 0; i < size / 5; i++)
            fill_rect(cx - size / 6 + i, cy + size / 7 + i,
                      size / 3 - i * 2, 1, color);
    }
}

void draw_dock()
{
    int dx, dy, dw, dh, spacing, icon;
    dock_metrics(&dx, &dy, &dw, &dh, &spacing, &icon);

    rounded_rect(dx + 3, dy + 5, dw, dh, ui(20), 0x0A1A2A, 90);
    rounded_rect(dx, dy, dw, dh, ui(20), 0xF4FAFF, 205);
    rounded_rect(dx + 2, dy + 2, dw - 4, dh / 2, ui(18), 0xFFFFFF, 90);
    rounded_rect(dx + 1, dy + 1, dw - 2, dh - 2, ui(19), 0x7E93A8, 60);

    static const u32 colors[kMaxWindows] = {
        0x2F7FD0, 0xE9A23B, 0x2E9E6B, 0x8E64C9, 0xD06A8C, 0x2D8FB8, 0xC0533F
    };
    static const char *labels[kMaxWindows] = {
        "欢迎", "文件", "终端", "计算", "便签", "监视", "安装"
    };

    for (int i = 0; i < kMaxWindows; i++) {
        int cx = dx + ui(12) + spacing * i + icon / 2;
        int base_y = dy + dh - icon - ui(21);
        int draw_icon = icon;
        int lift = 0;
        if (i == g_hover_icon) {
            draw_icon = icon * 5 / 4;
            lift = ui(9);
        }
        int ix = cx - draw_icon / 2;
        int iy = base_y - lift + (icon - draw_icon) / 2;
        u32 color = colors[i];
        rounded_rect(ix + 2, iy + 3, draw_icon, draw_icon, draw_icon / 5,
                     0x0A1A2A, 60);
        rounded_rect(ix, iy, draw_icon, draw_icon, draw_icon / 5, color, 255);
        rounded_rect(ix + 2, iy + 2, draw_icon - 4, draw_icon / 2,
                     draw_icon / 6, 0xFFFFFF, 65);
        draw_icon_glyph(i, cx, iy + draw_icon / 2, draw_icon, 0xFFFFFF);

        int label_y = dy + dh - ttf_line_height() - ui(1);
        if (i == g_hover_icon)
            draw_text_center(labels[i], cx, label_y, 0x1B344C, 1);
    }
}

/* ------------------------------------------------------------------ */
/* window chrome                                                       */
/* ------------------------------------------------------------------ */

void window_rect(const window *win, rect *r)
{
    r->x0 = win->x;
    r->y0 = win->y;
    r->x1 = win->x + win->w;
    r->y1 = win->y + win->h;
}

bool close_button_at(const window *win, int x, int y)
{
    int r = ui(8);
    int cx = win->x + win->w - ui(22);
    int cy = win->y + g_title_h / 2;
    int dx = x - cx;
    int dy = y - cy;
    return dx * dx + dy * dy <= r * r && x >= win->x && y >= win->y;
}

void traffic_lights(const window *win)
{
    int r = ui(7);
    int cy = win->y + g_title_h / 2;
    int gap = ui(20);
    int cx = win->x + win->w - ui(22);
    struct { u32 fill, border; } lights[3] = {
        {0xE9685C, 0xA53A30}, {0xF0BE4E, 0xA87B24}, {0x62C34E, 0x3D8A2E}
    };
    for (int i = 0; i < 3; i++) {
        int x = cx - i * gap;
        rounded_rect(x - r, cy - r, r * 2, r * 2, r, lights[i].fill, 255);
        rounded_rect(x - r + 1, cy - r + 1, r * 2 - 2, r, r / 2, 0xFFFFFF,
                     90);
        for (int ring = -r; ring <= r; ring++) {
            int dy2 = ring * ring;
            int span = 0;
            while (span * span + dy2 <= r * r)
                span++;
            if (span <= 0)
                continue;
            fill_rect(x - span, cy + ring, span * 2, 1, lights[i].border);
        }
        rounded_rect(x - r + 1, cy - r + 1, r * 2 - 2, r * 2 - 2, r - 1,
                     lights[i].fill, 255);
    }
}

void draw_titlebar(const window *win)
{
    int radius = ui(10);
    vgrad_rounded_top(win->x + 1, win->y + 1, win->w - 2, g_title_h - 2,
                      0xFDFEFF, 0xC9DBEC, radius);
    blend_rect(win->x + radius, win->y + 1, win->w - radius * 2,
               g_title_h / 2, 0xFFFFFF, 60);
    hline(win->x + 1, win->y + g_title_h - 1, win->w - 2, 0x8EA2B6);
    draw_text(win->title, win->x + ui(14),
              win->y + (g_title_h - ttf_line_height()) / 2 + 1, 0x1B2C3D, 1);
    traffic_lights(win);
}

/* ------------------------------------------------------------------ */
/* window content                                                      */
/* ------------------------------------------------------------------ */

void render_welcome(const window *win)
{
    int x = win->x + ui(26);
    int y = win->y + g_title_h + ui(14);
    u32 title = 0x123A5A;
    u32 body = 0x2A4258;

    y += draw_text("Zhaoxu OS", x, y, title, 2) + ui(2);
    draw_text("兆旭操作系统 · Desktop-Shell", x, y, 0x2B6FA8, 1);
    y += ttf_line_height() + ui(10);

    char line[96];
    ksnprintf(line, sizeof(line), "版本 %s \"%s\" · 内核 %s Kernel",
              ZHAOXU_VERSION, ZHAOXU_CODENAME, ZHAOXU_KERNEL);
    y += draw_text(line, x, y, body, 1) + ui(4);
    draw_text("DS-WM 窗口管理器 · Aqua 视觉 · 20px 矢量字体", x, y, 0x4A627A,
              1);
    y += ttf_line_height() + ui(8);

    fill_rect(x, y, win->w - ui(52), 1, 0xD2DDE7);
    y += ui(12);

    draw_text("已经可以使用的原生应用", x, y, title, 1);
    y += ttf_line_height() + ui(2);
    draw_text("· 终端：在桌面窗口里直接运行 Command-Shell", x, y, body, 1);
    y += ttf_line_height();
    draw_text("· 文件：浏览 /mnt 数据盘，点击文本在便签打开", x, y, body, 1);
    y += ttf_line_height();
    draw_text("· 计算器、便签、监视器与图形安装器", x, y, body, 1);
    y += ttf_line_height() + ui(6);
    draw_text("拖动标题栏移动窗口，点左上角红绿灯关闭。", x, y, 0x53687C, 1);
    y += ttf_line_height() + ui(2);
    draw_text("按 ESC 返回 Command-Shell。", x, y, 0x9A3342, 1);
}

void load_files()
{
    g_file_count = 0;
    g_files_selected = -1;
    vfs_list("/mnt", collect_file, nullptr);
}

int files_row_at(const window *win, int x, int y)
{
    int body_y = win->y + g_title_h + ui(10) + ttf_line_height() + ui(6) +
                 ui(8);
    int row_h = ttf_line_height() + ui(8);
    if (x < win->x + ui(12) || x >= win->x + win->w - ui(12))
        return -1;
    int index = (y - body_y) / row_h;
    if (index < 0 || index >= g_file_count)
        return -1;
    return index;
}

void open_file_in_notes(const file_entry *entry)
{
    char path[96];
    ksnprintf(path, sizeof(path), "/mnt/%s", entry->name);
    u8 *data = nullptr;
    u64 size = 0;
    if (vfs_read_all(path, &data, &size) != 0)
        return;
    int copy = (int)(size < kNotesSize - 1 ? size : kNotesSize - 1);
    for (int i = 0; i < copy; i++)
        g_notes[i] = (char)data[i];
    g_notes_len = copy;
    g_notes[g_notes_len] = 0;
    strncpy(g_notes_name, entry->name, sizeof(g_notes_name) - 1);
    g_notes_name[sizeof(g_notes_name) - 1] = 0;
    kfree(data);

    g_windows[KIND_NOTES].open = true;
    g_windows[KIND_NOTES].z = ++g_next_z;
    g_focus = KIND_NOTES;
}

void render_files(const window *win)
{
    int x = win->x + ui(16);
    int y = win->y + g_title_h + ui(10);
    u32 body = 0x24384E;
    draw_text("卷 /mnt · FAT32 只读", x, y, 0x5A6E82, 1);
    y += ttf_line_height() + ui(6);
    fill_rect(x, y, win->w - ui(32), 1, 0xD5E0EA);
    y += ui(8);

    if (g_file_count == 0) {
        draw_text("/mnt 为空或没有挂载数据盘。", x, y, 0x9A5B20, 1);
        return;
    }

    int row_h = ttf_line_height() + ui(8);
    for (int i = 0; i < g_file_count; i++) {
        if (y + row_h > win->y + win->h - ui(12))
            break;
        const file_entry *entry = &g_files[i];
        if (i == g_files_selected)
            rounded_rect(x - ui(4), y - ui(2), win->w - ui(24), row_h,
                         ui(6), 0x2F7FD0, 45);
        u32 chip = entry->is_dir ? 0xE9A23B : 0x8ECAE6;
        rounded_rect(x, y + ui(3), ui(16), ui(16), ui(4), chip, 255);
        draw_text(entry->name, x + ui(24), y, body, 1);
        if (!entry->is_dir) {
            char size[24];
            ksnprintf(size, sizeof(size), "%llu B", entry->size);
            int sw = text_width(size, 1);
            draw_text(size, win->x + win->w - ui(18) - sw, y, 0x6B7F93, 1);
        }
        y += row_h;
    }
}

/* ---- terminal ---------------------------------------------------- */

void term_clear()
{
    g_term_count = 1;
    g_term_row = 0;
    g_term_col = 0;
    for (int i = 0; i < kTermLines; i++)
        g_term[i][0] = 0;
}

void term_scroll()
{
    for (int i = 1; i < kTermLines; i++)
        for (int c = 0; c <= kTermCols; c++)
            g_term[i - 1][c] = g_term[i][c];
    for (int c = 0; c <= kTermCols; c++)
        g_term[kTermLines - 1][c] = 0;
    if (g_term_row > 0)
        g_term_row--;
}

void term_newline()
{
    g_term_col = 0;
    g_term_row++;
    if (g_term_row >= kTermLines) {
        term_scroll();
        g_term_row = kTermLines - 1;
    }
    if (g_term_count <= g_term_row)
        g_term_count = g_term_row + 1;
}

void term_output_char(char c)
{
    if (c == '\n') {
        term_newline();
        return;
    }
    if (c == '\r') {
        g_term_col = 0;
        return;
    }
    if (c == '\t') {
        int target = (g_term_col + 4) & ~3;
        while (g_term_col < target)
            term_output_char(' ');
        return;
    }
    if ((u8)c < 32)
        return;
    if (g_term_col >= kTermCols) {
        term_newline();
    }
    g_term[g_term_row][g_term_col++] = c;
    g_term[g_term_row][g_term_col] = 0;
}

void term_sink(char c)
{
    term_output_char(c);
}

void term_append(const char *text, u32 color)
{
    (void)color;
    while (*text)
        term_output_char(*text++);
}

void term_prompt(const char *line)
{
    term_append("zhaoxu> ", 0x7FE3A1);
    term_append(line, 0xE6F2FB);
    term_newline();
}

void terminal_submit()
{
    g_term_input[g_term_input_len] = 0;

    if (strcmp(g_term_input, "clear") == 0) {
        term_clear();
        g_term_input_len = 0;
        damage_add(g_windows[KIND_TERMINAL].x,
                   g_windows[KIND_TERMINAL].y,
                   g_windows[KIND_TERMINAL].w, g_windows[KIND_TERMINAL].h);
        return;
    }

    if (g_term_input_len > 0) {
        strncpy(g_term_history[g_term_history_count % kHistory],
                g_term_input, kTermCols);
        g_term_history[g_term_history_count % kHistory][kTermCols] = 0;
        g_term_history_count++;
        g_term_history_pos = g_term_history_count;
    }

    term_prompt(g_term_input);
    console_set_sink(term_sink);
    shell_exec_line_captured(g_term_input);
    console_set_sink(nullptr);
    if (g_term_col != 0)
        term_newline();

    g_term_input_len = 0;
    g_term_input[0] = 0;
    damage_add(g_windows[KIND_TERMINAL].x, g_windows[KIND_TERMINAL].y,
               g_windows[KIND_TERMINAL].w, g_windows[KIND_TERMINAL].h);
}

void render_terminal(const window *win)
{
    int pad = ui(12);
    int bx = win->x + pad;
    int by = win->y + g_title_h + pad;
    int bw = win->w - pad * 2;
    int bottom = win->y + win->h - pad;
    int bh = bottom - by;

    rounded_rect(bx, by, bw, bh, ui(8), 0x0C1B26, 240);
    border(bx, by, bw, bh, 0x2C4457);

    rect saved = g_clip;
    g_clip = rect_clip(saved, {bx + 1, by + 1, bx + bw - 1, by + bh - 1});

    int line_h = ttf_line_height();
    int input_y = bottom - line_h - ui(6);
    int lines_visible = (input_y - (by + ui(6))) / line_h;
    if (lines_visible < 1)
        lines_visible = 1;
    int first = g_term_count > lines_visible ? g_term_count - lines_visible : 0;
    int ty = by + ui(6);
    for (int i = first; i < g_term_count; i++) {
        if (ty + line_h > input_y)
            break;
        draw_text(g_term[i], bx + ui(8), ty, 0xC9E3F5, 1);
        ty += line_h;
    }

    int px = bx + ui(8);
    px += draw_text("zhaoxu> ", px, input_y, 0x7FE3A1, 1);
    px += draw_text(g_term_input, px, input_y, 0xEAF6FF, 1);
    if (g_blink_on && g_focus == KIND_TERMINAL)
        fill_rect(px + 1, input_y + ui(2), 2, ttf_line_height() - ui(4),
                  0xBFE4FF);

    g_clip = saved;
}

/* ---- calculator -------------------------------------------------- */

void calc_button_rect(const window *win, int index, int *x, int *y, int *w,
                      int *h)
{
    int pad = ui(16);
    int grid_w = win->w - pad * 2;
    int top = win->y + g_title_h + ui(96);
    int grid_h = win->y + win->h - top - ui(16);
    int cw = grid_w / 4;
    int ch = grid_h / 4;
    *x = win->x + pad + (index % 4) * cw;
    *y = top + (index / 4) * ch;
    *w = cw - ui(6);
    *h = ch - ui(6);
}

void calc_update_display()
{
    ksnprintf(g_calc_display, sizeof(g_calc_display), "%lld",
              (long long)g_calc_value);
}

i64 calc_apply(i64 a, i64 b, char op)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b != 0 ? a / b : 0;
    default: return b;
    }
}

void calc_press(int index)
{
    static const char *labels[16] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", "+", "C", "="
    };
    const char *label = labels[index];
    if (label[0] >= '0' && label[0] <= '9') {
        if (g_calc_fresh) {
            g_calc_value = 0;
            g_calc_fresh = false;
        }
        g_calc_value = g_calc_value * 10 + (label[0] - '0');
    } else if (label[0] == 'C') {
        g_calc_value = 0;
        g_calc_pending = 0;
        g_calc_op = 0;
        g_calc_fresh = false;
    } else if (label[0] == '=') {
        if (g_calc_op)
            g_calc_value = calc_apply(g_calc_pending, g_calc_value,
                                      g_calc_op);
        g_calc_op = 0;
        g_calc_fresh = true;
    } else {
        if (g_calc_op)
            g_calc_value = calc_apply(g_calc_pending, g_calc_value,
                                      g_calc_op);
        g_calc_pending = g_calc_value;
        g_calc_op = label[0];
        g_calc_fresh = true;
    }
    calc_update_display();
    damage_add(g_windows[KIND_CALC].x, g_windows[KIND_CALC].y,
               g_windows[KIND_CALC].w, g_windows[KIND_CALC].h);
}

int calc_button_at(const window *win, int x, int y)
{
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        calc_button_rect(win, i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh)
            return i;
    }
    return -1;
}

void render_calc(const window *win)
{
    int pad = ui(16);
    int dx = win->x + pad;
    int dy = win->y + g_title_h + ui(14);
    rounded_rect(dx, dy, win->w - pad * 2, ui(62), ui(10), 0xE8F1F8, 255);
    border(dx, dy, win->w - pad * 2, ui(62), 0xB6C8D8);
    int dw = text_width(g_calc_display, 2);
    draw_text(g_calc_display, dx + win->w - pad * 2 - dw - ui(10),
              dy + ui(8), 0x123A5A, 2);

    static const char *labels[16] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "0", "+", "C", "="
    };
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        calc_button_rect(win, i, &bx, &by, &bw, &bh);
        u32 color = (i == 15) ? 0x2F7FD0
                    : (i % 4 == 3 || i == 14) ? 0xE3EBF3
                                              : 0xFBFDFF;
        u32 text = (i == 15) ? 0xFFFFFF : 0x1B2C3D;
        rounded_rect(bx, by, bw, bh, ui(8), color, 255);
        border(bx, by, bw, bh, 0xA9BCCB);
        draw_text_center(labels[i], bx + bw / 2,
                         by + (bh - ttf_line_height()) / 2, text, 1);
    }
}

/* ---- notes ------------------------------------------------------- */

void render_notes(const window *win)
{
    int x = win->x + ui(18);
    int y = win->y + g_title_h + ui(12);
    draw_text(g_notes_name[0] ? g_notes_name : "未命名便签", x, y, 0x3D566E,
              1);
    y += ttf_line_height() + ui(4);
    fill_rect(x, y, win->w - ui(36), 1, 0xD5E0EA);
    y += ui(8);

    int line_x = x;
    int line_y = y;
    int max_chars = (win->w - ui(44)) / 9;
    if (max_chars < 8)
        max_chars = 8;
    int column = 0;
    for (int i = 0; i < g_notes_len;) {
        char c = g_notes[i];
        if (c == '\n') {
            line_y += ttf_line_height();
            column = 0;
            line_x = x;
            i++;
            if (line_y + ttf_line_height() > win->y + win->h - ui(12))
                break;
            continue;
        }

        /* Notes holds UTF-8: draw whole codepoints, never single bytes. */
        unsigned char lead = (u8)c;
        int bytes = 1;
        if (lead >= 0xF0)
            bytes = 4;
        else if (lead >= 0xE0)
            bytes = 3;
        else if (lead >= 0xC0)
            bytes = 2;
        if (i + bytes > g_notes_len)
            bytes = 1;

        if (column >= max_chars) {
            line_y += ttf_line_height();
            column = 0;
            line_x = x;
            if (line_y + ttf_line_height() > win->y + win->h - ui(12))
                break;
        }

        char text[5] = { 0, 0, 0, 0, 0 };
        for (int b = 0; b < bytes && i + b < g_notes_len; b++)
            text[b] = g_notes[i + b];
        int width = draw_text(text, line_x, line_y, 0x24384E, 1);
        line_x += width;
        column++;
        i += bytes;
    }

    if (g_focus == KIND_NOTES && g_blink_on)
        fill_rect(line_x + 1, line_y + ui(2), 2, ttf_line_height() - ui(4),
                  0x2F7FD0);
}

void notes_key(char c)
{
    if (c == '\b' || c == 0x7F) {
        if (g_notes_len > 0) {
            g_notes_len--;
            while (g_notes_len > 0 &&
                   ((u8)g_notes[g_notes_len] & 0xC0) == 0x80)
                g_notes_len--;
        }
    } else if (c == '\n') {
        if (g_notes_len + 1 < kNotesSize)
            g_notes[g_notes_len++] = '\n';
    } else if ((u8)c >= 32 && g_notes_len + 1 < kNotesSize) {
        g_notes[g_notes_len++] = c;
    }
    g_notes[g_notes_len] = 0;
    damage_add(g_windows[KIND_NOTES].x, g_windows[KIND_NOTES].y,
               g_windows[KIND_NOTES].w, g_windows[KIND_NOTES].h);
}

/* ---- monitor ----------------------------------------------------- */

void draw_bar(int x, int y, int w, int h, u32 ratio, u32 color)
{
    rounded_rect(x, y, w, h, h / 2, 0xDCE6EF, 255);
    if (ratio > 100)
        ratio = 100;
    int filled = (int)((u64)(u32)(w - 2) * ratio / 100);
    if (filled > 0)
        rounded_rect(x + 1, y + 1, filled, h - 2, (h - 2) / 2, color, 255);
}

void render_monitor(const window *win)
{
    int x = win->x + ui(20);
    int y = win->y + g_title_h + ui(16);
    u32 label = 0x5A6E82;
    u32 value = 0x17435F;

    struct pmm_stats physical;
    pmm_get_stats(&physical);
    u64 heap_total = 0, heap_used = 0, heap_free = 0;
    heap_get_stats(&heap_total, &heap_used, &heap_free);

    const struct cpu_info *cpu = cpu_get_info();
    const struct boot_info *boot = boot_get_info();

    draw_text("内存", x, y, 0x123A5A, 1);
    y += ttf_line_height() + ui(6);

    char line[96];
    u32 used_pct = physical.managed_pages
                       ? (u32)(physical.used_pages * 100 /
                               physical.managed_pages)
                       : 0;
    ksnprintf(line, sizeof(line), "物理 %llu / %llu MiB",
              (physical.used_pages * PAGE_SIZE) >> 20,
              (physical.managed_pages * PAGE_SIZE) >> 20);
    draw_text(line, x, y, label, 1);
    y += ttf_line_height() + ui(2);
    draw_bar(x, y, win->w - ui(40), ui(12), used_pct, 0x2F7FD0);
    y += ui(22);

    u32 heap_pct = heap_total ? (u32)(heap_used * 100 / heap_total) : 0;
    ksnprintf(line, sizeof(line), "内核堆 %llu / %llu KiB",
              heap_used >> 10, heap_total >> 10);
    draw_text(line, x, y, label, 1);
    y += ttf_line_height() + ui(2);
    draw_bar(x, y, win->w - ui(40), ui(12), heap_pct, 0x2E9E6B);
    y += ui(24);

    fill_rect(x, y, win->w - ui(40), 1, 0xD5E0EA);
    y += ui(10);
    draw_text("系统", x, y, 0x123A5A, 1);
    y += ttf_line_height() + ui(2);

    u64 seconds = timer_ticks() / (timer_hz() ? timer_hz() : 1);
    ksnprintf(line, sizeof(line), "CPU  %s", cpu->brand);
    draw_text(line, x, y, label, 1);
    y += ttf_line_height();
    ksnprintf(line, sizeof(line), "运行  %llu:%02llu:%02llu",
              seconds / 3600, (seconds / 60) % 60, seconds % 60);
    draw_text(line, x, y, label, 1);
    y += ttf_line_height();
    ksnprintf(line, sizeof(line), "显示  %ux%u · 32 bpp",
              boot->width, boot->height);
    draw_text(line, x, y, label, 1);
    y += ttf_line_height();
    ksnprintf(line, sizeof(line), "磁盘  %d 个 AHCI 设备", ahci_disk_count());
    draw_text(line, x, y, label, 1);
    y += ttf_line_height();
    ksnprintf(line, sizeof(line), "进程  ring 3 %s",
              process_active() ? "运行中" : "空闲");
    draw_text(line, x, y, value, 1);
}

/* ---- installer --------------------------------------------------- */

void installer_button_rect(const window *win, int *bx, int *by, int *bw,
                           int *bh)
{
    *bx = win->x + ui(24);
    *by = win->y + win->h - ui(64);
    *bw = win->w - ui(48);
    *bh = ui(42);
}

bool installer_button_at(const window *win, int x, int y)
{
    int bx, by, bw, bh;
    installer_button_rect(win, &bx, &by, &bw, &bh);
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void paint_install_bar(const window *win)
{
    int bx = win->x + ui(24);
    int by = win->y + win->h - ui(100);
    int bw = win->w - ui(48);
    int bh = ui(20);

    char pct[16];
    ksnprintf(pct, sizeof(pct), "%u%%", (unsigned)g_install_pct);
    draw_text(pct, bx + bw - text_width(pct, 1), by - ttf_line_height(), 0x24384E,
              1);

    rounded_rect(bx, by, bw, bh, bh / 2, 0xFFFFFF, 255);
    border(bx, by, bw, bh, 0x8698AC);
    int filled = (int)((u64)(u32)(bw - 2) * g_install_pct / 100);
    if (filled > 0)
        rounded_rect(bx + 1, by + 1, filled, bh - 2, (bh - 2) / 2, 0x2F7FD0,
                     255);
}

void installer_progress(u32 done, u32 total)
{
    g_install_pct = total ? (u32)((u64)done * 100 / total) : 0;
    if (g_install_win) {
        damage_add(g_install_win->x, g_install_win->y, g_install_win->w,
                   g_install_win->h);
        repaint();
    }
}

void start_install(const window *win)
{
    int source = install_source_disk();
    int target = source >= 0 ? install_pick_target(source) : -1;

    g_install_win = win;
    g_install_message[0] = 0;
    if (source < 0) {
        g_install_phase = INSTALL_FAILED;
        ksnprintf(g_install_message, sizeof(g_install_message),
                  "未检测到 ZHAOXUEFI 安装介质。");
        return;
    }
    if (target < 0) {
        g_install_phase = INSTALL_FAILED;
        ksnprintf(g_install_message, sizeof(g_install_message),
                  "没有可用的目标磁盘（需要一块空白非数据盘）。");
        return;
    }

    g_install_phase = INSTALL_RUNNING;
    g_install_pct = 0;
    if (install_run(source, target, installer_progress) == 0) {
        g_install_phase = INSTALL_DONE;
        g_install_pct = 100;
        ksnprintf(g_install_message, sizeof(g_install_message),
                  "安装完成：disk %d 已写入引导扇区与 ESP，重启后可从该盘启动。",
                  target);
    } else {
        g_install_phase = INSTALL_FAILED;
        ksnprintf(g_install_message, sizeof(g_install_message), "安装失败：%s",
                  install_last_error());
    }
    damage_add(win->x, win->y, win->w, win->h);
}

void render_installer(const window *win)
{
    int x = win->x + ui(24);
    int y = win->y + g_title_h + ui(16);

    draw_text("将 Zhaoxu OS 安装到本地磁盘（MBR + FAT16 ESP）。", x, y,
              0x16293C, 1);
    y += ttf_line_height() + ui(8);

    int source = install_source_disk();
    int target = -1;
    char line[128];
    if (source >= 0) {
        target = install_pick_target(source);
        ksnprintf(line, sizeof(line), "安装源：disk %d · ZHAOXUEFI · %llu MiB",
                  source, (install_source_sectors() * 512) >> 20);
        draw_text(line, x, y, 0x2A5C3A, 1);
    } else {
        draw_text("安装源：未检测到 ZHAOXUEFI 安装介质", x, y, 0x9A3342, 1);
    }
    y += ttf_line_height() + ui(4);

    if (target >= 0) {
        struct install_disk_info disks[8];
        int count = install_scan(disks, 8);
        const char *model = "";
        u64 sectors = 0;
        for (int i = 0; i < count; i++) {
            if (disks[i].disk != target)
                continue;
            model = disks[i].model;
            sectors = disks[i].sectors;
        }
        ksnprintf(line, sizeof(line), "目标磁盘：disk %d · %s · %llu MiB",
                  target, model, (sectors * 512) >> 20);
        draw_text(line, x, y, 0x1C3A5A, 1);
    } else {
        draw_text("目标磁盘：未找到空白磁盘（数据盘与安装介质会被跳过）", x, y,
                  0x9A3342, 1);
    }
    y += ttf_line_height() + ui(6);

    draw_text("注意：安装会覆盖目标磁盘的分区表和数据。", x, y, 0x8A5A20, 1);
    y += ttf_line_height() + ui(4);
    draw_text("安装完成后可从该磁盘以 UEFI 或 Legacy BIOS 启动。", x, y,
              0x53687C, 1);

    paint_install_bar(win);

    if (g_install_phase == INSTALL_DONE)
        draw_text(g_install_message, x, win->y + win->h - ui(138), 0x2A6B3A,
                  1);
    else if (g_install_phase == INSTALL_FAILED)
        draw_text(g_install_message, x, win->y + win->h - ui(138), 0x9A3342,
                  1);
    else if (g_install_phase == INSTALL_IDLE)
        draw_text("准备就绪。点击下方按钮开始。", x,
                  win->y + win->h - ui(138), 0x51606F, 1);

    int bx, by, bw, bh;
    installer_button_rect(win, &bx, &by, &bw, &bh);
    u32 button_color = g_install_phase == INSTALL_RUNNING ? 0x9AA4AE : 0x2F7FD0;
    rounded_rect(bx, by, bw, bh, ui(10), button_color, 255);
    rounded_rect(bx + 2, by + 2, bw - 4, bh / 2, ui(9), 0xFFFFFF, 60);
    border(bx, by, bw, bh, 0x245E96);
    const char *label = g_install_phase == INSTALL_DONE
                            ? "重新安装 · Reinstall"
                            : g_install_phase == INSTALL_FAILED
                                  ? "重试 · Retry"
                                  : g_install_phase == INSTALL_RUNNING
                                        ? "安装中 · Installing…"
                                        : "开始安装 · Install";
    draw_text_center(label, bx + bw / 2, by + (bh - ttf_line_height()) / 2 + 1,
                     0xFFFFFF, 1);
}

void render_window(const window *win)
{
    if (!win->open)
        return;
    soft_shadow(win->x, win->y, win->w, win->h, ui(9));
    rounded_rect(win->x, win->y, win->w, win->h, ui(12), 0x4A6072, 255);
    rounded_rect(win->x + 1, win->y + 1, win->w - 2, win->h - 2, ui(11),
                 0xF4F7FA, 255);

    rect saved = g_clip;
    rect outer = {win->x + 2, win->y + 2, win->x + win->w - 2,
                  win->y + win->h - 2};
    g_clip = rect_clip(saved, outer);
    draw_titlebar(win);

    rect inner = {win->x + 2, win->y + g_title_h, win->x + win->w - 2,
                  win->y + win->h - 2};
    g_clip = rect_clip(saved, inner);

    switch (win->kind) {
    case KIND_WELCOME: render_welcome(win); break;
    case KIND_FILES: render_files(win); break;
    case KIND_TERMINAL: render_terminal(win); break;
    case KIND_CALC: render_calc(win); break;
    case KIND_NOTES: render_notes(win); break;
    case KIND_MONITOR: render_monitor(win); break;
    default: render_installer(win); break;
    }
    g_clip = saved;
}

/* ------------------------------------------------------------------ */
/* composition                                                         */
/* ------------------------------------------------------------------ */

void repaint()
{
    if (g_damage.count == 0)
        return;

    if (g_cursor_drawn)
        cursor_restore();

    for (int i = 0; i < g_damage.count; i++) {
        const rect &r = g_damage.items[i];
        set_clip(r);
        if (g_bg)
            blit_from_bg(r);

        /* menu bar */
        if (rect_intersects(r, {0, 0, (int)g_width, g_menu_h}))
            draw_menubar();

        /* dock */
        int dx, dy, dw, dh, spacing, icon;
        dock_metrics(&dx, &dy, &dw, &dh, &spacing, &icon);
        if (rect_intersects(r, {dx - 8, dy - ui(20), dx + dw + 8,
                                dy + dh + 8}))
            draw_dock();

        for (int z = 1; z <= g_next_z; z++) {
            for (int w = 0; w < kMaxWindows; w++) {
                const window *win = &g_windows[w];
                if (!win->open || win->z != z)
                    continue;
                rect wr;
                window_rect(win, &wr);
                wr.x0 -= ui(12);
                wr.y0 -= ui(12);
                wr.x1 += ui(12);
                wr.y1 += ui(12);
                if (rect_intersects(r, wr))
                    render_window(win);
            }
        }
    }

    present();
    damage_reset();
    draw_cursor();
}

/* ------------------------------------------------------------------ */
/* pointer                                                             */
/* ------------------------------------------------------------------ */

void cursor_restore()
{
    if (!g_cursor_drawn)
        return;
    int index = 0;
    for (int row = 0; row < kCursorH; row++) {
        int py = g_cursor_y + row;
        if (py < 0 || py >= (int)g_height)
            continue;
        for (int col = 0; col < kCursorW; col++) {
            int px = g_cursor_x + col;
            if (px < 0 || px >= (int)g_width)
                continue;
            g_fb[(u32)py * g_pitch_px + (u32)px] = g_cursor_save[index];
            index++;
        }
    }
    g_cursor_drawn = 0;
}

void draw_cursor()
{
    static const char *shape[kCursorH] = {
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

    int index = 0;
    for (int row = 0; row < kCursorH; row++) {
        int py = g_cursor_y + row;
        for (int col = 0; col < kCursorW; col++) {
            int px = g_cursor_x + col;
            if (px < 0 || py < 0 || px >= (int)g_width ||
                py >= (int)g_height)
                continue;
            u32 *p = g_fb + (u32)py * g_pitch_px + (u32)px;
            g_cursor_save[index++] = *p;
            char c = col < 19 ? shape[row][col] : ' ';
            if (c == 'X')
                *p = pack(0x061624);
            else if (c == 'o')
                *p = pack(0xFFFFFF);
        }
    }
    g_cursor_drawn = 1;
}

void cursor_move(int x, int y)
{
    if (x == g_cursor_x && y == g_cursor_y && g_cursor_drawn)
        return;
    if (g_cursor_drawn)
        cursor_restore();
    g_cursor_x = x;
    g_cursor_y = y;
    draw_cursor();
}

/* ------------------------------------------------------------------ */
/* hit testing and layout                                              */
/* ------------------------------------------------------------------ */

int window_index_at(int x, int y)
{
    int best = -1;
    int best_z = 0;
    for (int i = 0; i < kMaxWindows; i++) {
        const window *win = &g_windows[i];
        if (!win->open)
            continue;
        if (x < win->x || y < win->y || x >= win->x + win->w ||
            y >= win->y + win->h)
            continue;
        if (win->z > best_z) {
            best = i;
            best_z = win->z;
        }
    }
    return best;
}

bool dock_click(int x, int y)
{
    int dx, dy, dw, dh, spacing, icon;
    dock_metrics(&dx, &dy, &dw, &dh, &spacing, &icon);
    if (x < dx || y < dy || x >= dx + dw || y >= dy + dh)
        return false;

    int index = (x - dx - ui(12)) / spacing;
    if (index < 0 || index >= kMaxWindows)
        return false;

    window *win = &g_windows[index];
    win->open = true;
    win->z = ++g_next_z;
    g_focus = index;
    if (win->kind == KIND_FILES)
        load_files();
    if (win->kind == KIND_INSTALLER && g_install_phase == INSTALL_IDLE)
        g_install_pct = 0;

    /* full repaint: z-order and visibility changed */
    damage_all();
    repaint();
    return true;
}

void bring_to_front(int index)
{
    g_windows[index].z = ++g_next_z;
    if (g_next_z > 900) {
        for (int i = 0; i < kMaxWindows; i++)
            g_windows[i].z /= 2;
        g_next_z /= 2;
    }
}

int hovered_dock_icon(int x, int y)
{
    int dx, dy, dw, dh, spacing, icon;
    dock_metrics(&dx, &dy, &dw, &dh, &spacing, &icon);
    if (x < dx || y < dy - ui(20) || x >= dx + dw || y >= dy + dh)
        return -1;
    int index = (x - dx - ui(12)) / spacing;
    if (index < 0 || index >= kMaxWindows)
        return -1;
    return index;
}

void init_windows()
{
    struct layout {
        int x, y, w, h;
    };
    layout preset[kMaxWindows] = {
        { ui(110), ui(120), ui(700), ui(430) },   /* welcome */
        { ui(860), ui(140), ui(560), ui(500) },   /* files */
        { ui(150), ui(150), ui(900), ui(480) },   /* terminal */
        { ui(1080), ui(470), ui(380), ui(470) },  /* calculator */
        { ui(600), ui(160), ui(680), ui(500) },   /* notes */
        { ui(1000), ui(180), ui(560), ui(460) },  /* monitor */
        { ui(470), ui(150), ui(760), ui(520) },   /* installer */
    };
    static const char *titles[kMaxWindows] = {
        "欢迎 · Welcome", "文件 · Files", "终端 · Terminal",
        "计算器 · Calculator", "便签 · Notes", "监视器 · Monitor",
        "安装 · Installer"
    };

    for (int i = 0; i < kMaxWindows; i++) {
        window &win = g_windows[i];
        win.x = preset[i].x;
        win.y = preset[i].y;
        win.w = preset[i].w;
        win.h = preset[i].h;
        win.title = titles[i];
        win.kind = i;
        win.open = false;
        win.z = 0;
        if (win.x + win.w > (int)g_width - ui(12))
            win.x = (int)g_width - win.w - ui(12);
        if (win.y + win.h > (int)g_height - g_dock_h - ui(12))
            win.h = (int)g_height - g_dock_h - ui(12) - win.y;
        if (win.x < ui(8))
            win.x = ui(8);
        if (win.y < g_menu_h + ui(8))
            win.y = g_menu_h + ui(8);
        if (win.w < ui(300))
            win.w = ui(300);
        if (win.h < ui(220))
            win.h = ui(220);
    }

    g_windows[KIND_WELCOME].open = true;
    g_windows[KIND_WELCOME].z = 1;
    g_windows[KIND_FILES].open = true;
    g_windows[KIND_FILES].z = 2;
    g_next_z = 2;
    g_focus = KIND_WELCOME;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

void terminal_key(int key)
{
    if (key == '\n' || key == '\r') {
        terminal_submit();
        return;
    }
    if (key == 3) {                        /* Ctrl+C */
        g_term_input_len = 0;
        g_term_input[0] = 0;
        term_newline();
        damage_add(g_windows[KIND_TERMINAL].x, g_windows[KIND_TERMINAL].y,
                   g_windows[KIND_TERMINAL].w, g_windows[KIND_TERMINAL].h);
        return;
    }
    if (key == 12) {                       /* Ctrl+L */
        term_clear();
        g_term_input_len = 0;
        g_term_input[0] = 0;
        damage_add(g_windows[KIND_TERMINAL].x, g_windows[KIND_TERMINAL].y,
                   g_windows[KIND_TERMINAL].w, g_windows[KIND_TERMINAL].h);
        return;
    }
    if (key == '\b' || key == 0x7F) {
        if (g_term_input_len > 0)
            g_term_input[--g_term_input_len] = 0;
    } else if (key == 0x1B) {
        return;
    } else if (key >= 32 && key < 127) {
        if (g_term_input_len + 1 < kTermCols) {
            g_term_input[g_term_input_len++] = (char)key;
            g_term_input[g_term_input_len] = 0;
        }
    }
    damage_add(g_windows[KIND_TERMINAL].x, g_windows[KIND_TERMINAL].y,
               g_windows[KIND_TERMINAL].w, g_windows[KIND_TERMINAL].h);
}

void calculator_key(int key)
{
    static const char *keys = "789/456*123-0+C=";
    for (int i = 0; i < 16; i++) {
        if (key == keys[i] || (key == '\n' && keys[i] == '=')) {
            calc_press(i);
            return;
        }
    }
}

void handle_key(int key)
{
    if (g_focus < 0 || !g_windows[g_focus].open)
        return;
    if (g_windows[g_focus].kind == KIND_TERMINAL)
        terminal_key(key);
    else if (g_windows[g_focus].kind == KIND_NOTES)
        notes_key((char)key);
    else if (g_windows[g_focus].kind == KIND_CALC)
        calculator_key(key);
}

int g_drag_index = -1;
int g_drag_dx;
int g_drag_dy;

void pointer_press(int mx, int my)
{
    int index = window_index_at(mx, my);
    if (index < 0) {
        dock_click(mx, my);
        return;
    }

    window *win = &g_windows[index];
    if (win->kind == KIND_INSTALLER && g_install_phase != INSTALL_RUNNING &&
        installer_button_at(win, mx, my)) {
        start_install(win);
        damage_add(win->x, win->y, win->w, win->h);
        repaint();
        return;
    }
    if (close_button_at(win, mx, my)) {
        win->open = false;
        g_focus = -1;
        g_drag_index = -1;
        damage_all();
        repaint();
        return;
    }

    bring_to_front(index);
    g_focus = index;

    if (my < win->y + g_title_h) {
        g_drag_index = index;
        g_drag_dx = mx - win->x;
        g_drag_dy = my - win->y;
    } else if (win->kind == KIND_FILES) {
        int row = files_row_at(win, mx, my);
        if (row >= 0) {
            g_files_selected = row;
            damage_add(win->x, win->y, win->w, win->h);
            if (!g_files[row].is_dir)
                open_file_in_notes(&g_files[row]);
        }
    } else if (win->kind == KIND_CALC) {
        int button = calc_button_at(win, mx, my);
        if (button >= 0)
            calc_press(button);
    } else if (win->kind == KIND_TERMINAL) {
        damage_add(win->x, win->y, win->w, win->h);
    }

    damage_all();
    repaint();
}

void pointer_drag(int mx, int my)
{
    if (g_drag_index < 0)
        return;
    window *win = &g_windows[g_drag_index];
    int nx = mx - g_drag_dx;
    int ny = my - g_drag_dy;
    if (nx < -win->w + ui(90))
        nx = -win->w + ui(90);
    if (nx > (int)g_width - ui(90))
        nx = (int)g_width - ui(90);
    if (ny < g_menu_h)
        ny = g_menu_h;
    if (ny > (int)g_height - g_dock_h - ui(20))
        ny = (int)g_height - g_dock_h - ui(20);
    if (nx == win->x && ny == win->y)
        return;

    int pad = ui(14);
    damage_add(win->x - pad, win->y - pad, win->w + pad * 2,
               win->h + pad * 2);
    win->x = nx;
    win->y = ny;
    damage_add(win->x - pad, win->y - pad, win->w + pad * 2,
               win->h + pad * 2);
    repaint();
}

void pointer_release()
{
    g_drag_index = -1;
}

int parse_seconds(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *p = argv[i];
        if (*p < '0' || *p > '9')
            continue;
        int value = 0;
        while (*p >= '0' && *p <= '9')
            value = value * 10 + (*p++ - '0');
        if (*p == 0)
            return value;
    }
    return 0;
}

void serial_line(const char *s)
{
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc((u8)*s++);
    }
}

} // namespace

int desktop_run(int argc, char **argv)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot->framebuffer_base) {
        kprintf("[desk] no framebuffer, Desktop-Shell needs GOP\n");
        return -1;
    }

    g_fb = (u32 *)(uintptr_t)boot->framebuffer_base;
    g_pitch_px = boot->pitch / 4;
    g_width = boot->width;
    g_height = boot->height;
    int prev_font_px = ttf_pixel_size();
    ttf_set_pixel_size(g_height >= 1000 ? 24 : 20);
    g_format = boot->pixel_format;
    g_stride = (int)g_width;
    g_target = g_fb;

    build_round_table();

    u32 pixels = g_width * g_height;
    u32 pages = (pixels * 4 + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 back = vmm_alloc_pages(pages);
    u64 bg = vmm_alloc_pages(pages);
    g_back = (u32 *)(uintptr_t)back;
    g_bg = (u32 *)(uintptr_t)bg;
    g_back_valid = back != 0 && bg != 0;
    if (!g_back_valid) {
        g_back = nullptr;
        g_bg = nullptr;
        serial_line("[desk] back buffer unavailable, drawing directly\n");
    } else {
        g_target = g_back;
        for (u32 i = 0; i < pixels; i++)
            g_back[i] = 0;
        build_background();
    }

    damage_reset();
    g_cursor_x = -1;
    g_cursor_y = -1;
    g_cursor_drawn = 0;
    g_hover_icon = -1;
    g_drag_index = -1;
    g_install_win = nullptr;
    g_install_phase = INSTALL_IDLE;
    g_install_pct = 0;
    g_install_message[0] = 0;
    g_clock_last = 0xFFFFFFFFu;
    g_blink_last = timer_ticks();
    g_blink_on = true;

    g_menu_h = ui(30);
    g_title_h = ui(32);
    g_dock_h = ui(78);

    term_clear();
    g_term_input_len = 0;
    g_term_input[0] = 0;
    g_term_history_count = 0;
    g_term_history_pos = 0;
    term_append("Zhaoxu OS Desktop Terminal", 0xBFE4FF);
    term_newline();
    term_append("输入 help 查看命令；桌面应用直接运行在 Command-Shell 之上。",
                0x8FB6CC);
    term_newline();

    g_notes_len = 0;
    g_notes[0] = 0;
    strncpy(g_notes_name, "welcome.txt", sizeof(g_notes_name) - 1);
    g_notes_name[sizeof(g_notes_name) - 1] = 0;
    static const char note[] =
        "欢迎使用 Zhaoxu OS 便签！\n"
        "\n"
        "这是操作系统内部的原生文本应用。\n"
        "输入文字会保存在内存工作区。\n"
        "中文与英文都使用 TrueType 矢量字体。\n";
    for (usize i = 0; i < sizeof(note) - 1; i++)
        g_notes[i] = note[i];
    g_notes_len = (int)sizeof(note) - 1;
    g_notes[g_notes_len] = 0;

    g_calc_value = 0;
    g_calc_pending = 0;
    g_calc_op = 0;
    g_calc_fresh = false;
    calc_update_display();

    load_files();
    init_windows();

    int seconds = parse_seconds(argc, argv);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "install") == 0) {
            g_windows[KIND_INSTALLER].open = true;
            g_windows[KIND_INSTALLER].z = ++g_next_z;
            g_install_win = &g_windows[KIND_INSTALLER];
        } else if (strcmp(argv[i], "term") == 0) {
            g_windows[KIND_TERMINAL].open = true;
            g_windows[KIND_TERMINAL].z = ++g_next_z;
            g_focus = KIND_TERMINAL;
        } else if (strcmp(argv[i], "files") == 0) {
            g_windows[KIND_FILES].open = true;
            g_windows[KIND_FILES].z = ++g_next_z;
        } else if (strcmp(argv[i], "calc") == 0) {
            g_windows[KIND_CALC].open = true;
            g_windows[KIND_CALC].z = ++g_next_z;
        } else if (strcmp(argv[i], "notes") == 0) {
            g_windows[KIND_NOTES].open = true;
            g_windows[KIND_NOTES].z = ++g_next_z;
        } else if (strcmp(argv[i], "monitor") == 0) {
            g_windows[KIND_MONITOR].open = true;
            g_windows[KIND_MONITOR].z = ++g_next_z;
        }
    }

    u64 deadline = timer_ticks() + (u64)seconds * timer_hz();

    damage_all();
    repaint();
    cursor_move(mouse_x(), mouse_y());
    serial_line("[desk] Desktop-Shell active, ESC exits\n");
    if (ttf_available())
        serial_line("[desk] TrueType UI at 20 px\n");

    int prev_buttons = 0;
    int last_mouse_x = mouse_x();
    int last_mouse_y = mouse_y();

    for (;;) {
        int key = console_read_char();
        if (key == 27)
            break;
        if (key >= 0)
            handle_key(key);

        if (seconds > 0 && timer_ticks() >= deadline)
            break;

        int mx = mouse_x();
        int my = mouse_y();
        int buttons = mouse_buttons();
        bool pressed = (buttons & 1) && !(prev_buttons & 1);

        if (pressed)
            pointer_press(mx, my);
        if (g_drag_index >= 0 && (buttons & 1))
            pointer_drag(mx, my);
        if (!(buttons & 1) && g_drag_index >= 0)
            pointer_release();

        int hover = hovered_dock_icon(mx, my);
        if (hover != g_hover_icon) {
            g_hover_icon = hover;
            int dx, dy, dw, dh, spacing, icon;
            dock_metrics(&dx, &dy, &dw, &dh, &spacing, &icon);
            damage_add(dx - ui(10), dy - ui(24), dw + ui(20), dh + ui(34));
            repaint();
        }

        if (mx != last_mouse_x || my != last_mouse_y) {
            cursor_move(mx, my);
            last_mouse_x = mx;
            last_mouse_y = my;
        }

        /* clock: refresh only the right-hand strip once per second */
        u64 now = timer_ticks();
        if (now - g_blink_last >= (timer_hz() ? timer_hz() / 2 : 50)) {
            g_blink_last = now;
            g_blink_on = !g_blink_on;
            if (g_focus == KIND_TERMINAL) {
                damage_add(g_windows[KIND_TERMINAL].x,
                           g_windows[KIND_TERMINAL].y,
                           g_windows[KIND_TERMINAL].w,
                           g_windows[KIND_TERMINAL].h);
                repaint();
            } else if (g_focus == KIND_NOTES) {
                damage_add(g_windows[KIND_NOTES].x, g_windows[KIND_NOTES].y,
                           g_windows[KIND_NOTES].w, g_windows[KIND_NOTES].h);
                repaint();
            }
        }

        struct rtc_time rtc;
        static u64 last_clock_poll;
        if (now - last_clock_poll >= (timer_hz() ? timer_hz() / 5 : 20)) {
            last_clock_poll = now;
            if (rtc_read(&rtc) == 0) {
                u32 seconds_now = (u32)rtc.hour * 3600 +
                                  (u32)rtc.minute * 60 + (u32)rtc.second;
                if (seconds_now != g_clock_last) {
                    g_clock_last = seconds_now;
                    damage_add((int)g_width - ui(170), 0, ui(170), g_menu_h);
                    if (g_windows[KIND_MONITOR].open)
                        damage_add(g_windows[KIND_MONITOR].x,
                                   g_windows[KIND_MONITOR].y,
                                   g_windows[KIND_MONITOR].w,
                                   g_windows[KIND_MONITOR].h);
                    repaint();
                }
            }
        }

        if (g_damage.count > 0 && g_drag_index < 0) {
            /* terminal submit / file open paths already repaint; this
             * covers deferred damage from installer callbacks */
            repaint();
        }

        prev_buttons = buttons;
        timer_sleep_ms(1);
    }

    if (g_cursor_drawn)
        cursor_restore();
    ttf_set_pixel_size(prev_font_px);
    console_clear();
    kprintf("Desktop-Shell closed, Command-Shell restored.\n");
    serial_line("[desk] Desktop-Shell closed\n");
    return 0;
}
