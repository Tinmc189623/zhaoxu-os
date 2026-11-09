#include <boot.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define HEAP_VIRTUAL_BASE 0xffffffff90000000ULL
#define PTE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS           (1ULL << 7)
#define PTE_2M_SIZE      0x200000ULL
#define IDENTITY_MAX     (64ULL * 1024 * 1024 * 1024)

struct vmm_memory_descriptor {
    u32 type;
    u32 pad;
    u64 physical_start;
    u64 virtual_start;
    u64 number_of_pages;
    u64 attribute;
} __attribute__((packed));

/* The user window lives in PML4 slot 2: 1 TiB..1.5 TiB.  Every process
 * gets a private copy of that slot; slots 0 and 3..511 stay shared with the
 * kernel address space (supervisor-only). */
#define USER_PML4_INDEX 2

static u64 *pml4;
static u64 heap_cursor = HEAP_VIRTUAL_BASE;
static struct address_space g_kernel_space;

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

static int map_in(u64 *root, u64 virtual_address, u64 physical_address,
                  u64 flags)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);
    int user = (flags & VMM_USER) != 0;

    u64 *pdpt = ensure_table(root, pml4_index, user);
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

static u64 get_physical_in(u64 *root, u64 virtual_address)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);

    u64 entry = root[pml4_index];
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

static u64 unmap_in(u64 *root, u64 virtual_address)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);

    u64 entry = root[pml4_index];
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

static int page_is_user_in(u64 *root, u64 virtual_address)
{
    u32 pml4_index = (u32)((virtual_address >> 39) & 0x1FF);
    u32 pdpt_index = (u32)((virtual_address >> 30) & 0x1FF);
    u32 pd_index = (u32)((virtual_address >> 21) & 0x1FF);
    u32 pt_index = (u32)((virtual_address >> 12) & 0x1FF);

    u64 entry = root[pml4_index];
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

int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags)
{
    return map_in(pml4, virtual_address, physical_address, flags);
}

u64 vmm_get_physical(u64 virtual_address)
{
    return get_physical_in(pml4, virtual_address);
}

u64 vmm_unmap_page(u64 virtual_address)
{
    return unmap_in(pml4, virtual_address);
}

int vmm_page_is_user(u64 virtual_address)
{
    return page_is_user_in(pml4, virtual_address);
}

int vmm_space_map_page(struct address_space *space, u64 virtual_address,
                       u64 physical_address, u64 flags)
{
    if (!space)
        return -1;
    return map_in(space->pml4, virtual_address, physical_address, flags);
}

u64 vmm_space_get_physical(struct address_space *space, u64 virtual_address)
{
    if (!space)
        return 0;
    return get_physical_in(space->pml4, virtual_address);
}

u64 vmm_space_unmap_page(struct address_space *space, u64 virtual_address)
{
    if (!space)
        return 0;
    return unmap_in(space->pml4, virtual_address);
}

int vmm_space_page_is_user(struct address_space *space, u64 virtual_address)
{
    if (!space)
        return 0;
    return page_is_user_in(space->pml4, virtual_address);
}

static void free_user_tables(u64 *root)
{
    u64 entry = root[USER_PML4_INDEX];
    if (!(entry & VMM_PRESENT) || (entry & PTE_PS)) {
        root[USER_PML4_INDEX] = 0;
        return;
    }

    u64 *pdpt = table_from_entry(entry);
    for (u32 i = 0; i < 512; i++) {
        u64 pdpt_entry = pdpt[i];
        if (!(pdpt_entry & VMM_PRESENT) || (pdpt_entry & PTE_PS))
            continue;
        u64 *pd = table_from_entry(pdpt_entry);
        for (u32 j = 0; j < 512; j++) {
            u64 pd_entry = pd[j];
            if (!(pd_entry & VMM_PRESENT) || (pd_entry & PTE_PS))
                continue;
            u64 *pt = table_from_entry(pd_entry);
            pmm_free_page((u64)(uintptr_t)pt);
            pd[j] = 0;
        }
        pmm_free_page((u64)(uintptr_t)pd);
        pdpt[i] = 0;
    }
    pmm_free_page((u64)(uintptr_t)pdpt);
    root[USER_PML4_INDEX] = 0;
}

struct address_space *vmm_space_create(void)
{
    u64 physical = pmm_alloc_page();
    if (!physical)
        return NULL;

    u64 *root = (u64 *)(uintptr_t)physical;
    for (u32 i = 0; i < 512; i++)
        root[i] = pml4[i];
    /* User pages, stacks and shared mappings are private to the process. */
    root[USER_PML4_INDEX] = 0;

    struct address_space *space = kmalloc(sizeof(*space));
    if (!space) {
        pmm_free_page(physical);
        return NULL;
    }
    space->pml4 = root;
    space->cr3 = physical;
    return space;
}

