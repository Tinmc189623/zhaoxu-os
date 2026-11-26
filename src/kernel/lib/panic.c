/*
 * Zhaoxu OS - red panic screen.
 *
 * A fatal kernel exception paints the whole display blood red and prints the
 * report, in the spirit of the Windows blue screen.  The field set follows
 * the BSD panic convention (vector, error code, faulting instruction, CR2).
 *
 * The kernel console has no framebuffer sink this early in boot (the Ring3
 * display service owns it), so this file draws directly into the Limine
 * framebuffer with the public-domain 8x8 font.  The KERNEL PANIC banner is
 * the art published in the repository's ascii.md; the kernel does not invent
 * its own fatal-screen glyphs.
 */
#include <boot.h>
#include <console.h>
#include <io.h>
#include <kprintf.h>
#include <types.h>

#include "../font/font8x8_basic.h"

typedef struct {
    u8 *base;
    u32 width;
    u32 height;
    u32 pitch;
    u32 format;
} panic_fb;

static panic_fb g_fb;
static int g_fb_ready;

/* Direct COM1 probe: the display service is not draining the log ring yet,
 * so the panic path reports its progress on the serial port the same way the
 * boot manager does. */
static void raw_serial_char(char c)
{
    while ((inb(0x3F8 + 5) & 0x20) == 0)
        ;
    outb(0x3F8, (u8)c);
}

static void raw_serial(const char *text)
{
    while (*text)
        raw_serial_char(*text++);
}

static void raw_serial_hex(u64 value)
{
    const char *digits = "0123456789abcdef";
    raw_serial("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        raw_serial_char(digits[(value >> shift) & 0xF]);
}

/* The KERNEL PANIC banner from ascii.md: KERNEL over PANIC. */
static const char *const panic_banner[] = {
    "##    ## ######## ########  ##    ## ######## ##",
    "##   ##  ##       ##     ## ###   ## ##       ##",
    "##  ##   ##       ##     ## ####  ## ##       ##",
    "#####    ######   ########  ## ## ## ######   ##",
    "##  ##   ##       ##   ##   ##  #### ##       ##",
    "##   ##  ##       ##    ##  ##   ### ##       ##",
    "##    ## ######## ##     ## ##    ## ######## ########",
    "########     ###    ##    ## ####  ######",
    "##     ##   ## ##   ###   ##  ##  ##    ##",
    "##     ##  ##   ##  ####  ##  ##  ##",
    "########  ##     ## ## ## ##  ##  ##",
    "##        ######### ##  ####  ##  ##",
    "##        ##     ## ##   ###  ##  ##    ##",
    "##        ##     ## ##    ## ####  ######",
};

#define PANIC_BANNER_LINES \
    (sizeof(panic_banner) / sizeof(panic_banner[0]))

/* 0x00RRGGBB shifted into the framebuffer's native u32 order. */
static u32 fb_colour(u32 rgb)
{
    u32 red = (rgb >> 16) & 0xFF;
    u32 green = (rgb >> 8) & 0xFF;
    u32 blue = rgb & 0xFF;
    if (g_fb.format == 0)  /* RGB bytes in memory: X,B,G,R as little-endian */
        return 0xFF000000u | (blue << 16) | (green << 8) | red;
    /* BGR bytes in memory: X,R,G,B as little-endian */
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

static void fb_pixel(u32 x, u32 y, u32 colour)
{
    if (x >= g_fb.width || y >= g_fb.height)
        return;
    *(u32 *)(g_fb.base + (usize)y * g_fb.pitch + (usize)x * 4) = colour;
}

static void fb_fill(u32 colour)
{
    for (u32 y = 0; y < g_fb.height; y++) {
        u32 *row = (u32 *)(g_fb.base + (usize)y * g_fb.pitch);
        for (u32 x = 0; x < g_fb.width; x++)
            row[x] = colour;
    }
}

/* One glyph at pixel (x, y); bit 0 of each row is the leftmost pixel. */
static void fb_glyph(u32 x, u32 y, char c, u32 scale, u32 colour)
{
    const char *glyph = font8x8_basic[(unsigned char)c & 0x7F];
    for (u32 row = 0; row < 8; row++) {
        unsigned char bits = (unsigned char)glyph[row];
        for (u32 col = 0; col < 8; col++) {
            if (!(bits & (1u << col)))
                continue;
            for (u32 dy = 0; dy < scale; dy++)
                for (u32 dx = 0; dx < scale; dx++)
                    fb_pixel(x + col * scale + dx, y + row * scale + dy,
                             colour);
        }
    }
}

static usize text_length(const char *text)
{
    usize length = 0;
    while (text[length])
        length++;
    return length;
}

/* One text line on the 80x25 grid, centred horizontally. */
static void fb_line(u32 row, const char *text, u32 scale, u32 colour)
{
    u32 cell = 8 * scale;
    usize length = text_length(text);
    u32 width = g_fb.width;
    u32 text_width = (u32)length * cell;
    u32 x = text_width < width ? (width - text_width) / 2 : 0;
    u32 y = 8 * scale + row * cell;
    for (usize i = 0; i < length; i++)
        fb_glyph(x + (u32)i * cell, y, text[i], scale, colour);
}

/* Small string builders: the kernel printf cannot reach the screen before
 * the display service runs, so the report is composed by hand. */
static usize put_text(char *buffer, usize used, const char *text)
{
    while (*text) {
        buffer[used++] = *text++;
        buffer[used] = 0;
    }
    return used;
}

static usize put_decimal(char *buffer, usize used, u64 value)
{
    char digits[20];
    usize count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        buffer[used++] = digits[--count];
        buffer[used] = 0;
    }
    return used;
}

static usize put_hex64(char *buffer, usize used, u64 value)
{
    const char *digits = "0123456789abcdef";
    buffer[used++] = '0';
    buffer[used++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4)
        buffer[used++] = digits[(value >> shift) & 0xF];
    buffer[used] = 0;
    return used;
}

/* Attach the Limine framebuffer if the machine has one. */
static int panic_fb_init(void)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot || boot->magic != BOOT_MAGIC || boot->framebuffer_base == 0 ||
        boot->width == 0 || boot->height == 0 || boot->pitch < boot->width * 4)
        return 0;
    g_fb.base = (u8 *)(usize)boot->framebuffer_base;
    g_fb.width = boot->width;
    g_fb.height = boot->height;
    g_fb.pitch = boot->pitch;
    g_fb.format = boot->pixel_format;
    g_fb_ready = 1;
    return 1;
}

