#ifndef ZHAOXU_INFLATE_H
#define ZHAOXU_INFLATE_H

#include <types.h>

/* Decompress an RFC 1950 zlib stream (RFC 1951 DEFLATE payload).  Returns 0
 * on success and stores the output length in `out_len`. */
int zlib_decompress(const u8 *source, u32 source_len, u8 *destination,
                    u32 capacity, u32 *out_len);

#endif
