/*
 * Zhaoxu OS - reflected CRC-32.
 *
 * A bit-at-a-time implementation keeps the kernel free of a 1 KiB lookup
 * table; the filesystems that use it read metadata sectors, not bulk data,
 * so the simpler loop is the right trade-off.
 */
#include <crc32.h>
#include <types.h>

u32 crc32_le(u32 seed, const void *data, usize length)
{
    const u8 *bytes = data;
    u32 crc = ~seed;
    for (usize i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (u32)-(i32)(crc & 1));
    }
    return ~crc;
}
