#ifndef ZHAOXU_FBDRAW_H
#define ZHAOXU_FBDRAW_H

#include <boot.h>
#include <types.h>

/* Minimal 32 bpp framebuffer painter for the kernel's own screens (boot
 * splash, panic) before the Ring3 display service exists. */
struct fb_draw {
    u8 *base;
    u32 width;
    u32 height;
    u32 pitch;
    u32 format;
};

/* Bind to the Limine framebuffer; 0 when the machine has none. */
int fb_draw_attach(struct fb_draw *draw, const struct boot_info *boot);
/* 0x00RRGGBB shifted into the framebuffer's native u32 order. */
u32 fb_draw_colour(const struct fb_draw *draw, u32 rgb);
void fb_draw_pixel(struct fb_draw *draw, u32 x, u32 y, u32 rgb);
void fb_draw_clear(struct fb_draw *draw, u32 rgb);
void fb_draw_rect(struct fb_draw *draw, u32 x, u32 y, u32 width, u32 height,
                  u32 rgb);
/* Vertical two-colour gradient across the whole screen. */
void fb_draw_gradient(struct fb_draw *draw, u32 top_rgb, u32 bottom_rgb);
/* One 8x8 glyph at (x, y), each font pixel drawn `scale` times. */
void fb_draw_glyph(struct fb_draw *draw, u32 x, u32 y, char c, u32 scale,
                   u32 rgb);
/* Width in pixels of `text` at the given glyph scale. */
u32 fb_draw_text_width(const char *text, u32 scale);
void fb_draw_text(struct fb_draw *draw, u32 x, u32 y, const char *text,
                  u32 scale, u32 rgb);
/* Text horizontally centred on `centre_x`. */
void fb_draw_text_centered(struct fb_draw *draw, u32 centre_x, u32 y,
                           const char *text, u32 scale, u32 rgb);

#endif
