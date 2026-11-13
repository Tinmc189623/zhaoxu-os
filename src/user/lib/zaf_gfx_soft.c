/*
 * ZAF software rasteriser (milestone Z0).
 *
 * Writes straight into a caller-owned surface, supports both 32 bpp XRGB8888
 * and the required 24 bpp packed RGB888 true-colour formats, and clips every
 * primitive to the surface bounds.
 */
#include "zaf_gfx.h"

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int bytes_per_pixel(enum zaf_format format)
{
    return format == ZAF_FMT_RGB888 ? 3 : 4;
}

static void put_pixel(struct zaf_surface *s, int x, int y, zaf_u32 rgb)
{
    if (!s->pixels || x < 0 || y < 0 || x >= s->width || y >= s->height)
        return;
    zaf_u8 *p = s->pixels + (long)y * s->stride + (long)x * bytes_per_pixel(s->format);
    if (s->format == ZAF_FMT_RGB888) {
        p[0] = (zaf_u8)(rgb >> 16);
        p[1] = (zaf_u8)(rgb >> 8);
        p[2] = (zaf_u8)rgb;
    } else {
        p[0] = (zaf_u8)rgb;
        p[1] = (zaf_u8)(rgb >> 8);
        p[2] = (zaf_u8)(rgb >> 16);
        p[3] = 0xFF;
    }
}

int zaf_surface_attach(struct zaf_surface *surface, void *pixels, int width,
                       int height, int stride, enum zaf_format format)
{
    if (!surface || !pixels || width <= 0 || height <= 0)
        return -1;
    if (format != ZAF_FMT_RGB888 && format != ZAF_FMT_XRGB8888)
        return -1;
    if (stride < width * bytes_per_pixel(format))
        return -1;
    surface->pixels = (zaf_u8 *)pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->format = format;
    return 0;
}

void zaf_clear(struct zaf_surface *s, zaf_u32 rgb)
{
    zaf_fill_rect(s, 0, 0, s->width, s->height, rgb);
}

void zaf_fill_rect(struct zaf_surface *s, int x, int y, int w, int h, zaf_u32 rgb)
{
    int x0 = clampi(x, 0, s->width);
    int y0 = clampi(y, 0, s->height);
    int x1 = clampi(x + w, 0, s->width);
    int y1 = clampi(y + h, 0, s->height);
    if (x1 <= x0 || y1 <= y0)
        return;

    int bpp = bytes_per_pixel(s->format);
    int span = x1 - x0;
    for (int row = y0; row < y1; row++) {
        zaf_u8 *p = s->pixels + (long)row * s->stride + (long)x0 * bpp;
        if (s->format == ZAF_FMT_RGB888) {
            zaf_u8 r = (zaf_u8)(rgb >> 16), g = (zaf_u8)(rgb >> 8);
            zaf_u8 b = (zaf_u8)rgb;
            for (int col = 0; col < span; col++) {
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p += 3;
            }
        } else {
            zaf_u8 r = (zaf_u8)rgb, g = (zaf_u8)(rgb >> 8);
            zaf_u8 b = (zaf_u8)(rgb >> 16);
            for (int col = 0; col < span; col++) {
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = 0xFF;
                p += 4;
            }
        }
    }
}

void zaf_fill_round_rect(struct zaf_surface *s, int x, int y, int w, int h,
                         int radius, zaf_u32 rgb)
{
    if (radius <= 0 || radius * 2 > w || radius * 2 > h) {
        zaf_fill_rect(s, x, y, w, h, rgb);
        return;
    }
    int r2 = radius * radius;
    for (int row = 0; row < h; row++) {
        int dy = 0;
        if (row < radius)
            dy = radius - row - 1;
        else if (row >= h - radius)
            dy = row - (h - radius);
        int inset = 0;
        while (inset < radius && (radius - inset) * (radius - inset) + dy * dy > r2)
            inset++;
        zaf_fill_rect(s, x + inset, y + row, w - inset * 2, 1, rgb);
    }
}

void zaf_draw_line(struct zaf_surface *s, int x0, int y0, int x1, int y1,
                   zaf_u32 rgb)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        put_pixel(s, x0, y0, rgb);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void zaf_fill_polygon(struct zaf_surface *s, const int *xs, const int *ys,
                      int count, zaf_u32 rgb)
{
    if (!s || !xs || !ys || count < 3)
        return;
    int min_y = ys[0], max_y = ys[0];
    for (int i = 1; i < count; i++) {
        if (ys[i] < min_y)
            min_y = ys[i];
        if (ys[i] > max_y)
            max_y = ys[i];
    }
    min_y = clampi(min_y, 0, s->height - 1);
    max_y = clampi(max_y, 0, s->height - 1);

    for (int y = min_y; y <= max_y; y++) {
        int cross[64];
        int found = 0;
        for (int i = 0; i < count && found + 2 < 64; i++) {
            int j = (i + 1) % count;
            int yi = ys[i], yj = ys[j];
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                int xi = xs[i], xj = xs[j];
                cross[found++] = xi + (y - yi) * (xj - xi) / (yj - yi);
            }
        }
        for (int i = 0; i + 1 < found; i++) {
            for (int j = i + 1; j < found; j++) {
                if (cross[j] < cross[i]) {
                    int t = cross[i];
                    cross[i] = cross[j];
                    cross[j] = t;
                }
            }
        }
        for (int i = 0; i + 1 < found; i += 2)
            zaf_fill_rect(s, cross[i], y, cross[i + 1] - cross[i], 1, rgb);
    }
}

void zaf_blit_alpha(struct zaf_surface *s, int x, int y, int w, int h,
                    const zaf_u8 *alpha, zaf_u32 rgb, zaf_u8 opacity)
{
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            zaf_u32 a = (zaf_u32)alpha[row * w + col] * opacity / 255;
            if (!a)
                continue;
            /* Flat source colour over the destination: blend per channel. */
            int px = x + col, py = y + row;
            if (px < 0 || py < 0 || px >= s->width || py >= s->height)
                continue;
            zaf_u8 *p = s->pixels + (long)py * s->stride +
                        (long)px * bytes_per_pixel(s->format);
            if (s->format == ZAF_FMT_RGB888) {
                p[0] = (zaf_u8)(((rgb >> 16) & 255) * a / 255 + p[0] * (255 - a) / 255);
                p[1] = (zaf_u8)(((rgb >> 8) & 255) * a / 255 + p[1] * (255 - a) / 255);
                p[2] = (zaf_u8)((rgb & 255) * a / 255 + p[2] * (255 - a) / 255);
            } else {
                p[0] = (zaf_u8)((rgb & 255) * a / 255 + p[0] * (255 - a) / 255);
                p[1] = (zaf_u8)(((rgb >> 8) & 255) * a / 255 + p[1] * (255 - a) / 255);
                p[2] = (zaf_u8)(((rgb >> 16) & 255) * a / 255 + p[2] * (255 - a) / 255);
            }
        }
    }
}
