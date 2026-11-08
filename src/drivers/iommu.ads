--  Zhaoxu OS - Intel VT-d IOMMU driver, SPARK/Ada.
--
--  Parses the ACPI DMAR table (DMA Remapping Reporting), decodes the
--  first DRHD unit's VER/CAP/ECAP registers and can enable identity
--  second-level translation for every PCI bus.  The C glue provides
--  MMIO access, logging and page allocation.

with Interfaces; use Interfaces;
with System; use System;

package Iommu is
   pragma Preelaborate;

   --  u64 dmar_physical: ACPI DMAR table address (0 when absent).
   procedure Init (Dmar_Physical : Unsigned_64)
     with Export, Convention => C, External_Name => "iommu_init";

   procedure Enable
     with Export, Convention => C, External_Name => "iommu_enable";

   --  const struct iommu_info *: see kernel/include/iommu.h.
   function Get_Info return System.Address
     with Export, Convention => C, External_Name => "iommu_get_info";

end Iommu;