void vmm_space_destroy(struct address_space *space)
{
    if (!space || space == &g_kernel_space)
        return;
    free_user_tables(space->pml4);
    pmm_free_page(space->cr3);
    kfree(space);
}

void vmm_space_activate(struct address_space *space)
{
    if (!space)
        return;
    vmm_activate_cr3(space->cr3);
}

void vmm_activate_cr3(u64 cr3)
{
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

struct address_space *vmm_kernel_space(void)
{
    return &g_kernel_space;
}

u64 vmm_kernel_cr3(void)
{
    return g_kernel_space.cr3;
}

u64 vmm_current_cr3(void)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

u64 vmm_alloc_pages(u32 count)
{
    if (count == 0)
        return 0;
    u64 start = heap_cursor;
    u64 cursor = start;
    for (u32 i = 0; i < count; i++) {
        u64 physical = pmm_alloc_page();
        if (!physical || vmm_map_page(cursor, physical, VMM_PRESENT |
                                      VMM_WRITABLE) != 0) {
            if (physical)
                pmm_free_page(physical);
            for (u64 undo = start; undo < cursor; undo += PAGE_SIZE) {
                u64 mapped = vmm_unmap_page(undo);
                if (mapped)
                    pmm_free_page(mapped);
            }
            return 0;
        }
        cursor += PAGE_SIZE;
    }
    heap_cursor = cursor;
    return start;
}

u64 vmm_heap_cursor(void)
{
    return heap_cursor;
}

/* Return the page directory covering one 1 GiB identity region, allocating
 * the PDPT/PD pages from low memory when the loader left them absent. */
static u64 *ensure_2m_pd(u32 pml4_index, u32 pdpt_index)
{
    u64 *pdpt = ensure_table(pml4, pml4_index, 0);
    if (!pdpt)
        return NULL;
    u64 entry = pdpt[pdpt_index];
    if (!(entry & VMM_PRESENT)) {
        u64 physical = pmm_alloc_page();
        if (!physical)
            return NULL;
        pdpt[pdpt_index] = physical | VMM_PRESENT | VMM_WRITABLE;
        return (u64 *)(uintptr_t)physical;
    }
    if (entry & PTE_PS)
        return NULL;
    return table_from_entry(entry);
}

/* Identity-map RAM above 4 GiB with 2 MiB pages so that physical addresses
 * can be used directly, exactly like the low 4 GiB the loader mapped. */
static void identity_map_high(u64 start, u64 end)
{
    for (u64 address = start & ~(PTE_2M_SIZE - 1); address < end;
         address += PTE_2M_SIZE) {
        u32 pml4_index = (u32)((address >> 39) & 0x1FF);
        u32 pdpt_index = (u32)((address >> 30) & 0x1FF);
        u32 pd_index = (u32)((address >> 21) & 0x1FF);
        u64 *pd = ensure_2m_pd(pml4_index, pdpt_index);
        if (!pd)
            return;
        pd[pd_index] = address | VMM_PRESENT | VMM_WRITABLE | PTE_PS;
    }
}

/* Highest firmware-described physical address that is not MMIO.  ACPI
 * tables and reserved NVS live in RAM, so include every RAM-backed type. */
static u64 memory_map_physical_end(const struct boot_info *bi)
{
    u64 end = 0x100000000ULL;
    u64 count = bi->memory_map_size / bi->memory_map_desc_size;
    const u8 *cursor = (const u8 *)bi->memory_map;
    for (u64 i = 0; i < count; i++) {
        const struct vmm_memory_descriptor *desc =
            (const struct vmm_memory_descriptor *)(cursor +
                                                   i * bi->memory_map_desc_size);
        if (desc->type == 11 || desc->type == 12)
            continue; /* EfiMemoryMappedIO / IOPortSpace */
        u64 desc_end =
            desc->physical_start + desc->number_of_pages * PAGE_SIZE;
        if (desc_end > end)
            end = desc_end;
    }
    return end > IDENTITY_MAX ? IDENTITY_MAX : end;
}

void vmm_init(const struct boot_info *bi)
{
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4 = (u64 *)(uintptr_t)(bi->page_table_root ? bi->page_table_root : cr3);
    heap_cursor = HEAP_VIRTUAL_BASE;
    g_kernel_space.pml4 = pml4;
    g_kernel_space.cr3 = (u64)(uintptr_t)pml4;

    u64 physical_end = memory_map_physical_end(bi);
    if (physical_end > 0x100000000ULL) {
        identity_map_high(0x100000000ULL, physical_end);
        /* Paging-structure caches may still hold the old non-present PDPT
         * entries; a CR3 reload makes the new 2 MiB mappings visible. */
        __asm__ volatile("mov %%cr3, %0\n\tmov %0, %%cr3"
                         : "=r"(cr3) : : "memory");
        pmm_extend(physical_end);
    }

    kprintf("[vmm ] pml4 @ 0x%llx, heap base 0x%llx\n",
            (u64)(uintptr_t)pml4, HEAP_VIRTUAL_BASE);
}
