/* Aqua Desktop Environment font adapter.
 *
 * The Ring3 desktop uses the unmodified FreeType engine.  Keeping this
 * adapter separate from the freestanding kernel font code prevents a libc
 * or floating-point dependency from leaking into Yuanshuo™.
 */
#include <ft2build.h>
#include FT_FREETYPE_H
#include <ttf.h>
#include <types.h>

static FT_Library g_library;
static FT_Face g_face;
static int g_ready;
static u32 g_pixels = 20;

int ttf_set_pixel_size(int px)
{
    if (px < 1) px = 1;
    if (px > TTF_MAX_PX) px = TTF_MAX_PX;
    g_pixels = (u32)px;
    return g_ready ? (FT_Set_Pixel_Sizes(g_face, 0, px) == 0 ? 0 : -1) : -1;
}

int ttf_pixel_size(void) { return (int)g_pixels; }
int ttf_line_height(void)
{
    return g_ready ? (int)(g_face->size->metrics.height >> 6) : 16;
}
int ttf_load(const u8 *data, u64 size)
{
    if (!data || size == 0) return -1;
    if (g_face) { FT_Done_Face(g_face); g_face = 0; }
    if (!g_library && FT_Init_FreeType(&g_library) != 0) return -1;
    if (FT_New_Memory_Face(g_library, data, (FT_Long)size, 0, &g_face) != 0) return -1;
    g_ready = 1;
    return ttf_set_pixel_size((int)g_pixels);
}
int ttf_available(void) { return g_ready; }
u32 ttf_upem(void) { return g_ready ? g_face->units_per_EM : 0; }
u32 ttf_num_glyphs(void) { return g_ready ? g_face->num_glyphs : 0; }
int ttf_glyph_for(u32 codepoint, u32 *glyph)
{
    if (!g_ready || !glyph) return -1;
    *glyph = FT_Get_Char_Index(g_face, codepoint);
    return *glyph ? 0 : -1;
}
u32 ttf_glyph_advance(u32 glyph)
{
    if (!g_ready || FT_Load_Glyph(g_face, glyph, FT_LOAD_DEFAULT) != 0) return 0;
    return (u32)((g_face->glyph->advance.x + 32) >> 6);
}
int ttf_glyph_bbox(u32 glyph, i32 box[4])
{
    if (!g_ready || !box || FT_Load_Glyph(g_face, glyph, FT_LOAD_DEFAULT) != 0) return -1;
    box[0] = (i32)(g_face->glyph->metrics.horiBearingX >> 6);
    box[1] = (i32)((g_face->size->metrics.ascender - g_face->glyph->metrics.horiBearingY) >> 6);
    box[2] = box[0] + (i32)((g_face->glyph->metrics.width + 63) >> 6);
    box[3] = box[1] + (i32)((g_face->glyph->metrics.height + 63) >> 6);
    return 0;
}
int ttf_render(u32 codepoint, struct ttf_glyph *out)
{
    if (!g_ready || !out || FT_Load_Char(g_face, codepoint, FT_LOAD_RENDER) != 0) return -1;
    FT_GlyphSlot slot = g_face->glyph;
    if (slot->bitmap.width > TTF_MAX_PX + 6 || slot->bitmap.rows > TTF_MAX_PX ||
        (u32)slot->bitmap.width * slot->bitmap.rows > sizeof(out->alpha)) return -2;
    out->w = (u8)slot->bitmap.width; out->h = (u8)slot->bitmap.rows;
    out->bx = (i8)slot->bitmap_left;
    /* render.c treats `by` as the distance from the line top (baseline minus
     * ascender) to the bitmap top.  FreeType reports bitmap_top upwards from
     * the baseline, so subtract it from the ascender; using it directly both
     * flips the glyph and makes every glyph bob to its own height. */
    i32 ascent = (i32)((g_face->size->metrics.ascender + 32) >> 6);
    int top = (int)slot->bitmap_top;
    out->by = (i8)(ascent - top);
    i32 advance = (i32)((slot->advance.x + 32) >> 6);
    if (advance < 0)
        advance = 0;
    if (advance > 255)
        advance = 255;
    out->adv = (u8)advance;
    for (u32 y = 0; y < out->h; y++)
        for (u32 x = 0; x < out->w; x++)
            out->alpha[y * out->w + x] = slot->bitmap.buffer[y * slot->bitmap.pitch + x];
    return 0;
}
