/* Zhaoxu OS - FS+ superblock encoding. */
#include "fsplus_endian.h"
#include "fsplus_format.h"

#include <string.h>

#define SB_OFF_MAGIC       0x000u
#define SB_OFF_VERSION     0x008u
#define SB_OFF_BLOCK_SIZE  0x010u
#define SB_OFF_FLAGS       0x014u
#define SB_OFF_TOTAL       0x018u
#define SB_OFF_FREE        0x020u
#define SB_OFF_ROOT_BLOCK  0x028u
#define SB_OFF_ROOT_LEVEL  0x030u
#define SB_OFF_CSUM_TYPE   0x034u
#define SB_OFF_GENERATION  0x038u
#define SB_OFF_ROOT_OWNER  0x040u
#define SB_OFF_UUID        0x048u
#define SB_OFF_LABEL       0x058u
#define SB_OFF_INCOMPAT    0x068u
#define SB_OFF_RO_COMPAT   0x06Cu
#define SB_OFF_COMPAT      0x070u
#define SB_OFF_SLOT        0x074u
#define SB_OFF_AG_SIZE     0x078u
#define SB_OFF_AG_COUNT    0x07Cu
#define SB_OFF_CHECKSUM    0x090u
#define SB_CHECKSUM_COVER  0x090u

/* Initialize a superblock block with the current FS+ layout. */
int fsplus_superblock_init(uint8_t block[FSPLUS_BLOCK_SIZE], uint64_t total_blocks,
                           uint64_t free_blocks, uint64_t root_tree_block,
                           uint16_t root_tree_level, uint64_t generation,
                           uint32_t slot, uint32_t ag_size_blocks,
                           uint32_t ag_count, const uint8_t uuid[16],
                           const char *label, uint32_t incompat,
                           uint32_t ro_compat, uint32_t compat)
{
    size_t index;
    uint32_t checksum;

    if (block == NULL || uuid == NULL || total_blocks == 0 ||
        ag_size_blocks == 0 || ag_count == 0 || root_tree_block >= total_blocks ||
        root_tree_block < 2 || root_tree_level > FSPLUS_NODE_MAX_LEVEL ||
        slot > 1 || (generation & 1u) != slot || free_blocks > total_blocks ||
        (incompat & FSPLUS_INCOMPAT_REQUIRED) != FSPLUS_INCOMPAT_REQUIRED) {
        return -1;
    }

    memset(block, 0, FSPLUS_BLOCK_SIZE);
    memcpy(block + SB_OFF_MAGIC, FSPLUS_SUPERBLOCK_MAGIC, 8);
    fsplus_put_u32(block + SB_OFF_VERSION, 1u);
    fsplus_put_u32(block + SB_OFF_VERSION + 4, 0u);
    fsplus_put_u32(block + SB_OFF_BLOCK_SIZE, FSPLUS_BLOCK_SIZE);
    fsplus_put_u32(block + SB_OFF_FLAGS, 0u);
    fsplus_put_u64(block + SB_OFF_TOTAL, total_blocks);
    fsplus_put_u64(block + SB_OFF_FREE, free_blocks);
    fsplus_put_u64(block + SB_OFF_ROOT_BLOCK, root_tree_block);
    fsplus_put_u16(block + SB_OFF_ROOT_LEVEL, root_tree_level);
    fsplus_put_u32(block + SB_OFF_CSUM_TYPE, 1u);
    fsplus_put_u64(block + SB_OFF_GENERATION, generation);
    fsplus_put_u64(block + SB_OFF_ROOT_OWNER, 0u);
    memcpy(block + SB_OFF_UUID, uuid, 16);
    if (label != NULL) {
        index = 0;
        while (index < 16 && label[index] != '\0') {
            block[SB_OFF_LABEL + index] = (uint8_t)label[index];
            ++index;
        }
    }
    fsplus_put_u32(block + SB_OFF_INCOMPAT, incompat);
    fsplus_put_u32(block + SB_OFF_RO_COMPAT, ro_compat);
    fsplus_put_u32(block + SB_OFF_COMPAT, compat);
    fsplus_put_u32(block + SB_OFF_SLOT, slot);
    fsplus_put_u32(block + SB_OFF_AG_SIZE, ag_size_blocks);
    fsplus_put_u32(block + SB_OFF_AG_COUNT, ag_count);
    fsplus_put_u32(block + SB_OFF_CHECKSUM, 0u);

    checksum = fsplus_crc32c_seeded(NULL, 0, block, SB_CHECKSUM_COVER);
    fsplus_put_u32(block + SB_OFF_CHECKSUM, checksum);
    return 0;
}

