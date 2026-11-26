/*
 * Zhaoxu OS - 80x25 framebuffer text console.
 *
 * The setup disc runs the text installer without a display service, and the
 * kernel console has no screen sink by default.  This console gives the
 * installer a real screen: an 80x25 grid of 16x32 cells centred on the
 * Limine framebuffer, VGA text colours, scrolling, and the usual control
 * characters.  It is presentation code only; the desktop boot never attaches
 * it because Aqua owns the screen there.
 */
#include <fbcon.h>
#include <fbdraw.h>
#include <string.h>
#include <types.h>

#define FBCON_COLS 80
#define FBCON_ROWS 25
#define FBCON_CELL_W 16
#define FBCON_CELL_H 32
#define FBCON_SCALE 2

static struct fb_draw g_fb;
static int g_ready;
static u32 g_cols;
static u32 g_rows;
static u32 g_origin_x;
static u32 g_origin_y;
static u32 g_col;
static u32 g_row;
static u32 g_cell_w;
static u32 g_cell_h;
static u32 g_scale;      /* glyph scale that fits the current cell */
static u32 g_foreground;
static u32 g_background;

/* VGA text palette (0xRRGGBB). */
static const u32 g_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static void fbcon_sync(void)
{
    g_foreground = g_palette[COLOR_LIGHT_GRAY];
    g_background = g_palette[COLOR_BLUE];
}

/* Paint one cell background and, when non-blank, its glyph. */
static void paint_cell(u32 col, u32 row, char c)
{
    u32 x = g_origin_x + col * g_cell_w;
    u32 y = g_origin_y + row * g_cell_h;
    fb_draw_rect(&g_fb, x, y, g_cell_w, g_cell_h, g_background);
    if (c != ' ' && c != 0)
        fb_draw_glyph(&g_fb, x + (g_cell_w - 8 * g_scale) / 2,
                      y + (g_cell_h - 8 * g_scale) / 2, c, g_scale,
                      g_foreground);
}

/* Blank the text grid and park the cursor at the top-left cell. */
void fbcon_clear(void)
{
    if (!g_ready)
        return;
    fb_draw_rect(&g_fb, g_origin_x, g_origin_y, g_cols * g_cell_w,
                 g_rows * g_cell_h, g_background);
    g_col = 0;
    g_row = 0;
}

/* Scroll the text area up by one cell and blank the new bottom row. */
static void scroll_up(void)
{
    usize stride = g_fb.pitch;
    usize row_bytes = (usize)g_cols * g_cell_w * 4;
    u8 *area = g_fb.base + (usize)g_origin_y * stride +
               (usize)g_origin_x * 4;
    u32 lines = g_rows * g_cell_h;

    for (u32 line = 0; line + g_cell_h < lines; line++)
        memcpy(area + (usize)line * stride,
               area + (usize)(line + g_cell_h) * stride, row_bytes);
    fb_draw_rect(&g_fb, g_origin_x, g_origin_y + (g_rows - 1) * g_cell_h,
                 g_cols * g_cell_w, g_cell_h, g_background);
}

/* Advance to the next line, scrolling when the grid is full. */
static void newline(void)
{
    g_col = 0;
    if (g_row + 1 < g_rows) {
        g_row++;
        return;
    }
    scroll_up();
}

void fbcon_putc(char c)
{
    if (!g_ready)
        return;
    if (c == '\r') {
        g_col = 0;
        return;
    }
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\b') {
        if (g_col > 0) {
            g_col--;
            paint_cell(g_col, g_row, ' ');
        }
        return;
    }
    if (c == '\t') {
        for (int i = 0; i < 4 && g_col < g_cols; i++)
            fbcon_putc(' ');
        return;
    }
    if ((unsigned char)c < 32 || (unsigned char)c > 126)
        c = ' ';
    if (g_col >= g_cols)
        newline();
    paint_cell(g_col, g_row, c);
    g_col++;
}

void fbcon_set_color(enum console_color fg, enum console_color bg)
{
    if (!g_ready)
        return;
    g_foreground = g_palette[fg & 0xF];
    g_background = g_palette[bg & 0xF];
}

int fbcon_init(const struct boot_info *boot)
{
    if (!fb_draw_attach(&g_fb, boot))
        return 0;

    g_cols = FBCON_COLS;
    g_rows = FBCON_ROWS;
    /* Fit the grid while keeping the 2x glyphs: shrink cells on small
     * screens instead of clipping the installer's 80 columns. */
    g_cell_w = FBCON_CELL_W;
    g_cell_h = FBCON_CELL_H;
    while (g_cols * g_cell_w > g_fb.width || g_rows * g_cell_h > g_fb.height) {
        if (g_cell_w <= 8 || g_cell_h <= 16)
            break;
        g_cell_w /= 2;
        g_cell_h /= 2;
    }
    if (g_cols * g_cell_w > g_fb.width || g_rows * g_cell_h > g_fb.height)
        return 0;
    g_scale = g_cell_w / 8;
    if (g_scale < 1)
        g_scale = 1;

    g_origin_x = (g_fb.width - g_cols * g_cell_w) / 2;
    g_origin_y = (g_fb.height - g_rows * g_cell_h) / 2;
    g_ready = 1;
    fbcon_sync();
    fb_draw_clear(&g_fb, g_palette[COLOR_BLACK]);
    fbcon_clear();
    return 1;
}
