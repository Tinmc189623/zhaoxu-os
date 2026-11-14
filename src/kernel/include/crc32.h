#ifndef ZHAOXU_CRC32_H
#define ZHAOXU_CRC32_H

#include <types.h>

/* Reflected CRC-32 (IEEE 802.3), the variant used by UBI, UBIFS and zlib. */
u32 crc32_le(u32 seed, const void *data, usize length);

#endif
