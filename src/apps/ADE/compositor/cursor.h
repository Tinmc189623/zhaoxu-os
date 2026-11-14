#ifndef ZHAOXU_CURSOR_THEME_H
#define ZHAOXU_CURSOR_THEME_H

#include <types.h>

/* A cursor theme is a directory with a text `cursor.theme` descriptor and
 * one PNG per frame.  The descriptor names each cursor, its hotspot and an
 * optional frame delay:
 *
 *   [arrow]
 *   image = arrow.png
 *   hotspot = 2,1
 *
 *   [wait]
 *   frames = wait-0.png,wait-1.png
 *   delay = 120
 *   hotspot = 12,12
 */
#define CURSOR_MAX_DEFS 12
#define CURSOR_MAX_FRAMES 8
#define CURSOR_MAX_FRAME_POOL 32
#define CURSOR_MAX_SIZE 48
#define CURSOR_NAME_MAX 32
#define CURSOR_FILE_MAX 64

struct cursor_frame {
    u32 *pixels;
    int width;
    int height;
};

struct cursor_def {
    char name[CURSOR_NAME_MAX];
    int hotspot_x;
    int hotspot_y;
    int delay_ms;
    int frame_count;
    struct cursor_frame frames[CURSOR_MAX_FRAMES];
};

int cursor_theme_load(const char *directory);
const struct cursor_def *cursor_find(const char *name);
int cursor_theme_ready(void);
int cursor_theme_count(void);
const char *cursor_theme_name(void);
int cursor_frame_index(const struct cursor_def *definition, u64 now_ms);

/* Implemented in cursor_stb.c. */
int cursor_decode_png(const void *data, usize length, int *width, int *height,
                      u32 *pixels, int capacity_pixels);
void cursor_stbi_reset(void);

#endif