/* Validate a superblock block and copy its fields. */
int fsplus_superblock_validate(const uint8_t block[FSPLUS_BLOCK_SIZE],
                               struct fsplus_superblock_view *view)
{
    uint32_t stored_checksum;
    uint32_t calculated;
    uint32_t generation_low;
    size_t index;

    if (block == NULL || view == NULL) {
        return -1;
    }
    if (memcmp(block + SB_OFF_MAGIC, FSPLUS_SUPERBLOCK_MAGIC, 8) != 0 ||
        fsplus_get_u32(block + SB_OFF_VERSION) != 1u ||
        fsplus_get_u32(block + SB_OFF_VERSION + 4) != 0u ||
        fsplus_get_u32(block + SB_OFF_BLOCK_SIZE) != FSPLUS_BLOCK_SIZE ||
        fsplus_get_u32(block + SB_OFF_CSUM_TYPE) != 1u) {
        return -1;
    }

    stored_checksum = fsplus_get_u32(block + SB_OFF_CHECKSUM);
    calculated = fsplus_crc32c_seeded(NULL, 0, block, SB_CHECKSUM_COVER);
    if (stored_checksum != calculated) {
        return -1;
    }

    view->flags = fsplus_get_u32(block + SB_OFF_FLAGS);
    view->total_blocks = fsplus_get_u64(block + SB_OFF_TOTAL);
    view->free_blocks = fsplus_get_u64(block + SB_OFF_FREE);
    view->root_tree_block = fsplus_get_u64(block + SB_OFF_ROOT_BLOCK);
    view->root_tree_level = fsplus_get_u16(block + SB_OFF_ROOT_LEVEL);
    view->generation = fsplus_get_u64(block + SB_OFF_GENERATION);
    view->incompat = fsplus_get_u32(block + SB_OFF_INCOMPAT);
    view->ro_compat = fsplus_get_u32(block + SB_OFF_RO_COMPAT);
    view->compat = fsplus_get_u32(block + SB_OFF_COMPAT);
    view->slot = fsplus_get_u32(block + SB_OFF_SLOT);
    view->ag_size_blocks = fsplus_get_u32(block + SB_OFF_AG_SIZE);
    view->ag_count = fsplus_get_u32(block + SB_OFF_AG_COUNT);
    memcpy(view->uuid, block + SB_OFF_UUID, 16);
    for (index = 0; index < 16; ++index) {
        view->label[index] = (char)block[SB_OFF_LABEL + index];
    }
    view->label[16] = '\0';

    generation_low = (uint32_t)(view->generation & 1u);
    if (view->total_blocks == 0 || view->free_blocks > view->total_blocks ||
        view->root_tree_block < 2 || view->root_tree_block >= view->total_blocks ||
        view->root_tree_level > FSPLUS_NODE_MAX_LEVEL ||
        view->ag_size_blocks == 0 || view->ag_count == 0 ||
        view->slot > 1 || view->slot != generation_low ||
        (view->incompat & FSPLUS_INCOMPAT_REQUIRED) != FSPLUS_INCOMPAT_REQUIRED) {
        return -1;
    }
    return 0;
}
