#ifndef ZHAOXU_TTF_H
#define ZHAOXU_TTF_H

#include <types.h>

/* Largest em size the rasterizer supports; buffers are sized for it and
 * the active size is chosen at runtime (20 px at 800p, 24 px at 1080p). */
#define TTF_MAX_PX 28

int ttf_load(const u8 *data, u64 size);
int ttf_available(void);
int ttf_autoload(const char *path);

int ttf_set_pixel_size(int px);
int ttf_pixel_size(void);
int ttf_line_height(void);

u32 ttf_upem(void);
u32 ttf_num_glyphs(void);
int ttf_cmap_fmt(void);

int ttf_glyph_for(u32 codepoint, u32 *glyph);
u32 ttf_glyph_advance(u32 glyph);
int ttf_glyph_bbox(u32 glyph, i32 box[4]);

struct ttf_glyph {
    u8 alpha[TTF_MAX_PX * (TTF_MAX_PX + 6)];
    u8 w, h, adv;
    i8 bx, by;
};

int ttf_render(u32 codepoint, struct ttf_glyph *out);

#endif
