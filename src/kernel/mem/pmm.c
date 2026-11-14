#include <boot.h>
#include <kprintf.h>
#include <mem.h>
#include <spinlock.h>
#include <string.h>
#include <types.h>

/* 64 GiB of physical memory needs a 2 MiB bitmap; going further would
 * bloat the kernel .bss for little gain on current desktop hardware. */
#define PMM_MAX_PHYS (64ULL * 1024 * 1024 * 1024)
#define MAX_PAGES    (PMM_MAX_PHYS / PAGE_SIZE)
#define PMM_INITIAL_LIMIT (4ULL * 1024 * 1024 * 1024)

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
static u64 managed_limit = PMM_INITIAL_LIMIT;
static const struct boot_info *boot_info;
static spinlock_t pmm_lock = SPINLOCK_INIT;

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

    u64 flags = spin_lock_irqsave(&pmm_lock);
    for (u64 page = first; page < last; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            if (free_pages_count > 0)
                free_pages_count--;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

void pmm_init(const struct boot_info *bi)
{
    memset(bitmap, 0xFF, sizeof(bitmap));
    free_pages_count = 0;
    managed_pages_count = 0;
    managed_limit = PMM_INITIAL_LIMIT;
    boot_info = bi;

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
        if (end > managed_limit / PAGE_SIZE)
            end = managed_limit / PAGE_SIZE;
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
    /* The loader reserves 32 pages for its page tables (PAGE_TABLE_PAGES). */
    pmm_reserve_range(bi->page_table_root, 32 * PAGE_SIZE);

    search_hint = 0x100;

    kprintf("[pmm ] %llu MiB managed, %llu MiB free\n",
            (managed_pages_count * PAGE_SIZE) >> 20,
            (free_pages_count * PAGE_SIZE) >> 20);
}

/* Admit usable RAM above the initial 4 GiB watermark.  vmm_init() calls this
 * only after it has identity-mapped the range with 2 MiB pages, so the
 * allocator can never hand out a physical page the CPU cannot address. */
void pmm_extend(u64 limit)
{
    if (!boot_info || limit <= managed_limit)
        return;
    if (limit > PMM_MAX_PHYS)
        limit = PMM_MAX_PHYS;

    u64 descriptor_count =
        boot_info->memory_map_size / boot_info->memory_map_desc_size;
    const u8 *cursor = (const u8 *)boot_info->memory_map;
    u64 first_new = managed_limit / PAGE_SIZE;
    u64 last_new = limit / PAGE_SIZE;

    u64 flags = spin_lock_irqsave(&pmm_lock);
    for (u64 i = 0; i < descriptor_count; i++) {
        const struct memory_descriptor *desc =
            (const struct memory_descriptor *)(cursor +
                                               i * boot_info->memory_map_desc_size);
        if (desc->type != 7)
            continue;

        u64 first = desc->physical_start / PAGE_SIZE;
        u64 end = (desc->physical_start + desc->number_of_pages * PAGE_SIZE) /
                  PAGE_SIZE;
        if (first < first_new)
            first = first_new;
        if (end > last_new)
            end = last_new;
        for (u64 page = first; page < end; page++) {
            if (bitmap_test(page)) {
                bitmap_clear(page);
                free_pages_count++;
                managed_pages_count++;
            }
        }
    }
    managed_limit = limit;
    search_hint = 0x100;
    spin_unlock_irqrestore(&pmm_lock, flags);
    kprintf("[pmm ] high memory above 4 GiB admitted: %llu MiB managed, "
            "%llu MiB free\n",
            (managed_pages_count * PAGE_SIZE) >> 20,
            (free_pages_count * PAGE_SIZE) >> 20);
}

u64 pmm_alloc_page(void)
{
    u64 physical = 0;

    u64 flags = spin_lock_irqsave(&pmm_lock);
    for (u64 page = search_hint; page < MAX_PAGES; page++) {
        if (!bitmap_test(page)) {
            bitmap_set(page);
            if (free_pages_count > 0)
                free_pages_count--;
            search_hint = page + 1;
            physical = page * PAGE_SIZE;
            break;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);

    if (physical)
        memset((void *)(uintptr_t)physical, 0, PAGE_SIZE);
    return physical;
}

void pmm_free_page(u64 physical)
{
    u64 page = physical / PAGE_SIZE;
    if (page >= MAX_PAGES || page < 0x100)
        return;

    u64 flags = spin_lock_irqsave(&pmm_lock);
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages_count++;
        if (page < search_hint)
            search_hint = page;
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

u64 pmm_managed_bytes(void)
{
    return managed_pages_count * PAGE_SIZE;
}

void pmm_get_stats(struct pmm_stats *stats)
{
    if (!stats)
        return;

    u64 flags = spin_lock_irqsave(&pmm_lock);
    stats->managed_pages = managed_pages_count;
    stats->free_pages = free_pages_count;
    stats->used_pages = managed_pages_count - free_pages_count;
    spin_unlock_irqrestore(&pmm_lock, flags);
}
