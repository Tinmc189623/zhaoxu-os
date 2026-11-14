/*
 * Zhaoxu OS - page-table / MMU driver.
 *
 * A four-level x86-64 walker that works on any CR3: map, unmap, protect and
 * translate 4 KiB pages.  Intermediate 1 GiB and 2 MiB leaves are split on
 * demand, so a driver can safely install a 4 KiB mapping inside a region the
 * boot loader mapped with large pages.  All page tables live below the
 * kernel's identity window, which makes their physical addresses directly
 * dereferenceable.
 */
#include <kprintf.h>
#include <mem.h>
#include <mmu.h>
#include <string.h>
#include <types.h>

#define PTE_ADDRESS 0x000FFFFFFFFFF000ull
#define PTE_LARGE (1ull << 7)
#define FLAGS_MASK 0x0000000000000FFFull

static u64 *table_at(u64 physical)
{
    return (u64 *)(uintptr_t)(physical & PTE_ADDRESS);
}

static u64 *allocate_table(void)
{
    u64 physical = pmm_alloc_page();
    if (!physical)
        return NULL;
    u64 *table = (u64 *)(uintptr_t)physical;
    memset(table, 0, PAGE_SIZE);
    return table;
}

/* Split a 1 GiB leaf into a page directory of 2 MiB leaves. */
static u64 *split_1g(u64 entry)
{
    u64 base = entry & 0x000FFFFFC0000000ull;
    u64 flags = (entry & FLAGS_MASK) & ~PTE_LARGE;
    u64 *directory = allocate_table();
    if (!directory)
        return NULL;
    for (u64 i = 0; i < 512; i++)
        directory[i] = (base + i * 0x200000ull) | flags | PTE_LARGE;
    return directory;
}

/* Split a 2 MiB leaf into a page table of 4 KiB entries. */
static u64 *split_2m(u64 entry)
{
    u64 base = entry & 0x000FFFFFFFE00000ull;
    u64 flags = (entry & FLAGS_MASK) & ~PTE_LARGE;
    u64 *table = allocate_table();
    if (!table)
        return NULL;
    for (u64 i = 0; i < 512; i++)
        table[i] = (base + i * PAGE_SIZE) | flags;
    return table;
}

/* Fetch (and optionally create) a child table for `table[index]`.  A large
 * leaf is split so the caller always receives a 512-entry table. */
static u64 *child_table(u64 *table, u32 index, int create, int user,
                        int level)
{
    u64 entry = table[index];
    if (entry & MMU_PRESENT) {
        if (entry & PTE_LARGE) {
            if (!create)
                return NULL;
            u64 *split = level == 3 ? split_1g(entry) : split_2m(entry);
            if (!split)
                return NULL;
            table[index] = ((u64)(uintptr_t)split & PTE_ADDRESS) |
                           ((entry & FLAGS_MASK) & ~PTE_LARGE) |
                           MMU_PRESENT;
            return split;
        }
        return table_at(entry);
    }
    if (!create)
        return NULL;
    u64 *fresh = allocate_table();
    if (!fresh)
        return NULL;
    table[index] = ((u64)(uintptr_t)fresh & PTE_ADDRESS) |
                   MMU_PRESENT | MMU_WRITABLE | (user ? MMU_USER : 0);
    return fresh;
}

/* Return the PTE for a virtual address, creating the path when requested. */
static u64 *page_entry(u64 cr3, u64 virtual_address, int create, int user)
{
    u64 *pml4 = table_at(cr3);
    u32 i4 = (u32)((virtual_address >> 39) & 0x1FF);
    u64 *pdpt = child_table(pml4, i4, create, user, 4);
    if (!pdpt)
        return NULL;
    u32 i3 = (u32)((virtual_address >> 30) & 0x1FF);
    u64 *pd = child_table(pdpt, i3, create, user, 3);
    if (!pd)
        return NULL;
    u32 i2 = (u32)((virtual_address >> 21) & 0x1FF);
    u64 *pt = child_table(pd, i2, create, user, 2);
    if (!pt)
        return NULL;
    return &pt[(virtual_address >> 12) & 0x1FF];
}

