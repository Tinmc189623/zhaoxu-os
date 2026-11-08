#include <boot.h>
#include <console.h>
#include <drivers.h>
#include <keyboard.h>
#include <spinlock.h>
#include <string.h>
#include <ttf.h>
#include <types.h>

#define GLYPH_WIDTH  8
#define GLYPH_HEIGHT 16

static u64 fb_base;
static u32 fb_pitch;
static u32 fb_width;
static u32 fb_height;
static u32 fb_format;
static int have_fb;
static u32 cursor_x;
static u32 cursor_y;
static u32 columns = 80;
static u32 rows = 25;
static enum console_color fg_index = COLOR_LIGHT_GRAY;
static enum console_color bg_index = COLOR_BLACK;
static u32 fg_pixel;
static u32 bg_pixel;
static int ttf_mode;
static void (*output_sink)(char c);
static spinlock_t console_lock = SPINLOCK_INIT;

extern char font8x8_basic[128][8];

static const u32 palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static u32 pack_color(u32 rgb)
{
    if (fb_format == 0) {
        /* EFI_PIXEL_RED_GREEN_BLUE_RESERVED_8_BIT: bytes R,G,B,X */
        return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
    }
    /* BGRX (QEMU default) and fallback: bytes B,G,R,X */
    return rgb & 0xFFFFFF;
}

static void serial_raw(const char *s)
{
    while (*s)
        serial_putc((u8)*s++);
}

static int append_decimal(char *buf, int n, u32 value)
{
    char digits[4];
    int count = 0;
    if (value == 0) {
        buf[n++] = '0';
        return n;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count > 0)
        buf[n++] = digits[--count];
    return n;
}

static void serial_ansi_color(enum console_color fg, enum console_color bg)
{
    /* Map the VGA palette to the ANSI SGR colour indices. */
    static const u8 fg_codes[16] = {
        30, 34, 32, 36, 31, 35, 33, 37,
        90, 94, 92, 96, 91, 95, 93, 97
    };
    static const u8 bg_codes[16] = {
        40, 44, 42, 46, 41, 45, 43, 47,
        100, 104, 102, 106, 101, 105, 103, 107
    };
    char buf[24];
    int n = 0;
    buf[n++] = 27;
    buf[n++] = '[';
    n = append_decimal(buf, n, fg_codes[fg & 15]);
    buf[n++] = ';';
    n = append_decimal(buf, n, bg_codes[bg & 15]);
    buf[n++] = 'm';
    buf[n] = 0;
    serial_raw(buf);
}

void console_set_color(enum console_color fg, enum console_color bg)
{
    fg_index = fg;
    bg_index = bg;
    fg_pixel = pack_color(palette[fg & 15]);
    bg_pixel = pack_color(palette[bg & 15]);
    serial_ansi_color(fg, bg);
}

void console_reset_color(void)
{
    console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
}

void console_set_sink(void (*sink)(char c))
{
    output_sink = sink;
}

void console_init(const struct boot_info *bi)
{
    serial_init(115200);
    fb_base = bi ? bi->framebuffer_base : 0;
    fb_pitch = bi ? bi->pitch : 0;
    fb_width = bi ? bi->width : 0;
    fb_height = bi ? bi->height : 0;
    fb_format = bi ? bi->pixel_format : 1;
    have_fb = fb_base != 0 && fb_width >= GLYPH_WIDTH && fb_height >= GLYPH_HEIGHT;

    /* 1080p panels get a larger em so the console keeps the 800p feel */
    ttf_set_pixel_size(fb_height >= 1000 ? 24 : 20);

    cursor_x = 0;
    cursor_y = 0;
    fg_index = COLOR_LIGHT_GRAY;
    bg_index = COLOR_BLACK;
    fg_pixel = pack_color(palette[fg_index]);
    bg_pixel = pack_color(palette[bg_index]);
    ttf_mode = 0;

    if (have_fb) {
        columns = fb_width / GLYPH_WIDTH;
        rows = fb_height / GLYPH_HEIGHT;
        gop_clear(fb_base, fb_pitch, fb_width, fb_height, bg_pixel);
    } else {
        columns = 80;
        rows = 25;
    }
    serial_raw("\x1b[0m");
}

/* The font arrives after the first boot messages, so the console starts in
 * fixed-cell bitmap mode and switches to proportional TrueType text once
 * ttf_autoload() succeeds.  Cursor coordinates are kept in the units of the
 * active mode. */
