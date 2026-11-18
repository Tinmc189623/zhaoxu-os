/* Zhaoxu OS - FS+ little-endian field helpers. */
#ifndef ZHAOXU_FSPLUS_ENDIAN_H
#define ZHAOXU_FSPLUS_ENDIAN_H

#include <stdint.h>

static inline uint16_t fsplus_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t fsplus_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t fsplus_get_u64(const uint8_t *p)
{
    return (uint64_t)fsplus_get_u32(p) |
           ((uint64_t)fsplus_get_u32(p + 4) << 32);
}

static inline void fsplus_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline void fsplus_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static inline void fsplus_put_u64(uint8_t *p, uint64_t value)
{
    fsplus_put_u32(p, (uint32_t)value);
    fsplus_put_u32(p + 4, (uint32_t)(value >> 32));
}

#endif
