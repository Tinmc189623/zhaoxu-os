#ifndef ZHAOXU_MMU_H
#define ZHAOXU_MMU_H

#include <types.h>

#define MMU_PRESENT  (1ULL << 0)
#define MMU_WRITABLE (1ULL << 1)
#define MMU_USER     (1ULL << 2)
#define MMU_WRITE_THROUGH (1ULL << 3)
#define MMU_CACHE_DISABLE (1ULL << 4)
#define MMU_NO_EXECUTE (1ULL << 63)

/* Explicit page-table driver.  Unlike the process loader helpers, these
 * functions accept any CR3 and operate on the live four-level tree:
 * splitting a 1 GiB or 2 MiB leaf when a smaller mapping is requested. */
void mmu_init(void);
u64 mmu_current_cr3(void);
int mmu_map_page(u64 cr3, u64 virtual_address, u64 physical_address,
                 u64 flags);
int mmu_map_range(u64 cr3, u64 virtual_address, u64 physical_address,
                  u64 pages, u64 flags);
u64 mmu_unmap_page(u64 cr3, u64 virtual_address);
int mmu_protect_page(u64 cr3, u64 virtual_address, u64 flags);
u64 mmu_translate(u64 cr3, u64 virtual_address);
int mmu_is_mapped(u64 cr3, u64 virtual_address);

#endif
