--  Zhaoxu OS - ACPI driver, SPARK/Ada.
--
--  Parses the RSDP, RSDT/XSDT, FADT and the DSDT _S5 package, and drives
--  power off and reset through the FADT registers with the usual legacy
--  fallbacks.  The C side only provides logging, port I/O, CPU control
--  and the final triple-fault sequence.

with Interfaces; use Interfaces;
with System; use System;

package Acpi is
   pragma Preelaborate;

   --  u64 rsdp_physical: physical address handed over by the boot layer.
   procedure Init (Rsdp_Physical : Unsigned_64)
     with Export, Convention => C, External_Name => "acpi_init";

   --  const struct acpi_info *: see kernel/include/acpi.h for the layout.
   function Get_Info return System.Address
     with Export, Convention => C, External_Name => "acpi_get_info";

   --  Physical address of the ACPI DMAR table, or 0 when the platform has
   --  no VT-d/IOMMU description. Consumed by drivers/iommu.adb.
   function Dmar_Address return Unsigned_64
     with Export, Convention => C, External_Name => "acpi_dmar_address";

   procedure Poweroff
     with Export, Convention => C, External_Name => "acpi_poweroff";

   procedure Reboot
     with Export, Convention => C, External_Name => "acpi_reboot";

end Acpi;
