/*
 * Zhaoxu OS - FreeType port smoke test.
 *
 * Loads the boot font staged by the loader, creates a FreeType face from the
 * in-memory OpenType file and rasterizes U+4E2D.  The serial output reports
 * the face name, glyph metrics and the number of covered pixels.
 */
#include <ft2build.h>
#include FT_FREETYPE_H

#include "userlib.h"

#define FONT_MAX_BYTES (8u * 1024u * 1024u)

static int fail(const char *where)
{
    u_puts("[ft] FAIL: ");
    u_puts(where);
    u_puts("\n");
    return 1;
}

int user_main(long *stack)
{
    (void)stack;

    long shm = sys_shm_create(FONT_MAX_BYTES);
    if (shm <= 0)
        return fail("shm_create");
    unsigned char *font = (unsigned char *)sys_shm_map(shm);
    if (!font)
        return fail("shm_map");

    long size = sys_boot_font(font, FONT_MAX_BYTES);
    u_puts("[ft] boot font ");
    u_putn(size);
    u_puts(" bytes\n");
    if (size < 13)
        return fail("boot_font");

    FT_Library library;
    if (FT_Init_FreeType(&library) != 0)
        return fail("FT_Init_FreeType");

    FT_Face face;
    if (FT_New_Memory_Face(library, font, size, 0, &face) != 0)
        return fail("FT_New_Memory_Face");

    u_puts("[ft] face ");
    if (face->family_name)
        u_puts(face->family_name);
    else
        u_puts("(unnamed)");
    u_puts("\n");

    if (FT_Set_Pixel_Sizes(face, 0, 32) != 0)
        return fail("FT_Set_Pixel_Sizes");
    if (FT_Load_Char(face, 0x4E2D, FT_LOAD_RENDER) != 0)
        return fail("FT_Load_Char");

    FT_GlyphSlot glyph = face->glyph;
    unsigned long covered = 0;
    for (unsigned int y = 0; y < glyph->bitmap.rows; y++) {
        for (unsigned int x = 0; x < glyph->bitmap.width; x++) {
            if (glyph->bitmap.buffer[y * glyph->bitmap.pitch + x])
                covered++;
        }
    }
    u_puts("[ft] glyph ");
    u_putn((long)glyph->bitmap.width);
    u_puts("x");
    u_putn((long)glyph->bitmap.rows);
    u_puts(" covered ");
    u_putn((long)covered);
    u_puts("\n");
    if (glyph->bitmap.width == 0 || glyph->bitmap.rows == 0 || covered == 0)
        return fail("empty glyph");

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    u_puts("[ft] FreeType PASS\n");
    return 0;
}
