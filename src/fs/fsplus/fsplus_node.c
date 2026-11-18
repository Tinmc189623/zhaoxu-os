/* Zhaoxu OS - FS+ B+tree node encoding. */
#include "fsplus_endian.h"
#include "fsplus_format.h"

#include <string.h>

#define NODE_OFF_MAGIC      0x000u
#define NODE_OFF_VERSION    0x004u
#define NODE_OFF_LEVEL      0x005u
#define NODE_OFF_NRITEMS    0x006u
#define NODE_OFF_FREE       0x008u
#define NODE_OFF_CHECKSUM   0x00Cu
#define NODE_OFF_GENERATION 0x010u
#define NODE_OFF_OWNER      0x018u
#define NODE_OFF_KEY_ID     0x020u
#define NODE_OFF_ITEMS      0x028u
#define NODE_ITEM_HEADER    8u

static void build_checksum_seed(const uint8_t uuid[16], uint64_t owner,
                                uint8_t seed[24])
{
    memcpy(seed, uuid, 16);
    fsplus_put_u64(seed + 16, owner);
}

/* Initialize an empty B+tree node. */
int fsplus_node_init(uint8_t block[FSPLUS_BLOCK_SIZE], uint8_t level,
                     uint64_t generation, uint64_t owner, uint32_t key_id)
{
    if (block == NULL || level > FSPLUS_NODE_MAX_LEVEL) {
        return -1;
    }
    memset(block, 0, FSPLUS_BLOCK_SIZE);
    memcpy(block + NODE_OFF_MAGIC, FSPLUS_NODE_MAGIC, 4);
    block[NODE_OFF_VERSION] = 1u;
    block[NODE_OFF_LEVEL] = level;
    fsplus_put_u16(block + NODE_OFF_NRITEMS, 0u);
    fsplus_put_u32(block + NODE_OFF_FREE, FSPLUS_NODE_ITEMS_SIZE);
    fsplus_put_u64(block + NODE_OFF_GENERATION, generation);
    fsplus_put_u64(block + NODE_OFF_OWNER, owner);
    fsplus_put_u32(block + NODE_OFF_KEY_ID, key_id);
    return 0;
}

/* Append one packed item to a node. */
int fsplus_node_add_item(uint8_t block[FSPLUS_BLOCK_SIZE], const void *key,
                         uint16_t key_len, const void *value,
                         uint16_t value_len)
{
    uint16_t nritems;
    uint32_t free_bytes;
    size_t needed;
    size_t cursor;

    if (block == NULL || (key_len != 0 && key == NULL) ||
        (value_len != 0 && value == NULL)) {
        return -1;
    }
    nritems = fsplus_get_u16(block + NODE_OFF_NRITEMS);
    free_bytes = fsplus_get_u32(block + NODE_OFF_FREE);
    if (nritems == UINT16_MAX || free_bytes > FSPLUS_NODE_ITEMS_SIZE) {
        return -1;
    }
    needed = NODE_ITEM_HEADER + (size_t)key_len + (size_t)value_len;
    if (needed > free_bytes) {
        return -1;
    }
    cursor = NODE_OFF_ITEMS +
             (FSPLUS_NODE_ITEMS_SIZE - (size_t)free_bytes);
    fsplus_put_u16(block + cursor, key_len);
    fsplus_put_u16(block + cursor + 2, value_len);
    fsplus_put_u16(block + cursor + 4, 0u);
    fsplus_put_u16(block + cursor + 6, 0u);
    cursor += NODE_ITEM_HEADER;
    if (key_len != 0) {
        memcpy(block + cursor, key, key_len);
        cursor += key_len;
    }
    if (value_len != 0) {
        memcpy(block + cursor, value, value_len);
    }
    fsplus_put_u16(block + NODE_OFF_NRITEMS, (uint16_t)(nritems + 1u));
    fsplus_put_u32(block + NODE_OFF_FREE, free_bytes - (uint32_t)needed);
    return 0;
}

/* Seal the node checksum. The authentication tag is left untouched. */
int fsplus_node_seal(uint8_t block[FSPLUS_BLOCK_SIZE], const uint8_t uuid[16])
{
    uint8_t seed[24];
    uint64_t owner;
    uint32_t checksum;

    if (block == NULL || uuid == NULL) {
        return -1;
    }
    owner = fsplus_get_u64(block + NODE_OFF_OWNER);
    build_checksum_seed(uuid, owner, seed);
    fsplus_put_u32(block + NODE_OFF_CHECKSUM, 0u);
    checksum = fsplus_crc32c_seeded(seed, sizeof(seed), block, FSPLUS_BLOCK_SIZE);
    fsplus_put_u32(block + NODE_OFF_CHECKSUM, checksum);
    return 0;
}

/* Validate a node, its checksum and optionally its owner. */
int fsplus_node_validate(const uint8_t block[FSPLUS_BLOCK_SIZE],
                         const uint8_t uuid[16], uint64_t expected_owner,
                         int check_owner, struct fsplus_node_view *view)
{
    uint8_t copy[FSPLUS_BLOCK_SIZE];
    uint8_t seed[24];
    uint16_t nritems;
    uint32_t free_bytes;
    uint32_t stored_checksum;
    uint64_t owner;
    size_t cursor;
    size_t items_end;
    uint16_t index;

    if (block == NULL || uuid == NULL || view == NULL) {
        return -1;
    }
    if (memcmp(block + NODE_OFF_MAGIC, FSPLUS_NODE_MAGIC, 4) != 0 ||
        block[NODE_OFF_VERSION] != 1u ||
        block[NODE_OFF_LEVEL] > FSPLUS_NODE_MAX_LEVEL) {
        return -1;
    }
    nritems = fsplus_get_u16(block + NODE_OFF_NRITEMS);
    free_bytes = fsplus_get_u32(block + NODE_OFF_FREE);
    owner = fsplus_get_u64(block + NODE_OFF_OWNER);
    if (check_owner && owner != expected_owner) {
        return -1;
    }

    cursor = NODE_OFF_ITEMS;
    items_end = NODE_OFF_ITEMS + FSPLUS_NODE_ITEMS_SIZE;
    for (index = 0; index < nritems; ++index) {
        uint16_t key_len;
        uint16_t value_len;
        size_t needed;

        if (cursor + NODE_ITEM_HEADER > items_end) {
            return -1;
        }
        key_len = fsplus_get_u16(block + cursor);
        value_len = fsplus_get_u16(block + cursor + 2);
        needed = NODE_ITEM_HEADER + (size_t)key_len + (size_t)value_len;
        if (needed > items_end - cursor) {
            return -1;
        }
        cursor += needed;
    }
    if ((uint32_t)(items_end - cursor) != free_bytes) {
        return -1;
    }

    memcpy(copy, block, FSPLUS_BLOCK_SIZE);
    stored_checksum = fsplus_get_u32(copy + NODE_OFF_CHECKSUM);
    fsplus_put_u32(copy + NODE_OFF_CHECKSUM, 0u);
    build_checksum_seed(uuid, owner, seed);
    if (stored_checksum !=
        fsplus_crc32c_seeded(seed, sizeof(seed), copy, FSPLUS_BLOCK_SIZE)) {
        return -1;
    }

    view->level = block[NODE_OFF_LEVEL];
    view->nritems = nritems;
    view->free_bytes = free_bytes;
    view->generation = fsplus_get_u64(block + NODE_OFF_GENERATION);
    view->owner = owner;
    view->key_id = fsplus_get_u32(block + NODE_OFF_KEY_ID);
    return 0;
}
