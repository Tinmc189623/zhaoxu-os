#ifndef ZHAOXU_IOMMU_H
#define ZHAOXU_IOMMU_H

#include <types.h>

struct iommu_info {
    int present;         /* DMAR found with at least one DRHD unit */
    u32 unit_count;      /* DRHD units discovered */
    u64 register_base;   /* MMIO base of the first DRHD unit */
    u32 version;         /* VT-d architecture version (VER register) */
    u64 capability;      /* CAP register */
    u64 ext_capability;  /* ECAP register */
};

void iommu_init(u64 dmar_physical);
void iommu_enable(void);
const struct iommu_info *iommu_get_info(void);

#endif