/* Red screen with the exception report; never returns. */
void panic_red_screen(u32 vector, const char *name, u64 error, u64 rip,
                      u64 cr2, int page_fault)
{
    const u32 red = 0xC00000u;
    const u32 white = 0xFFFFFFu;
    const u32 light = 0xFFD0D0u;

    raw_serial("\n[panic] vector=");
    raw_serial_hex(vector);
    raw_serial(" name=");
    raw_serial(name ? name : "Unknown");
    raw_serial(" error=");
    raw_serial_hex(error);
    raw_serial(" rip=");
    raw_serial_hex(rip);
    raw_serial(" cr2=");
    raw_serial_hex(cr2);
    raw_serial("\n");

    if (!panic_fb_init()) {
        raw_serial("[panic] no framebuffer; screen output unavailable\n");
    } else {
        fb_fill(fb_colour(red));
        u32 scale_x = g_fb.width / (80 * 8);
        u32 scale_y = g_fb.height / (30 * 8);
        u32 scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale < 2)
            scale = 2;
        if (scale > 4)
            scale = 4;

        u32 row = 0;
        fb_line(row++, "=====  S T O P  =====", scale, white);
        row++;
        for (usize i = 0; i < PANIC_BANNER_LINES; i++)
            fb_line(row++, panic_banner[i], scale, white);
        row++;
        fb_line(row++, "A FATAL ERROR HAS DESTROYED THIS MACHINE.", scale,
                light);
        fb_line(row++, "THE SYSTEM HAS BEEN HALTED TO PROTECT YOUR DATA.",
                scale, light);
        row++;

        char report[96];
        usize used = 0;
        report[0] = 0;
        used = put_text(report, used, "EXCEPTION : ");
        used = put_decimal(report, used, vector);
        used = put_text(report, used, " ");
        used = put_text(report, used, name ? name : "Unknown");
        fb_line(row++, report, scale, white);

        used = 0;
        report[0] = 0;
        used = put_text(report, used, "ERROR CODE: ");
        used = put_hex64(report, used, error);
        fb_line(row++, report, scale, white);

        used = 0;
        report[0] = 0;
        used = put_text(report, used, "RIP       : ");
        used = put_hex64(report, used, rip);
        fb_line(row++, report, scale, white);

        if (page_fault) {
            used = 0;
            report[0] = 0;
            used = put_text(report, used, "CR2       : ");
            used = put_hex64(report, used, cr2);
            fb_line(row++, report, scale, white);
        }
        row++;
        fb_line(row++, "Restart the machine.  If this screen returns, the",
                scale, light);
        fb_line(row++, "kernel is damaged and must be reinstalled.", scale,
                light);
    }

    /* Keep the plain-text report in the log ring as well, for a serial line
     * or the Ring3 display service if it is already draining it.  The raw
     * serial line above is the copy that survives a pre-driver panic. */
    kprintf("[panic] vector %u %s error 0x%llx rip 0x%llx cr2 0x%llx\n",
            vector, name ? name : "Unknown", error, rip, cr2);

    for (;;)
        __asm__ volatile("cli; hlt");
}
