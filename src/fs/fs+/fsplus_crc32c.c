/* Zhaoxu OS - FS+ CRC32C. */
#include "fsplus_format.h"

/* Update a CRC32C accumulator with one buffer. */
static uint32_t crc32c_update(uint32_t crc, const uint8_t *data, size_t length)
{
    size_t index;
    unsigned bit;

    for (index = 0; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

/* Compute a seeded CRC32C with FS+ initial and final values. */
uint32_t fsplus_crc32c_seeded(const void *seed, size_t seed_len,
                              const void *data, size_t data_len)
{
    uint32_t crc = 0xFFFFFFFFu;

    if (seed_len != 0 && seed != NULL) {
        crc = crc32c_update(crc, (const uint8_t *)seed, seed_len);
    }
    if (data_len != 0 && data != NULL) {
        crc = crc32c_update(crc, (const uint8_t *)data, data_len);
    }
    return crc ^ 0xFFFFFFFFu;
}
