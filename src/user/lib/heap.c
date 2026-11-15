/* Free-list heap for Ring3 services that do not use the Wayland allocator.
 *
 * FreeType allocates and releases raster and table blocks on every glyph.
 * A bump pointer that never returns memory exhausts the desktop image while
 * text is being drawn, so this heap splits, frees and coalesces.
 */
#include <stddef.h>
#include <string.h>

#define HEAP_BYTES (8u * 1024u * 1024u)
#define HEAP_ALIGN 16u

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

static unsigned char heap_storage[HEAP_BYTES] __attribute__((aligned(16)));
static struct heap_block *heap_first;

static size_t heap_align(size_t value)
{
    return (value + HEAP_ALIGN - 1u) & ~(size_t)(HEAP_ALIGN - 1u);
}

static void heap_setup(void)
{
    if (heap_first)
        return;
    heap_first = (struct heap_block *)heap_storage;
    heap_first->size = HEAP_BYTES;
    heap_first->free = 1;
    heap_first->next = 0;
}

static void heap_coalesce(void)
{
    struct heap_block *block = heap_first;
    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += block->next->size;
            block->next = block->next->next;
            continue;
        }
        block = block->next;
    }
}

void *malloc(size_t size)
{
    heap_setup();
    if (size == 0)
        size = 1;
    size = heap_align(size);
    for (struct heap_block *block = heap_first; block; block = block->next) {
        if (!block->free || block->size < size + sizeof(*block))
            continue;
        if (block->size >= size + sizeof(*block) * 2 + HEAP_ALIGN) {
            struct heap_block *split =
                (struct heap_block *)((unsigned char *)block +
                                      sizeof(*block) + size);
            split->size = block->size - sizeof(*block) - size;
            split->free = 1;
            split->next = block->next;
            block->next = split;
            block->size = sizeof(*block) + size;
        }
        block->free = 0;
        return (unsigned char *)block + sizeof(*block);
    }
    return 0;
}

void free(void *pointer)
{
    if (!pointer)
        return;
    struct heap_block *block =
        (struct heap_block *)((unsigned char *)pointer - sizeof(*block));
    block->free = 1;
    heap_coalesce();
}

void *calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-1 / size)
        return 0;
    size_t total = count * size;
    void *result = malloc(total);
    if (result)
        memset(result, 0, total);
    return result;
}

void *realloc(void *pointer, size_t size)
{
    if (!pointer)
        return malloc(size);
    if (size == 0) {
        free(pointer);
        return 0;
    }
    struct heap_block *block =
        (struct heap_block *)((unsigned char *)pointer - sizeof(*block));
    size_t old = block->size - sizeof(*block);
    if (old >= size)
        return pointer;
    void *result = malloc(size);
    if (!result)
        return 0;
    memcpy(result, pointer, old);
    free(pointer);
    return result;
}
