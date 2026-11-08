#include <console.h>
#include <io.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define HEAP_MAGIC 0x48454150u
#define HEAP_REGION_SIZE (4 * 1024 * 1024)
#define HEAP_ALIGN 16

struct heap_block {
    u32 magic;
    u32 free;
    usize size;
    struct heap_block *next;
    struct heap_block *previous;
};

static struct heap_block *head;
static u64 heap_total;
static u64 heap_used;

void heap_init(void)
{
    u64 region = vmm_alloc_pages(HEAP_REGION_SIZE / PAGE_SIZE);
    if (!region) {
        kprintf("[heap] FATAL: cannot reserve heap region\n");
        for (;;)
            hlt();
    }

    head = (struct heap_block *)(uintptr_t)region;
    head->magic = HEAP_MAGIC;
    head->free = 1;
    head->size = HEAP_REGION_SIZE - sizeof(struct heap_block);
    head->next = NULL;
    head->previous = NULL;
    heap_total = head->size;
    heap_used = 0;

    kprintf("[heap] %u KiB at 0x%llx\n", HEAP_REGION_SIZE / 1024, region);
}

void *kmalloc(usize size)
{
    if (size == 0)
        return NULL;
    usize wanted = (usize)ALIGN_UP(size, HEAP_ALIGN);

    for (struct heap_block *block = head; block; block = block->next) {
        if (!block->free || block->size < wanted)
            continue;

        if (block->size >= wanted + sizeof(struct heap_block) + HEAP_ALIGN) {
            struct heap_block *split =
                (struct heap_block *)((u8 *)(block + 1) + wanted);
            split->magic = HEAP_MAGIC;
            split->free = 1;
            split->size = block->size - wanted - sizeof(struct heap_block);
            split->next = block->next;
            split->previous = block;
            if (split->next)
                split->next->previous = split;
            block->next = split;
            block->size = wanted;
        }

        block->free = 0;
        heap_used += block->size;
        return (void *)(block + 1);
    }
    return NULL;
}

void *kzalloc(usize size)
{
    void *ptr = kmalloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void kfree(void *pointer)
{
    if (!pointer)
        return;
    struct heap_block *block = (struct heap_block *)pointer - 1;
    if (block->magic != HEAP_MAGIC || block->free)
        return;

    block->free = 1;
    if (heap_used >= block->size)
        heap_used -= block->size;

    if (block->next && block->next->free) {
        struct heap_block *next = block->next;
        block->size += sizeof(struct heap_block) + next->size;
        block->next = next->next;
        if (block->next)
            block->next->previous = block;
    }
    if (block->previous && block->previous->free) {
        struct heap_block *previous = block->previous;
        previous->size += sizeof(struct heap_block) + block->size;
        previous->next = block->next;
        if (previous->next)
            previous->next->previous = previous;
    }
}

void heap_get_stats(u64 *total, u64 *used, u64 *free_bytes)
{
    if (total)
        *total = heap_total;
    if (used)
        *used = heap_used;
    if (free_bytes)
        *free_bytes = heap_total - heap_used;
}
