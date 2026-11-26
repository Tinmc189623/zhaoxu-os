/* FreeBSD endian.h subset used by sys/crypto/sha2 and libcrypt. */
#ifndef ZHAOXU_ENDIAN_PORT_H
#define ZHAOXU_ENDIAN_PORT_H

#include <stdint.h>

#define ZOS_LITTLE_ENDIAN 1234
#define ZOS_BIG_ENDIAN 4321
#define ZOS_PDP_ENDIAN 3412

#ifndef BYTE_ORDER
#define BYTE_ORDER ZOS_LITTLE_ENDIAN
#endif

static inline void zos_be32enc(void *output, uint32_t value)
{
    uint8_t *bytes = (uint8_t *)output;
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static inline uint32_t zos_be32dec(const void *input)
{
    const uint8_t *bytes = (const uint8_t *)input;
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline void zos_be64enc(void *output, uint64_t value)
{
    uint8_t *bytes = (uint8_t *)output;
    for (int i = 0; i < 8; i++)
        bytes[i] = (uint8_t)(value >> (56 - 8 * i));
}

static inline uint64_t zos_be64dec(const void *input)
{
    const uint8_t *bytes = (const uint8_t *)input;
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value = (value << 8) | bytes[i];
    return value;
}

static inline void zos_le32enc(void *output, uint32_t value)
{
    uint8_t *bytes = (uint8_t *)output;
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static inline uint32_t zos_le32dec(const void *input)
{
    const uint8_t *bytes = (const uint8_t *)input;
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline void zos_le64enc(void *output, uint64_t value)
{
    uint8_t *bytes = (uint8_t *)output;
    for (int i = 0; i < 8; i++)
        bytes[i] = (uint8_t)(value >> (8 * i));
}

static inline uint64_t zos_le64dec(const void *input)
{
    const uint8_t *bytes = (const uint8_t *)input;
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value |= (uint64_t)bytes[i] << (8 * i);
    return value;
}

#define be32enc zos_be32enc
#define be32dec zos_be32dec
#define be64enc zos_be64enc
#define be64dec zos_be64dec
#define le32enc zos_le32enc
#define le32dec zos_le32dec
#define le64enc zos_le64enc
#define le64dec zos_le64dec

#endif
