#include <boot.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define MAX_PAGES (4ULL * 1024 * 1024 * 1024 / PAGE_SIZE)

struct memory_descriptor {
    u32 type;
    u32 pad;
    u64 physical_start;
    u64 virtual_start;
    u64 number_of_pages;
    u64 attribute;
} __attribute__((packed));

static u64 bitmap[MAX_PAGES / 64];
static u64 free_pages_count;
static u64 managed_pages_count;
static u64 search_hint = 0x100;

static inline int bitmap_test(u64 page)
{
    return (bitmap[page / 64] >> (page % 64)) & 1;
}

static inline void bitmap_set(u64 page)
{
    bitmap[page / 64] |= 1ULL << (page % 64);
}

static inline void bitmap_clear(u64 page)
{
    bitmap[page / 64] &= ~(1ULL << (page % 64));
}

void pmm_reserve_range(u64 physical, u64 size)
{
    u64 first = physical / PAGE_SIZE;
    u64 last = (physical + size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (last > MAX_PAGES)
        last = MAX_PAGES;
    for (u64 page = first; page < last; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            if (free_pages_count > 0)
                free_pages_count--;
        }
    }
}

void pmm_init(const struct boot_info *bi)
{
    memset(bitmap, 0xFF, sizeof(bitmap));
    free_pages_count = 0;
    managed_pages_count = 0;

    u64 descriptor_count = bi->memory_map_size / bi->memory_map_desc_size;
    const u8 *cursor = (const u8 *)bi->memory_map;

    for (u64 i = 0; i < descriptor_count; i++) {
        const struct memory_descriptor *desc =
            (const struct memory_descriptor *)(cursor + i * bi->memory_map_desc_size);
        if (desc->type != 7)
            continue;

        u64 first = desc->physical_start / PAGE_SIZE;
        u64 end = (desc->physical_start + desc->number_of_pages * PAGE_SIZE) / PAGE_SIZE;
        if (end > MAX_PAGES)
            end = MAX_PAGES;
        for (u64 page = first; page < end; page++) {
            if (bitmap_test(page)) {
                bitmap_clear(page);
                free_pages_count++;
                managed_pages_count++;
            }
        }
    }

    pmm_reserve_range(0, 0x100000);              /* real-mode / firmware area */
    pmm_reserve_range(bi->kernel_phys_base, bi->kernel_map_size);
    pmm_reserve_range(bi->page_table_root, 8 * PAGE_SIZE);

    search_hint = 0x100;

    kprintf("[pmm ] %llu MiB managed, %llu MiB free\n",
            (managed_pages_count * PAGE_SIZE) >> 20,
            (free_pages_count * PAGE_SIZE) >> 20);
}

u64 pmm_alloc_page(void)
{
    for (u64 page = search_hint; page < MAX_PAGES; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            if (free_pages_count > 0)
                free_pages_count--;
            search_hint = page + 1;
            u64 physical = page * PAGE_SIZE;
            memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);
            return physical;
        }
    }
    return 0;
}

void pmm_free_page(u64 physical)
{
    u64 page = physical / PAGE_SIZE;
    if (page >= MAX_PAGES || page < 0x100)
        return;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages_count++;
        if (page < search_hint)
            search_hint = page;
    }
}

void pmm_get_stats(struct pmm_stats *stats)
{
    stats->managed_pages = managed_pages_count;
    stats->free_pages = free_pages_count;
    stats->used_pages = managed_pages_count - free_pages_count;
}
