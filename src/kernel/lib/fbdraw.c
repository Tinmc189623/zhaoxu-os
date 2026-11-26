/*
 * Zhaoxu OS - direct framebuffer painting.
 *
 * The kernel owns no framebuffer console: until the Ring3 display service
 * runs, the only screen the kernel can touch is the Limine framebuffer.
 * This file provides the few primitives the boot splash and the panic screen
 * need (pixel, rectangle, gradient, 8x8 text).  It is presentation code, not
 * a driver: there is no hardware access beyond the mapped buffer.
 */
#include <fbdraw.h>
#include <string.h>
#include <types.h>

/* The public-domain 8x8 table is defined in panic.c; including its header
 * here would emit a second definition. */
extern char font8x8_basic[128][8];

int fb_draw_attach(struct fb_draw *draw, const struct boot_info *boot)
{
    if (!draw || !boot || boot->magic != BOOT_MAGIC ||
        boot->framebuffer_base == 0 || boot->width == 0 || boot->height == 0 ||
        boot->pitch < boot->width * 4)
        return 0;
    draw->base = (u8 *)(usize)boot->framebuffer_base;
    draw->width = boot->width;
    draw->height = boot->height;
    draw->pitch = boot->pitch;
    draw->format = boot->pixel_format;
    return 1;
}

u32 fb_draw_colour(const struct fb_draw *draw, u32 rgb)
{
    u32 red = (rgb >> 16) & 0xFF;
    u32 green = (rgb >> 8) & 0xFF;
    u32 blue = rgb & 0xFF;
    if (draw->format == 0)  /* RGB bytes in memory: X,B,G,R little-endian */
        return 0xFF000000u | (blue << 16) | (green << 8) | red;
    /* BGR bytes in memory: X,R,G,B little-endian */
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

void fb_draw_pixel(struct fb_draw *draw, u32 x, u32 y, u32 rgb)
{
    if (!draw || !draw->base || x >= draw->width || y >= draw->height)
        return;
    *(u32 *)(draw->base + (usize)y * draw->pitch + (usize)x * 4) =
        fb_draw_colour(draw, rgb);
}

void fb_draw_rect(struct fb_draw *draw, u32 x, u32 y, u32 width, u32 height,
                  u32 rgb)
{
    if (!draw || !draw->base || width == 0 || height == 0)
        return;
    if (x >= draw->width || y >= draw->height)
        return;
    if (x + width > draw->width)
        width = draw->width - x;
    if (y + height > draw->height)
        height = draw->height - y;

    u32 colour = fb_draw_colour(draw, rgb);
    for (u32 row = 0; row < height; row++) {
        u32 *line = (u32 *)(draw->base + (usize)(y + row) * draw->pitch);
        for (u32 col = 0; col < width; col++)
            line[x + col] = colour;
    }
}

void fb_draw_clear(struct fb_draw *draw, u32 rgb)
{
    fb_draw_rect(draw, 0, 0, draw ? draw->width : 0,
                 draw ? draw->height : 0, rgb);
}

void fb_draw_gradient(struct fb_draw *draw, u32 top_rgb, u32 bottom_rgb)
{
    if (!draw || !draw->base || draw->height == 0)
        return;
    for (u32 y = 0; y < draw->height; y++) {
        u32 mix = draw->height > 1 ? y * 255u / (draw->height - 1) : 0;
        u32 red = ((top_rgb >> 16) & 0xFF) * (255 - mix) / 255 +
                  ((bottom_rgb >> 16) & 0xFF) * mix / 255;
        u32 green = ((top_rgb >> 8) & 0xFF) * (255 - mix) / 255 +
                    ((bottom_rgb >> 8) & 0xFF) * mix / 255;
        u32 blue = (top_rgb & 0xFF) * (255 - mix) / 255 +
                   (bottom_rgb & 0xFF) * mix / 255;
        u32 colour = fb_draw_colour(draw, (red << 16) | (green << 8) | blue);
        u32 *line = (u32 *)(draw->base + (usize)y * draw->pitch);
        for (u32 x = 0; x < draw->width; x++)
            line[x] = colour;
    }
}

void fb_draw_glyph(struct fb_draw *draw, u32 x, u32 y, char c, u32 scale,
                   u32 rgb)
{
    if (!draw || scale == 0)
        return;
    const char *glyph = font8x8_basic[(unsigned char)c & 0x7F];
    for (u32 row = 0; row < 8; row++) {
        unsigned char bits = (unsigned char)glyph[row];
        for (u32 col = 0; col < 8; col++) {
            if (!(bits & (1u << col)))
                continue;
            fb_draw_rect(draw, x + col * scale, y + row * scale, scale,
                         scale, rgb);
        }
    }
}

u32 fb_draw_text_width(const char *text, u32 scale)
{
    if (!text)
        return 0;
    return (u32)strlen(text) * 8 * scale;
}

void fb_draw_text(struct fb_draw *draw, u32 x, u32 y, const char *text,
                  u32 scale, u32 rgb)
{
    if (!draw || !text || scale == 0)
        return;
    for (usize index = 0; text[index]; index++) {
        fb_draw_glyph(draw, x, y, text[index], scale, rgb);
        x += 8 * scale;
        if (x >= draw->width)
            break;
    }
}

void fb_draw_text_centered(struct fb_draw *draw, u32 centre_x, u32 y,
                           const char *text, u32 scale, u32 rgb)
{
    u32 width = fb_draw_text_width(text, scale);
    u32 x = width < centre_x ? centre_x - width / 2 : 0;
    fb_draw_text(draw, x, y, text, scale, rgb);
}
