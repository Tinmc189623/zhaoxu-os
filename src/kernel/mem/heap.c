#include <console.h>
#include <io.h>
#include <kprintf.h>
#include <mem.h>
#include <spinlock.h>
#include <string.h>
#include <types.h>

#define HEAP_MAGIC 0x48454150u
#define HEAP_REGION_SIZE (16 * 1024 * 1024)
#define HEAP_ALIGN 16

struct heap_block {
    u32 magic;
    u32 free;
    u32 region;
    u32 pad;
    usize size;
    struct heap_block *next;
    struct heap_block *previous;
};

#define HEAP_GROW_MIN (1024 * 1024)

static struct heap_block *head;
static u64 heap_total;
static u64 heap_used;
static u64 heap_limit;
static u32 heap_region_next = 1;
static spinlock_t heap_lock = SPINLOCK_INIT;

/* Extend the kernel heap from the PMM when the free list cannot satisfy a
 * request.  The old implementation fixed the whole heap at 16 MiB and
 * returned NULL forever once fragmented or full.  Growth is bounded by a
 * quarter of usable RAM, not by a compile-time byte count. */
static int heap_grow(usize wanted)
{
    usize needed = (usize)ALIGN_UP(wanted + sizeof(struct heap_block),
                                   HEAP_ALIGN);
    usize grow = HEAP_GROW_MIN;
    if (grow < needed)
        grow = (usize)ALIGN_UP(needed, HEAP_GROW_MIN);
    u32 pages = (u32)(grow / PAGE_SIZE);
    u64 block_size = (u64)pages * PAGE_SIZE - sizeof(struct heap_block);

    u64 flags = spin_lock_irqsave(&heap_lock);
    if (!heap_limit || heap_total + block_size > heap_limit) {
        spin_unlock_irqrestore(&heap_lock, flags);
        return -1;
    }
    /* Reserve the accounting slot before dropping the lock; if the page
     * allocator fails, put it back. */
    heap_total += block_size;
    spin_unlock_irqrestore(&heap_lock, flags);

    u64 region = vmm_alloc_pages(pages);
    if (!region) {
        flags = spin_lock_irqsave(&heap_lock);
        heap_total -= block_size;
        spin_unlock_irqrestore(&heap_lock, flags);
        return -1;
    }

    struct heap_block *block = (struct heap_block *)(uintptr_t)region;
    block->magic = HEAP_MAGIC;
    block->free = 1;
    block->region = __atomic_fetch_add(&heap_region_next, 1,
                                       __ATOMIC_RELAXED);
    block->size = block_size;
    block->next = NULL;

    flags = spin_lock_irqsave(&heap_lock);
    struct heap_block *last = head;
    while (last->next)
        last = last->next;
    block->previous = last;
    last->next = block;
    spin_unlock_irqrestore(&heap_lock, flags);
    return 0;
}

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
    head->region = 0;
    head->size = HEAP_REGION_SIZE - sizeof(struct heap_block);
    head->next = NULL;
    head->previous = NULL;
    heap_total = head->size;
    heap_used = 0;
    heap_limit = pmm_managed_bytes() / 4;
    if (heap_limit < HEAP_REGION_SIZE)
        heap_limit = HEAP_REGION_SIZE;

    kprintf("[heap] %u KiB at 0x%llx, limit %llu MiB\n",
            HEAP_REGION_SIZE / 1024, region, heap_limit >> 20);
}

void *kmalloc(usize size)
{
    if (size == 0 || (heap_limit && (u64)size > heap_limit))
        return NULL;
    usize wanted = (usize)ALIGN_UP(size, HEAP_ALIGN);

    for (;;) {
        u64 flags = spin_lock_irqsave(&heap_lock);
        for (struct heap_block *block = head; block; block = block->next) {
            if (!block->free || block->size < wanted)
                continue;

            if (block->size >=
                wanted + sizeof(struct heap_block) + HEAP_ALIGN) {
                struct heap_block *split =
                    (struct heap_block *)((u8 *)(block + 1) + wanted);
                split->magic = HEAP_MAGIC;
                split->free = 1;
                split->region = block->region;
                split->size =
                    block->size - wanted - sizeof(struct heap_block);
                split->next = block->next;
                split->previous = block;
                if (split->next)
                    split->next->previous = split;
                block->next = split;
                block->size = wanted;
            }

            block->free = 0;
            heap_used += block->size;
            void *result = (void *)(block + 1);
            spin_unlock_irqrestore(&heap_lock, flags);
            return result;
        }
        spin_unlock_irqrestore(&heap_lock, flags);

        if (heap_grow(wanted) != 0)
            return NULL;
    }
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

    u64 flags = spin_lock_irqsave(&heap_lock);
    struct heap_block *block = (struct heap_block *)pointer - 1;
    if (block->magic != HEAP_MAGIC || block->free) {
        spin_unlock_irqrestore(&heap_lock, flags);
        return;
    }

    block->free = 1;
    if (heap_used >= block->size)
        heap_used -= block->size;

    /* Only coalesce within one vmm_alloc_pages() region: separate growth
     * chunks are linked in VA order but are not guaranteed contiguous. */
    if (block->next && block->next->free &&
        block->next->region == block->region) {
        struct heap_block *next = block->next;
        block->size += sizeof(struct heap_block) + next->size;
        block->next = next->next;
        if (block->next)
            block->next->previous = block;
    }
    if (block->previous && block->previous->free &&
        block->previous->region == block->region) {
        struct heap_block *previous = block->previous;
        previous->size += sizeof(struct heap_block) + block->size;
        previous->next = block->next;
        if (previous->next)
            previous->next->previous = previous;
    }
    spin_unlock_irqrestore(&heap_lock, flags);
}

void heap_get_stats(u64 *total, u64 *used, u64 *free_bytes)
{
    u64 flags = spin_lock_irqsave(&heap_lock);
    if (total)
        *total = heap_total;
    if (used)
        *used = heap_used;
    if (free_bytes)
        *free_bytes = heap_total - heap_used;
    spin_unlock_irqrestore(&heap_lock, flags);
}
