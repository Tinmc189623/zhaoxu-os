/*
 * Zhaoxu OS - C glue for the SPARK/Ada Intel VT-d IOMMU driver.
 *
 * The Ada side parses the ACPI DMAR table and decodes the DRHD unit
 * registers; logging only lives here because Ada cannot call variadic
 * kprintf.
 */
#include <kprintf.h>
#include <types.h>

void iommu_log_none(void)
{
    kprintf("[iommu] no DMAR table, DMA passthrough\n");
}

void iommu_log_unit(u32 index, u64 base, u32 version, u64 cap, u64 ecap)
{
    kprintf("[iommu] DRHD%u base 0x%llx, v%u.%u, CAP 0x%llx, ECAP 0x%llx\n",
            index, base, version >> 4, version & 0xf, cap, ecap);
}

void iommu_log_summary(u32 units)
{
    kprintf("[iommu] %u DRHD unit(s), translation disabled (passthrough)\n",
            units);
}
