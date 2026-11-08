#include <boot.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define HEAP_VIRTUAL_BASE 0xffffffff90000000ULL
#define PTE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

static u64 *pml4;
static u64 heap_cursor = HEAP_VIRTUAL_BASE;

static inline u64 *table_from_entry(u64 entry)
{
    return (u64 *)(entry & PTE_ADDRESS_MASK);
}

static u64 *ensure_table(u64 *table, u32 index, int user)
{
    u64 entry = table[index];
    if (!(entry & VMM_PRESENT)) {
        u64 physical = pmm_alloc_page();
        if (!physical)
            return NULL;
        table[index] = physical | VMM_PRESENT | VMM_WRITABLE |
                       (user ? VMM_USER : 0);
        return (u64 *)(uintptr_t)physical;
    }
    if (entry & (1ULL << 7))
        return NULL;
    if (user && !(entry & VMM_USER))
        table[index] |= VMM_USER;
    return table_from_entry(entry);
}

int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);
    int user = (flags & VMM_USER) != 0;

    u64 *pdpt = ensure_table(pml4, pml4_index, user);
    if (!pdpt)
        return -1;
    u64 *pd = ensure_table(pdpt, pdpt_index, user);
    if (!pd)
        return -1;
    u64 *pt = ensure_table(pd, pd_index, user);
    if (!pt)
        return -1;

    pt[pt_index] = (physical_address & PTE_ADDRESS_MASK) |
                   (flags & 0xFFF) | VMM_PRESENT;
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return 0;
}

u64 vmm_get_physical(u64 virtual_address)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);

    u64 entry = pml4[pml4_index];
    if (!(entry & VMM_PRESENT))
        return 0;
    u64 *pdpt = table_from_entry(entry);
    entry = pdpt[pdpt_index];
    if (!(entry & VMM_PRESENT) || (entry & (1ULL << 7)))
        return 0;
    u64 *pd = table_from_entry(entry);
    entry = pd[pd_index];
    if (!(entry & VMM_PRESENT))
        return 0;
    if (entry & (1ULL << 7))
        return (entry & 0x000FFFFFFFE00000ULL) + (virtual_address & 0x1FFFFF);
    u64 *pt = table_from_entry(entry);
    entry = pt[pt_index];
    if (!(entry & VMM_PRESENT))
        return 0;
    return (entry & PTE_ADDRESS_MASK) + (virtual_address & 0xFFF);
}

u64 vmm_unmap_page(u64 virtual_address)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);

    u64 entry = pml4[pml4_index];
    if (!(entry & VMM_PRESENT))
        return 0;
    u64 *pdpt = table_from_entry(entry);
    entry = pdpt[pdpt_index];
    if (!(entry & VMM_PRESENT) || (entry & (1ULL << 7)))
        return 0;
    u64 *pd = table_from_entry(entry);
    entry = pd[pd_index];
    if (!(entry & VMM_PRESENT) || (entry & (1ULL << 7)))
        return 0;
    u64 *pt = table_from_entry(entry);
    entry = pt[pt_index];
    if (!(entry & VMM_PRESENT))
        return 0;

    u64 physical = entry & PTE_ADDRESS_MASK;
    pt[pt_index] = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return physical;
}

int vmm_page_is_user(u64 virtual_address)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);

    u64 entry = pml4[pml4_index];
    if ((entry & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER))
        return 0;
    u64 *pdpt = table_from_entry(entry);
    entry = pdpt[pdpt_index];
    if ((entry & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER))
        return 0;
    if (entry & (1ULL << 7))
        return 1;
    u64 *pd = table_from_entry(entry);
    entry = pd[pd_index];
    if ((entry & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER))
        return 0;
    if (entry & (1ULL << 7))
        return 1;
    u64 *pt = table_from_entry(entry);
    entry = pt[pt_index];
    return (entry & (VMM_PRESENT | VMM_USER)) == (VMM_PRESENT | VMM_USER);
}

u64 vmm_alloc_pages(u32 count)
{
    if (count == 0)
        return 0;
    u64 start = heap_cursor;
    u64 cursor = start;
    for (u32 i = 0; i < count; i++) {
        u64 physical = pmm_alloc_page();
        if (!physical)
            return 0;
        if (vmm_map_page(cursor, physical, VMM_PRESENT | VMM_WRITABLE) != 0)
            return 0;
        cursor += PAGE_SIZE;
    }
    heap_cursor = cursor;
    return start;
}

u64 vmm_heap_cursor(void)
{
    return heap_cursor;
}

void vmm_init(const struct boot_info *bi)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4 = (u64 *)(uintptr_t)(bi->page_table_root ? bi->page_table_root : cr3);
    heap_cursor = HEAP_VIRTUAL_BASE;
    kprintf("[vmm ] pml4 @ 0x%llx, heap base 0x%llx\n",
            (u64)(uintptr_t)pml4, HEAP_VIRTUAL_BASE);
}
