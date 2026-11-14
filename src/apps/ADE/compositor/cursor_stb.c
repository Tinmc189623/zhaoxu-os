/*
 * Zhaoxu OS - PNG decoding for cursor themes.
 *
 * stb_image is used as-is (rule 19: reuse the vendored header) with its
 * allocator redirected to a shared-memory arena.  The arena is reset before
 * every frame, and the decoded RGBA8 pixels are copied into the caller's
 * ARGB storage so the arena can be reused immediately.
 */
#include <string.h>
#include <types.h>
#include <zos_user.h>

static void *cursor_stbi_alloc(usize size);
static void *cursor_stbi_realloc(void *pointer, usize old_size,
                                 usize new_size);

#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_NO_GIF
#define STBI_NO_JPEG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(size) cursor_stbi_alloc(size)
#define STBI_FREE(pointer) ((void)(pointer))
#define STBI_REALLOC_SIZED(pointer, old_size, new_size) \
    cursor_stbi_realloc(pointer, old_size, new_size)

#define STB_IMAGE_IMPLEMENTATION
#include "../../../third_party/stb/stb_image.h"

#define CURSOR_ARENA_PAGES 64u /* 256 KiB is ample for 48x48 RGBA frames */

static u8 *g_arena;
static usize g_arena_size;
static usize g_arena_used;

/* Allocate a 256 KiB shared-memory arena on first use. */
static int arena_ready(void)
{
    if (g_arena)
        return 0;
    u64 address = zos_alloc_pages(CURSOR_ARENA_PAGES);
    if (!address)
        return -1;
    g_arena = (u8 *)(uintptr_t)address;
    g_arena_size = CURSOR_ARENA_PAGES * 4096u;
    g_arena_used = 0;
    return 0;
}

void cursor_stbi_reset(void)
{
    g_arena_used = 0;
}

static void *cursor_stbi_alloc(usize size)
{
    if (arena_ready() != 0 || !size)
        return NULL;
    usize aligned = (size + 15u) & ~(usize)15u;
    if (aligned > g_arena_size - g_arena_used)
        return NULL;
    void *result = g_arena + g_arena_used;
    g_arena_used += aligned;
    return result;
}

static void *cursor_stbi_realloc(void *pointer, usize old_size,
                                 usize new_size)
{
    void *result = cursor_stbi_alloc(new_size);
    if (!result || !pointer)
        return result;
    usize copy = old_size < new_size ? old_size : new_size;
    memcpy(result, pointer, copy);
    return result;
}

/* Decode one PNG into tightly packed 0xAARRGGBB pixels.  Returns the pixel
 * count, or -1 when the image is malformed or larger than the slot. */
int cursor_decode_png(const void *data, usize length, int *width, int *height,
                      u32 *pixels, int capacity_pixels)
{
    if (!data || !length || !pixels || capacity_pixels <= 0)
        return -1;
    cursor_stbi_reset();
    int w = 0;
    int h = 0;
    int components = 0;
    unsigned char *rgba = stbi_load_from_memory(
        (const stbi_uc *)data, (int)length, &w, &h, &components, 4);
    if (!rgba || w <= 0 || h <= 0)
        return -1;
    if ((i64)w * h > capacity_pixels)
        return -1;
    for (int i = 0; i < w * h; i++) {
        u8 r = rgba[i * 4 + 0];
        u8 g = rgba[i * 4 + 1];
        u8 b = rgba[i * 4 + 2];
        u8 a = rgba[i * 4 + 3];
        pixels[i] = ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
    }
    *width = w;
    *height = h;
    return w * h;
}
