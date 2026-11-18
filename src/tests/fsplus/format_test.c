/* Zhaoxu OS - FS+ host format tests. */
#include "fsplus_format.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #condition);                                            \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static int test_crc32c(void)
{
    static const char vector[] = "123456789";
    uint32_t crc = fsplus_crc32c_seeded(NULL, 0, vector, 9);

    CHECK(crc == 0xE3069283u);
    return 0;
}

static int test_superblock(void)
{
    uint8_t block[FSPLUS_BLOCK_SIZE];
    uint8_t broken[FSPLUS_BLOCK_SIZE];
    uint8_t uuid[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
    };
    struct fsplus_superblock_view view;

    CHECK(fsplus_superblock_init(block, 262144, 200000, 16, 0, 1, 1,
                                 262144, 1, uuid, "FS+",
                                 FSPLUS_INCOMPAT_REQUIRED,
                                 FSPLUS_RO_BASE_REQUIRED, 0) == 0);
    CHECK(fsplus_superblock_validate(block, &view) == 0);
    CHECK(view.total_blocks == 262144);
    CHECK(view.free_blocks == 200000);
    CHECK(view.root_tree_block == 16);
    CHECK(view.root_tree_level == 0);
    CHECK(view.generation == 1);
    CHECK(view.slot == 1);
    CHECK(view.ag_size_blocks == 262144);
    CHECK(view.ag_count == 1);
    CHECK(memcmp(view.uuid, uuid, 16) == 0);
    CHECK(strcmp(view.label, "FS+") == 0);

    memcpy(broken, block, sizeof(broken));
    broken[0x18] ^= 0x01u;
    CHECK(fsplus_superblock_validate(broken, &view) != 0);
    return 0;
}

static int test_node(void)
{
    uint8_t block[FSPLUS_BLOCK_SIZE];
    uint8_t broken[FSPLUS_BLOCK_SIZE];
    uint8_t uuid[16] = {0};
    static const uint8_t key[] = {'a', 'b', 'c'};
    static const uint8_t value[] = {1, 2, 3, 4, 5};
    struct fsplus_node_view view;

    uuid[0] = 0x5A;
    CHECK(fsplus_node_init(block, 0, 2, 1, 0) == 0);
    CHECK(fsplus_node_add_item(block, key, sizeof(key), value,
                               sizeof(value)) == 0);
    CHECK(fsplus_node_seal(block, uuid) == 0);
    CHECK(fsplus_node_validate(block, uuid, 1, 1, &view) == 0);
    CHECK(view.level == 0);
    CHECK(view.nritems == 1);
    CHECK(view.generation == 2);
    CHECK(view.owner == 1);
    CHECK(view.key_id == 0);
    CHECK(view.free_bytes == FSPLUS_NODE_ITEMS_SIZE -
                              (8u + sizeof(key) + sizeof(value)));

    memcpy(broken, block, sizeof(broken));
    broken[0x0C] ^= 0x01u;
    CHECK(fsplus_node_validate(broken, uuid, 1, 1, &view) != 0);

    CHECK(fsplus_node_validate(block, uuid, 9, 1, &view) != 0);
    CHECK(fsplus_node_validate(block, uuid, 9, 0, &view) == 0);
    return 0;
}

int main(void)
{
    if (test_crc32c() != 0 || test_superblock() != 0 || test_node() != 0) {
        return 1;
    }
    printf("FS+ format test: OK\n");
    return 0;
}
