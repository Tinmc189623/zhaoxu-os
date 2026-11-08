#ifndef ZHAOXU_DRIVERS_H
#define ZHAOXU_DRIVERS_H

#include <types.h>

/* Implemented in Ada (src/drivers). */
void serial_init(u32 baud);
void serial_putc(u8 value);
int  serial_getc(void);

void pit_init(u32 hz);

void ps2_init(void);
u8   ps2_status(void);
u8   ps2_read_data(void);

void gop_fill_rect(u64 fb, u32 pitch, u32 x, u32 y, u32 w, u32 h, u32 color);
void gop_blit_glyph(u64 fb, u32 pitch, u32 x, u32 y, const u8 *glyph,
                    u32 fg, u32 bg);
void gop_copy_rows(u64 fb, u32 pitch, u32 src_y, u32 dst_y, u32 rows,
                   u32 width_bytes);
void gop_clear(u64 fb, u32 pitch, u32 width, u32 height, u32 color);

#endif
