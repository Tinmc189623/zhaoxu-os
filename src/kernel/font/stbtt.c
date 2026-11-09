/*
 * Zhaoxu OS - stb_truetype integration.
 *
 * The font engine is stb_truetype.h v1.26, unmodified, from
 * third_party/stb.  This file only supplies the freestanding hooks: heap,
 * assertions and the handful of math routines stb needs.
 */
#include <kprintf.h>
#include <mem.h>
#include <stddef.h>
#include <string.h>
#include <stbtt.h>
#include <types.h>

static double z_fabs(double x)
{
    union {
        double d;
        u64 u;
    } value;
    value.d = x;
    value.u &= 0x7FFFFFFFFFFFFFFFULL;
    return value.d;
}

static double z_floor(double x)
{
    if (x != x || x >= 9.0e18 || x <= -9.0e18)
        return x;
    double truncated = (double)(i64)x;
    if (truncated > x)
        truncated -= 1.0;
    return truncated;
}

static double z_ceil(double x)
{
    if (x != x || x >= 9.0e18 || x <= -9.0e18)
        return x;
    double truncated = (double)(i64)x;
    if (truncated < x)
        truncated += 1.0;
    return truncated;
}

static double z_sqrt(double x)
{
    if (x <= 0)
        return 0;
    double result;
    __asm__ volatile("sqrtsd %1, %0" : "=x"(result) : "x"(x));
    return result;
}

static double z_fmod(double x, double y)
{
    if (y == 0)
        return 0;
    return x - z_floor(x / y) * y;
}

static double z_cos(double x)
{
    double result;
    __asm__ volatile("fldl %1; fcos; fstpl %0"
                     : "=m"(result) : "m"(x) : "st");
    return result;
}

static double z_atan2(double y, double x)
{
    double result;
    __asm__ volatile("fldl %1; fldl %2; fpatan; fstpl %0"
                     : "=m"(result) : "m"(y), "m"(x) : "st");
    return result;
}

static double z_acos(double x)
{
    if (x <= -1.0)
        return 3.14159265358979323846;
    if (x >= 1.0)
        return 0;
    return z_atan2(z_sqrt(1.0 - x * x), x);
}

/* stb only calls pow() for the signed-distance path, which the desktop does
 * not use; supplying the cube root keeps that code path well-defined. */
static double z_pow(double x, double y)
{
    if (y == 1.0 / 3.0) {
        int negative = x < 0;
        double value = z_fabs(x);
        if (value == 0)
            return 0;
        double root = value > 1 ? value / 3 : value;
        for (int i = 0; i < 24; i++)
            root = (2 * root + value / (root * root)) / 3;
        return negative ? -root : root;
    }
    if (y == 2.0)
        return x * x;
    if (y == 1.0)
        return x;
    return x;
}

#define STBTT_ifloor(x)     ((int)z_floor(x))
#define STBTT_iceil(x)      ((int)z_ceil(x))
#define STBTT_sqrt(x)       z_sqrt(x)
#define STBTT_pow(x, y)     z_pow((x), (y))
#define STBTT_fmod(x, y)    z_fmod((x), (y))
#define STBTT_cos(x)        z_cos(x)
#define STBTT_acos(x)       z_acos(x)
#define STBTT_fabs(x)       z_fabs(x)
#define STBTT_malloc(x, u)  ((void)(u), kmalloc((usize)(x)))
#define STBTT_free(x, u)    ((void)(u), kfree((void *)(x)))
#define STBTT_assert(x)     ((void)0)

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

static stbtt_fontinfo g_font;
static int g_ready;

int zstb_load(const u8 *data, u64 size)
{
    (void)size;
    if (!data)
        return 0;

    int offset = stbtt_GetFontOffsetForIndex((const unsigned char *)data, 0);
    if (offset < 0)
        return 0;
    if (!stbtt_InitFont(&g_font, (const unsigned char *)data, offset))
        return 0;

    g_ready = 1;
    return 1;
}

int zstb_ready(void)
{
    return g_ready;
}

float zstb_scale(float pixels)
{
    if (!g_ready || pixels <= 0)
        return 0;
    return stbtt_ScaleForPixelHeight(&g_font, pixels);
}

void zstb_vmetrics(int *ascent, int *descent, int *line_gap)
{
    if (!g_ready)
        return;
    stbtt_GetFontVMetrics(&g_font, ascent, descent, line_gap);
}

u32 zstb_upem(void)
{
    if (!g_ready || g_font.head == 0)
        return 0;
    const u8 *head = g_font.data + g_font.head;
    return ((u32)head[18] << 8) | head[19];
}

u32 zstb_num_glyphs(void)
{
    return g_ready ? (u32)g_font.numGlyphs : 0;
}

int zstb_find_glyph(u32 codepoint)
{
    if (!g_ready)
        return 0;
    return stbtt_FindGlyphIndex(&g_font, (int)codepoint);
}

void zstb_hmetrics(int glyph, int *advance, int *bearing)
{
    if (!g_ready)
        return;
    stbtt_GetGlyphHMetrics(&g_font, glyph, advance, bearing);
}

void zstb_box(int glyph, float scale, int box[4])
{
    if (!g_ready) {
        box[0] = box[1] = box[2] = box[3] = 0;
        return;
    }
    stbtt_GetGlyphBitmapBox(&g_font, glyph, scale, scale,
                            &box[0], &box[1], &box[2], &box[3]);
}

void zstb_render(int glyph, float scale, u8 *out, int width, int height,
                 int stride)
{
    if (!g_ready)
        return;
    stbtt_MakeGlyphBitmap(&g_font, out, width, height, stride, scale, scale,
                          glyph);
}