static void sync_font_mode(void)
{
    int want = ttf_available() && have_fb;
    if (want == ttf_mode)
        return;
    if (want) {
        cursor_x *= GLYPH_WIDTH;
        cursor_y *= GLYPH_HEIGHT;
    } else {
        cursor_x /= GLYPH_WIDTH;
        cursor_y /= GLYPH_HEIGHT;
    }
    ttf_mode = want;
}

void console_clear(void)
{
    if (output_sink)
        return;
    if (have_fb)
        gop_clear(fb_base, fb_pitch, fb_width, fb_height, bg_pixel);
    serial_raw("\x1b[2J\x1b[H");
    cursor_x = 0;
    cursor_y = 0;
}

static void draw_cell(u32 column, u32 row, char c)
{
    if (!have_fb)
        return;
    u8 ch = (u8)c;
    if (ch >= 128)
        ch = '?';
    gop_blit_glyph(fb_base, fb_pitch, column * GLYPH_WIDTH, row * GLYPH_HEIGHT,
                   (const u8 *)font8x8_basic[ch], fg_pixel, bg_pixel);
}

static void scroll_bitmap(void)
{
    if (!have_fb)
        return;
    u32 pixel_height = (rows - 1) * GLYPH_HEIGHT;
    if (pixel_height > 0)
        gop_copy_rows(fb_base, fb_pitch, GLYPH_HEIGHT, 0, pixel_height,
                      fb_pitch);
    gop_fill_rect(fb_base, fb_pitch, 0, (rows - 1) * GLYPH_HEIGHT,
                  fb_width, GLYPH_HEIGHT, bg_pixel);
}

static void newline_bitmap(void)
{
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= rows) {
        scroll_bitmap();
        cursor_y = rows - 1;
    }
}

static void putchar_bitmap(char c)
{
    if (c == '\n') {
        newline_bitmap();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            draw_cell(cursor_x, cursor_y, ' ');
        }
        return;
    }
    if (c == '\t') {
        u32 target = (cursor_x + 4) & ~3u;
        while (cursor_x < target)
            putchar_bitmap(' ');
        return;
    }
    if ((u8)c < 32)
        return;

    draw_cell(cursor_x, cursor_y, c);
    cursor_x++;
    if (cursor_x >= columns)
        newline_bitmap();
}

/* ------------------------------------------------------------------ */
/* TrueType console                                                    */
/* ------------------------------------------------------------------ */

#define TTF_LINE_HISTORY 256
static u32 line_starts[TTF_LINE_HISTORY];
static u32 line_glyphs;

static void scroll_ttf(void)
{
    int lh = ttf_line_height();
    if (!have_fb || (int)fb_height <= lh)
        return;
    gop_copy_rows(fb_base, fb_pitch, lh, 0, fb_height - lh, fb_pitch);
    gop_fill_rect(fb_base, fb_pitch, 0, fb_height - lh,
                  fb_width, lh, bg_pixel);
}

static void newline_ttf(void)
{
    cursor_x = 0;
    cursor_y += ttf_line_height();
    if (cursor_y + ttf_line_height() > fb_height) {
        scroll_ttf();
        cursor_y = fb_height - ttf_line_height();
    }
    line_glyphs = 0;
}

static void blit_bitmap_at(u32 x, u32 y, u8 ch)
{
    if (ch >= 128)
        ch = '?';
    gop_blit_glyph(fb_base, fb_pitch, x, y, (const u8 *)font8x8_basic[ch],
                   fg_pixel, bg_pixel);
}

