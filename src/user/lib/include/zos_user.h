#ifndef ZHAOXU_USER_LIB_H
#define ZHAOXU_USER_LIB_H

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Renderer surfaces are backed by shared-memory objects so the desktop does
 * not need a private heap allocator in ring 3. */
u64 zos_alloc_pages(u32 pages);

/* Read a system font from /mnt into a shared mapping. */
u8 *zos_load_font(u64 *size_out);

/* Read the default wallpaper (/mnt/WALLPAPER.RAW, rule 33) into a shared
 * mapping. Returns 0 when the data volume has no wallpaper. */
u8 *zos_load_wallpaper(u64 *size_out);

#ifdef __cplusplus
}
#endif

#endif
