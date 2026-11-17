/* Zhaoxu OS - FS+ on-disk format core. */
#ifndef ZHAOXU_FSPLUS_FORMAT_H
#define ZHAOXU_FSPLUS_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define FSPLUS_BLOCK_SIZE 4096u
#define FSPLUS_SUPERBLOCK_MAGIC "FS+1\0\0\0\0"
#define FSPLUS_NODE_MAGIC "FS+N"
#define FSPLUS_NODE_HEADER_SIZE 40u
#define FSPLUS_NODE_TRAILER_SIZE 16u
#define FSPLUS_NODE_ITEMS_SIZE \
    (FSPLUS_BLOCK_SIZE - FSPLUS_NODE_HEADER_SIZE - FSPLUS_NODE_TRAILER_SIZE)
#define FSPLUS_NODE_TAG_OFFSET (FSPLUS_BLOCK_SIZE - FSPLUS_NODE_TRAILER_SIZE)
#define FSPLUS_NODE_MAX_LEVEL 8u

#define FSPLUS_INCOMPAT_COW        (1u << 0)
#define FSPLUS_INCOMPAT_BTREE      (1u << 1)
#define FSPLUS_INCOMPAT_CHECKSUM   (1u << 2)
#define FSPLUS_INCOMPAT_64BIT      (1u << 3)
#define FSPLUS_INCOMPAT_AG         (1u << 4)
#define FSPLUS_INCOMPAT_EPOCH      (1u << 5)
#define FSPLUS_INCOMPAT_REQUIRED   (0x3Fu)

#define FSPLUS_RO_INLINE_DATA      (1u << 0)
#define FSPLUS_RO_SPARSE           (1u << 1)
#define FSPLUS_RO_XATTR            (1u << 2)
#define FSPLUS_RO_DATA_CSUM        (1u << 3)
#define FSPLUS_RO_REFLINK          (1u << 4)
#define FSPLUS_RO_SNAPSHOT         (1u << 5)
#define FSPLUS_RO_COMPRESS         (1u << 6)
#define FSPLUS_RO_ENCRYPT          (1u << 7)
#define FSPLUS_RO_QUOTA            (1u << 8)
#define FSPLUS_RO_BASE_REQUIRED    (0x3Fu)

struct fsplus_superblock_view {
    uint32_t flags;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t root_tree_block;
    uint16_t root_tree_level;
    uint64_t generation;
    uint32_t incompat;
    uint32_t ro_compat;
    uint32_t compat;
    uint32_t slot;
    uint32_t ag_size_blocks;
    uint32_t ag_count;
    uint8_t uuid[16];
    char label[17];
};

struct fsplus_node_view {
    uint8_t level;
    uint16_t nritems;
    uint32_t free_bytes;
    uint64_t generation;
    uint64_t owner;
    uint32_t key_id;
};

/* Compute CRC32C with the FS+ reflected polynomial and initial/final XOR. */
uint32_t fsplus_crc32c_seeded(const void *seed, size_t seed_len,
                              const void *data, size_t data_len);

/* Initialize a superblock block. Returns 0 on success. */
int fsplus_superblock_init(uint8_t block[FSPLUS_BLOCK_SIZE], uint64_t total_blocks,
                           uint64_t free_blocks, uint64_t root_tree_block,
                           uint16_t root_tree_level, uint64_t generation,
                           uint32_t slot, uint32_t ag_size_blocks,
                           uint32_t ag_count, const uint8_t uuid[16],
                           const char *label, uint32_t incompat,
                           uint32_t ro_compat, uint32_t compat);

/* Validate a superblock block and copy its fields. Returns 0 on success. */
int fsplus_superblock_validate(const uint8_t block[FSPLUS_BLOCK_SIZE],
                               struct fsplus_superblock_view *view);

/* Initialize an empty B+tree node. Returns 0 on success. */
int fsplus_node_init(uint8_t block[FSPLUS_BLOCK_SIZE], uint8_t level,
                     uint64_t generation, uint64_t owner, uint32_t key_id);

/* Append one packed item to a node. Returns 0 on success. */
int fsplus_node_add_item(uint8_t block[FSPLUS_BLOCK_SIZE], const void *key,
                         uint16_t key_len, const void *value,
                         uint16_t value_len);

/* Seal the node checksum and clear the unused authentication tag. */
int fsplus_node_seal(uint8_t block[FSPLUS_BLOCK_SIZE], const uint8_t uuid[16]);

/* Validate a node, its checksum and optional expected owner. Returns 0. */
int fsplus_node_validate(const uint8_t block[FSPLUS_BLOCK_SIZE],
                         const uint8_t uuid[16], uint64_t expected_owner,
                         int check_owner, struct fsplus_node_view *view);

#endif
