/*
 * Zhaoxu OS - system font API.
 *
 * Parsing and rasterization are done by stb_truetype (kernel/font/stbtt.c).
 * This file only keeps the font image mapped, owns the active pixel size and
 * adapts stb results to the small ttf_glyph contract used by the renderer.
 */
#include <kprintf.h>
#include <stbtt.h>
#include <string.h>
#include <ttf.h>
#include <types.h>

#ifndef ZOS_USER
#include <mem.h>
#include <vfs.h>
#endif

static int g_ready;
static u32 g_pixels = 20;
static float g_scale;
static int g_ascent_pixels;
static int g_line_height = 16;
static u32 g_upem;
static u32 g_glyphs;

static int clamp_i8(int value)
{
    if (value < -128)
        return -128;
    if (value > 127)
        return 127;
    return value;
}

static int round_positive(float value)
{
    return (int)(value + 0.5f);
}

int ttf_set_pixel_size(int px)
{
    if (px < 1)
        px = 1;
    if (px > TTF_MAX_PX)
        px = TTF_MAX_PX;
    g_pixels = (u32)px;
    if (!g_ready)
        return -1;

    g_scale = zstb_scale((float)px);
    int ascent = 0, descent = 0, line_gap = 0;
    zstb_vmetrics(&ascent, &descent, &line_gap);
    g_ascent_pixels = round_positive((float)ascent * g_scale);
    int height = round_positive((float)(ascent - descent + line_gap) * g_scale);
    g_line_height = height > px ? height : px;
    return 0;
}

int ttf_pixel_size(void)
{
    return (int)g_pixels;
}

int ttf_line_height(void)
{
    return g_line_height;
}

int ttf_load(const u8 *data, u64 size)
{
    if (!data || size == 0)
        return -1;
    if (!zstb_load(data, size))
        return -1;

    g_ready = 1;
    g_upem = zstb_upem();
    g_glyphs = zstb_num_glyphs();
    ttf_set_pixel_size((int)g_pixels);
    return 0;
}

#ifndef ZOS_USER
int ttf_autoload(const char *path)
{
    int is_dir = 0;
    u64 size = 0;
    if (!path || vfs_stat(path, &is_dir, &size) != 0 || is_dir)
        return -1;
    if (size < 12 || size > 64u * 1024 * 1024)
        return -1;

    u32 pages = (u32)((size + PAGE_SIZE - 1) / PAGE_SIZE);
    u64 address = vmm_alloc_pages(pages);
    if (!address)
        return -2;

    u64 read = 0;
    if (vfs_read_into(path, (u8 *)(uintptr_t)address,
                      (u64)pages * PAGE_SIZE, &read) != 0) {
        for (u32 i = 0; i < pages; i++) {
            u64 physical = vmm_unmap_page(address + (u64)i * PAGE_SIZE);
            if (physical)
                pmm_free_page(physical);
        }
        return -3;
    }

    if (ttf_load((const u8 *)(uintptr_t)address, read) != 0) {
        for (u32 i = 0; i < pages; i++) {
            u64 physical = vmm_unmap_page(address + (u64)i * PAGE_SIZE);
            if (physical)
                pmm_free_page(physical);
        }
        return -4;
    }
    return 0;
}
#endif

int ttf_available(void)
{
    return g_ready;
}

u32 ttf_upem(void)
{
    return g_upem;
}

u32 ttf_num_glyphs(void)
{
    return g_glyphs;
}

int ttf_glyph_for(u32 codepoint, u32 *glyph)
{
    if (!g_ready || !glyph)
        return -1;
    int index = zstb_find_glyph(codepoint);
    if (index <= 0)
        return -1;
    *glyph = (u32)index;
    return 0;
}

u32 ttf_glyph_advance(u32 glyph)
{
    if (!g_ready)
        return 0;
    int advance = 0, bearing = 0;
    zstb_hmetrics((int)glyph, &advance, &bearing);
    int pixels = round_positive((float)advance * g_scale);
    return pixels > 0 ? (u32)pixels : 0;
}

int ttf_glyph_bbox(u32 glyph, i32 box[4])
{
    if (!g_ready || !box)
        return -1;
    int raw[4];
    zstb_box((int)glyph, g_scale, raw);
    box[0] = raw[0];
    box[1] = raw[1] + g_ascent_pixels;
    box[2] = raw[2];
    box[3] = raw[3] + g_ascent_pixels;
    return 0;
}

int ttf_render(u32 codepoint, struct ttf_glyph *out)
{
    if (!g_ready || !out)
        return -1;

    int glyph = zstb_find_glyph(codepoint);
    if (glyph <= 0)
        return -1;

    out->w = 0;
    out->h = 0;
    out->bx = 0;
    out->by = 0;

    int advance = 0, bearing = 0;
    zstb_hmetrics(glyph, &advance, &bearing);
    int pixels = round_positive((float)advance * g_scale);
    out->adv = (u8)(pixels < 0 ? 0 : (pixels > 255 ? 255 : pixels));

    int box[4];
    zstb_box(glyph, g_scale, box);
    int width = box[2] - box[0];
    int height = box[3] - box[1];
    if (width <= 0 || height <= 0)
        return 0;

    if (width > TTF_MAX_PX + 6 || height > TTF_MAX_PX ||
        width * height > (int)sizeof(out->alpha))
        return -2;

    zstb_render(glyph, g_scale, out->alpha, width, height, width);
    out->w = (u8)width;
    out->h = (u8)height;
    out->bx = (i8)clamp_i8(box[0]);
    /* Rendering treats y as the top of the line, matching the old engine. */
    out->by = (i8)clamp_i8(box[1] + g_ascent_pixels);
    return 0;
}