void mmu_init(void)
{
    kprintf("[mmu ] 4-level page tables active (CR3 0x%llx)\n",
            mmu_current_cr3());
}

u64 mmu_current_cr3(void)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PTE_ADDRESS;
}

int mmu_map_page(u64 cr3, u64 virtual_address, u64 physical_address,
                 u64 flags)
{
    int user = (flags & MMU_USER) != 0;
    u64 *entry = page_entry(cr3, virtual_address, 1, user);
    if (!entry)
        return -1;
    *entry = (physical_address & PTE_ADDRESS) |
             (flags & (FLAGS_MASK | MMU_NO_EXECUTE)) | MMU_PRESENT;
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return 0;
}

int mmu_map_range(u64 cr3, u64 virtual_address, u64 physical_address,
                  u64 pages, u64 flags)
{
    for (u64 i = 0; i < pages; i++) {
        if (mmu_map_page(cr3, virtual_address + i * PAGE_SIZE,
                         physical_address + i * PAGE_SIZE, flags) != 0)
            return -1;
    }
    return 0;
}

u64 mmu_unmap_page(u64 cr3, u64 virtual_address)
{
    u64 *entry = page_entry(cr3, virtual_address, 0, 0);
    if (!entry || !(*entry & MMU_PRESENT))
        return 0;
    u64 physical = *entry & PTE_ADDRESS;
    *entry = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return physical;
}

int mmu_protect_page(u64 cr3, u64 virtual_address, u64 flags)
{
    u64 *entry = page_entry(cr3, virtual_address, 0, 0);
    if (!entry || !(*entry & MMU_PRESENT))
        return -1;
    u64 physical = *entry & PTE_ADDRESS;
    *entry = physical | (flags & (FLAGS_MASK | MMU_NO_EXECUTE)) |
             MMU_PRESENT;
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return 0;
}

u64 mmu_translate(u64 cr3, u64 virtual_address)
{
    u64 *pml4 = table_at(cr3);
    u64 entry = pml4[(virtual_address >> 39) & 0x1FF];
    if (!(entry & MMU_PRESENT))
        return 0;
    u64 *pdpt = table_at(entry);
    entry = pdpt[(virtual_address >> 30) & 0x1FF];
    if (!(entry & MMU_PRESENT))
        return 0;
    if (entry & PTE_LARGE)
        return (entry & 0x000FFFFFC0000000ull) |
               (virtual_address & 0x3FFFFFFFull);
    u64 *pd = table_at(entry);
    entry = pd[(virtual_address >> 21) & 0x1FF];
    if (!(entry & MMU_PRESENT))
        return 0;
    if (entry & PTE_LARGE)
        return (entry & 0x000FFFFFFFE00000ull) |
               (virtual_address & 0x1FFFFFull);
    u64 *pt = table_at(entry);
    entry = pt[(virtual_address >> 12) & 0x1FF];
    if (!(entry & MMU_PRESENT))
        return 0;
    return (entry & PTE_ADDRESS) | (virtual_address & 0xFFFull);
}

int mmu_is_mapped(u64 cr3, u64 virtual_address)
{
    u64 *pml4 = table_at(cr3);
    u64 entry = pml4[(virtual_address >> 39) & 0x1FF];
    if (!(entry & MMU_PRESENT))
        return 0;
    u64 *pdpt = table_at(entry);
    entry = pdpt[(virtual_address >> 30) & 0x1FF];
    if (!(entry & MMU_PRESENT))
        return 0;
    if (entry & PTE_LARGE)
        return 1;
    u64 *pd = table_at(entry);
    entry = pd[(virtual_address >> 21) & 0x1FF];
    if (!(entry & MMU_PRESENT))
        return 0;
    if (entry & PTE_LARGE)
        return 1;
    u64 *pt = table_at(entry);
    return (pt[(virtual_address >> 12) & 0x1FF] & MMU_PRESENT) != 0;
}
