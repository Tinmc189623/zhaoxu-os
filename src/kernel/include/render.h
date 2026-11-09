#ifndef ZHAOXU_RENDER_H
#define ZHAOXU_RENDER_H

#include <types.h>
#include <zos_api.h>

#ifdef __cplusplus
extern "C" {
#endif

struct boot_info;

struct render_surface {
    u32 *pixels;
    int width;
    int height;
    int stride;
    u32 format;
    int screen;
};

struct render_rect {
    int x0, y0, x1, y1;
};

void render_init(const struct boot_info *bi);
void render_init_surface(u32 *pixels, int width, int height, int stride,
                         u32 format);
void render_set_page_allocator(u64 (*alloc)(u32 pages));
struct render_surface *render_screen(void);
struct render_surface *render_surface_create(int width, int height);

void render_set_target(struct render_surface *surface);
struct render_surface *render_target(void);
void render_set_clip(struct render_rect rect);
struct render_rect render_clip(void);

struct render_rect render_rect_make(int x, int y, int w, int h);
struct render_rect render_rect_intersect(struct render_rect a,
                                         struct render_rect b);
int render_rect_empty(struct render_rect r);
int render_rect_intersects(struct render_rect a, struct render_rect b);

u32 render_pack(u32 rgb);
u32 render_lerp(u32 a, u32 b, int step, int total);

void render_fill_rect(int x, int y, int w, int h, u32 rgb);
void render_blend_rect(int x, int y, int w, int h, u32 rgb, u32 alpha);
void render_rounded_rect(int x, int y, int w, int h, int radius, u32 rgb,
                         u32 alpha);
void render_vgrad(int x, int y, int w, int h, u32 top, u32 bottom);
void render_hline(int x, int y, int w, u32 rgb);
void render_border(int x, int y, int w, int h, u32 rgb);
void render_soft_shadow(int x, int y, int w, int h, int spread);
void render_soft_circle(int cx, int cy, int radius, u32 rgb, u32 max_alpha);
void render_blit(struct render_surface *source, struct render_rect rect);
void render_span(int x0, int x1, int y, u32 rgb);
int render_svg(const char *svg, int x, int y, int size);

int render_line_height(void);
int render_text_width(const char *text, int scale);
int render_text(const char *text, int x, int y, u32 fg, int scale);
int render_text_center(const char *text, int cx, int y, u32 fg, int scale);

void render_cursor(int x, int y);
void render_cursor_restore(void);

#ifdef __cplusplus
}
#endif

#endif
