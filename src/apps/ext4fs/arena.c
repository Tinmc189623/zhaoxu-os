/*
 * Zhaoxu OS - ext4fs service arena.
 *
 * 2 MiB static arena with first-fit allocation and a coalescing free.  The
 * service is long-lived and the ext4 core allocates one block buffer per
 * metadata access, so a bump allocator would exhaust the arena during the
 * first few directory listings.
 */
#include <ext4fs.h>
#include <types.h>

#define ARENA_BYTES (2u * 1024u * 1024u)

struct arena_block {
    u32 size;
    u32 free;
    struct arena_block *next;
};

static u8 g_arena[ARENA_BYTES] __attribute__((aligned(16)));
static struct arena_block *g_head;

static void arena_init(void)
{
    g_head = (struct arena_block *)g_arena;
    g_head->size = ARENA_BYTES - (u32)sizeof(struct arena_block);
    g_head->free = 1;
    g_head->next = 0;
}

void *arena_alloc(u32 bytes)
{
    if (!g_head)
        arena_init();
    bytes = (bytes + 15u) & ~15u;
    for (struct arena_block *block = g_head; block; block = block->next) {
        if (!block->free || block->size < bytes)
            continue;
        if (block->size >= bytes + (u32)sizeof(struct arena_block) + 16u) {
            struct arena_block *rest =
                (struct arena_block *)((u8 *)(block + 1) + bytes);
            rest->size = block->size - bytes -
                         (u32)sizeof(struct arena_block);
            rest->free = 1;
            rest->next = block->next;
            block->next = rest;
            block->size = bytes;
        }
        block->free = 0;
        return block + 1;
    }
    return 0;
}

void arena_free(void *pointer)
{
    if (!pointer)
        return;
    struct arena_block *block = (struct arena_block *)pointer - 1;
    block->free = 1;

    /* Coalesce every adjacent free pair in one walk.  The inner loop keeps
     * merging so A|B|C frees collapse into one block instead of A|C. */
    struct arena_block *item = g_head;
    while (item) {
        while (item->free && item->next && item->next->free) {
            item->size += (u32)sizeof(struct arena_block) + item->next->size;
            item->next = item->next->next;
        }
        item = item->next;
    }
}
