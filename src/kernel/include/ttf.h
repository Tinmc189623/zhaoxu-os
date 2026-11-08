#ifndef ZHAOXU_TTF_H
#define ZHAOXU_TTF_H

#include <types.h>

#define TTF_PIXEL_SIZE 16

int ttf_load(const u8 *data, u64 size);
int ttf_available(void);
int ttf_autoload(const char *path);

u32 ttf_upem(void);
u32 ttf_num_glyphs(void);
int ttf_cmap_fmt(void);

int ttf_glyph_for(u32 codepoint, u32 *glyph);
u32 ttf_glyph_advance(u32 glyph);
int ttf_glyph_bbox(u32 glyph, i32 box[4]);

struct ttf_glyph {
    u8 alpha[TTF_PIXEL_SIZE * (TTF_PIXEL_SIZE + 4)];
    u8 w, h, adv;
    i8 bx, by;
};

int ttf_render(u32 codepoint, struct ttf_glyph *out);

#endif
