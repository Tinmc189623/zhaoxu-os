#ifndef ZHAOXU_STBTT_H
#define ZHAOXU_STBTT_H

#include <types.h>

/* Thin wrapper around stb_truetype.h so the rest of the kernel never sees
 * stbtt_fontinfo or its floating-point internals. */
int zstb_load(const u8 *data, u64 size);
int zstb_ready(void);
float zstb_scale(float pixels);
void zstb_vmetrics(int *ascent, int *descent, int *line_gap);
u32 zstb_upem(void);
u32 zstb_num_glyphs(void);
int zstb_find_glyph(u32 codepoint);
void zstb_hmetrics(int glyph, int *advance, int *bearing);
void zstb_box(int glyph, float scale, int box[4]);
void zstb_render(int glyph, float scale, u8 *out, int width, int height,
                 int stride);

#endif
