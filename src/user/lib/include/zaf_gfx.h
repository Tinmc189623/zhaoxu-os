#ifndef ZAF_GFX_H
#define ZAF_GFX_H

/*
 * ZAF rendering interface (milestone Z0).
 *
 * This is the backend-neutral contract that the ZAF compositor, widgets and
 * the future Skia backend all draw through. The software backend in
 * zaf_gfx_soft.c implements it today; a Skia backend can implement the same
 * header without touching callers.
 */

typedef unsigned int zaf_u32;
typedef unsigned char zaf_u8;

enum zaf_format {
    ZAF_FMT_XRGB8888 = 1, /* 32 bpp, X in the high byte, true colour */
    ZAF_FMT_RGB888 = 2    /* 24 bpp packed RGB, the required true-colour mode */
};

struct zaf_surface {
    zaf_u8 *pixels;
    int width;
    int height;
    int stride;              /* bytes per row */
    enum zaf_format format;
};

/* Bind a caller-owned pixel buffer. Returns 0 on success. */
int zaf_surface_attach(struct zaf_surface *surface, void *pixels, int width,
                       int height, int stride, enum zaf_format format);

void zaf_clear(struct zaf_surface *surface, zaf_u32 rgb);
void zaf_fill_rect(struct zaf_surface *surface, int x, int y, int w, int h,
                   zaf_u32 rgb);
void zaf_fill_round_rect(struct zaf_surface *surface, int x, int y, int w,
                         int h, int radius, zaf_u32 rgb);
void zaf_draw_line(struct zaf_surface *surface, int x0, int y0, int x1, int y1,
                   zaf_u32 rgb);
/* Even-odd polygon fill; xs/ys hold `count` points. */
void zaf_fill_polygon(struct zaf_surface *surface, const int *xs, const int *ys,
                      int count, zaf_u32 rgb);
void zaf_blit_alpha(struct zaf_surface *surface, int x, int y, int w, int h,
                    const zaf_u8 *alpha, zaf_u32 rgb, zaf_u8 opacity);

#endif