static void put_codepoint_ttf(u32 codepoint)
{
    struct ttf_glyph glyph;
    int status = ttf_render(codepoint, &glyph);

    if (status != 0 || (glyph.w == 0 && glyph.adv == 0)) {
        /* No glyph: keep ASCII readable through the bitmap font and
         * substitute '?' for anything else. */
        u32 advance = GLYPH_WIDTH;
        if (cursor_x + advance > fb_width)
            newline_ttf();
        if (codepoint < 0x80) {
            blit_bitmap_at(cursor_x, cursor_y, (u8)codepoint);
        } else {
            gop_fill_rect(fb_base, fb_pitch, cursor_x, cursor_y, advance,
                          ttf_line_height(), bg_pixel);
            blit_bitmap_at(cursor_x, cursor_y, '?');
        }
        if (line_glyphs < TTF_LINE_HISTORY)
            line_starts[line_glyphs++] = cursor_x;
        cursor_x += advance;
        if (cursor_x >= fb_width)
            newline_ttf();
        return;
    }

    u32 advance = glyph.adv ? glyph.adv : (glyph.w ? glyph.w : GLYPH_WIDTH);
    if (cursor_x + advance > fb_width)
        newline_ttf();

    gop_fill_rect(fb_base, fb_pitch, cursor_x, cursor_y, advance,
                  ttf_line_height(), bg_pixel);

    if (glyph.w && glyph.h) {
        i32 shift = advance > glyph.w ? (i32)(advance - glyph.w) / 2 : 0;
        i32 gx = (i32)cursor_x + shift + glyph.bx;
        i32 gy = (i32)cursor_y + glyph.by;
        i32 x0 = gx > 0 ? gx : 0;
        i32 y0 = gy > 0 ? gy : 0;
        i32 x1 = gx + (i32)glyph.w;
        if (x1 > (i32)fb_width)
            x1 = (i32)fb_width;
        i32 y1 = gy + (i32)glyph.h;
        if (y1 > (i32)fb_height)
            y1 = (i32)fb_height;
        if (x1 > x0 && y1 > y0)
            gop_blit_alpha(fb_base, fb_pitch, (u32)x0, (u32)y0, glyph.alpha,
                           glyph.w, (u32)(x0 - gx), (u32)(y0 - gy),
                           (u32)(x1 - x0), (u32)(y1 - y0), fg_pixel, bg_pixel);
    }

    if (line_glyphs < TTF_LINE_HISTORY)
        line_starts[line_glyphs++] = cursor_x;
    cursor_x += advance;
    if (cursor_x >= fb_width)
        newline_ttf();
}

static void backspace_ttf(void)
{
    if (line_glyphs == 0)
        return;
    line_glyphs--;
    u32 target = line_starts[line_glyphs];
    if (cursor_x > target)
        gop_fill_rect(fb_base, fb_pitch, target, cursor_y, cursor_x - target,
                      ttf_line_height(), bg_pixel);
    cursor_x = target;
}

static void putchar_ttf(char c)
{
    if (c == '\n') {
        newline_ttf();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        line_glyphs = 0;
        return;
    }
    if (c == '\b') {
        backspace_ttf();
        return;
    }
    if (c == '\t') {
        u32 advance = GLYPH_WIDTH * 4;
        u32 target = (cursor_x + advance) / advance * advance;
        while (cursor_x < target)
            put_codepoint_ttf(' ');
        return;
    }
    if ((u8)c < 32)
        return;
    put_codepoint_ttf((u8)c);
}

static u8 utf8_left;    /* continuation bytes still missing */
static u32 utf8_cp;

static void draw_codepoint(u32 codepoint)
{
    if (ttf_mode)
        put_codepoint_ttf(codepoint);
    else
        putchar_bitmap('?');
}

static void putchar_screen(u8 byte)
{
    if (!have_fb)
        return;

    sync_font_mode();

    if (utf8_left) {
        if ((byte & 0xC0) == 0x80) {
            utf8_cp = (utf8_cp << 6) | (byte & 0x3F);
            if (--utf8_left == 0) {
                draw_codepoint(utf8_cp);
                return;
            }
            return;
        }
        utf8_left = 0;
        draw_codepoint('?');
    }
    if (byte < 0x80) {
        if (ttf_mode)
            putchar_ttf((char)byte);
        else
            putchar_bitmap((char)byte);
        return;
    }
    if ((byte & 0xE0) == 0xC0) {
        utf8_cp = byte & 0x1F;
        utf8_left = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        utf8_cp = byte & 0x0F;
        utf8_left = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        utf8_cp = byte & 0x07;
        utf8_left = 3;
    }
}

void console_putc(char c)
{
    if (output_sink) {
        output_sink(c);
        return;
    }
    if (c == '\n')
        serial_putc('\r');
    serial_putc((u8)c);
    putchar_screen((u8)c);
}

void console_write(const char *s)
{
    u64 flags = spin_lock_irqsave(&console_lock);
    while (*s)
        console_putc(*s++);
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_write_n(const char *s, usize length)
{
    u64 flags = spin_lock_irqsave(&console_lock);
    for (usize i = 0; i < length; i++)
        console_putc(s[i]);
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_backspace(void)
{
    console_putc('\b');
}

u32 console_columns(void)
{
    if (ttf_mode && have_fb)
        return fb_width / (ttf_pixel_size() / 2);
    return columns;
}

u32 console_rows(void)
{
    if (ttf_mode && have_fb)
        return fb_height / ttf_line_height();
    return rows;
}

int console_has_framebuffer(void)
{
    return have_fb;
}

int console_read_char(void)
{
    int c = keyboard_getchar();
    if (c >= 0)
        return c;
    c = serial_getc();
    if (c < 0)
        return -1;
    if (c == '\r')
        c = '\n';
    return c;
}

int console_has_input(void)
{
    return keyboard_has_input();
}
