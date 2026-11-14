#ifndef ZHAOXU_ADE_STDLIB_H
#define ZHAOXU_ADE_STDLIB_H

/* Minimal <stdlib.h> for stb_image inside the freestanding ADE binary.
 * stb_image's allocator macros are redirected to the cursor arena, so only
 * the integer helpers need declarations here. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int abs(int value);
long labs(long value);

#ifdef __cplusplus
}
#endif

#endif
