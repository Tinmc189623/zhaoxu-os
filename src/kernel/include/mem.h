#ifndef ZHAOXU_MEM_H
#define ZHAOXU_MEM_H

#include <boot.h>
#include <types.h>

#define PAGE_SIZE 4096ULL

#define VMM_PRESENT  (1ULL << 0)
#define VMM_WRITABLE (1ULL << 1)
#define VMM_USER     (1ULL << 2)

struct pmm_stats {
    u64 managed_pages;
    u64 free_pages;
    u64 used_pages;
};

/* One per-process page table.  Kernel entries are shared with the boot
 * address space; the user window (PML4 slot 2, 1 TiB..1.5 TiB) is private. */
struct address_space {
    u64 *pml4;
    u64 cr3;
};

void pmm_init(const struct boot_info *bi);
void pmm_extend(u64 limit);
u64 pmm_alloc_page(void);
void pmm_free_page(u64 physical);
void pmm_get_stats(struct pmm_stats *stats);
void pmm_reserve_range(u64 physical, u64 size);

void vmm_init(const struct boot_info *bi);
int vmm_map_page(u64 virtual_address, u64 physical_address, u64 flags);
u64 vmm_unmap_page(u64 virtual_address);
int vmm_page_is_user(u64 virtual_address);
u64 vmm_alloc_pages(u32 count);
u64 vmm_get_physical(u64 virtual_address);
u64 vmm_heap_cursor(void);

struct address_space *vmm_space_create(int user_phys);
void vmm_space_destroy(struct address_space *space);
int vmm_space_map_page(struct address_space *space, u64 virtual_address,
                       u64 physical_address, u64 flags);
u64 vmm_space_unmap_page(struct address_space *space, u64 virtual_address);
u64 vmm_space_get_physical(struct address_space *space, u64 virtual_address);
int vmm_space_page_is_user(struct address_space *space, u64 virtual_address);
void vmm_space_activate(struct address_space *space);
int vmm_space_user_phys(struct address_space *space);
void vmm_activate_cr3(u64 cr3);
struct address_space *vmm_kernel_space(void);
u64 vmm_kernel_cr3(void);
u64 vmm_current_cr3(void);

void heap_init(void);
void *kmalloc(usize size);
void *kzalloc(usize size);
void kfree(void *pointer);
void heap_get_stats(u64 *total, u64 *used, u64 *free_bytes);

#endif
