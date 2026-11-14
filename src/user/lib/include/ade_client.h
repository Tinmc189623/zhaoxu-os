#ifndef ZHAOXU_ADE_CLIENT_H
#define ZHAOXU_ADE_CLIENT_H

#include <ade_protocol.h>

long ade_connect(void);
int ade_create_window(long channel, int width, int height, const char *title,
                      char glyph, u32 accent);
int ade_clear(long channel, u32 color);
int ade_fill(long channel, int x, int y, int w, int h, u32 color);
int ade_round(long channel, int x, int y, int w, int h, u32 radius,
              u32 color);
int ade_text(long channel, int x, int y, u32 color, const char *text);
int ade_text_flags(long channel, int x, int y, u32 color, u32 flags,
                   const char *text);
int ade_commit(long channel);
/* Declare that the pointer inside (x, y, w, h) --- content coordinates ---
 * uses the named cursor theme shape.  Zones reset on the next ade_clear() so
 * they can be re-declared with each frame. */
int ade_cursor(long channel, int x, int y, int w, int h, const char *name);
int ade_poll(long channel, struct ade_message *message);

#endif
