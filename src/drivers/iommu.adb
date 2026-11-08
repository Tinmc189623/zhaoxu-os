--  Zhaoxu OS - Intel VT-d IOMMU driver, SPARK/Ada.
--
--  Walks the ACPI DMAR table (address supplied by the C side from
--  Acpi.Dmar_Address), enumerates DRHD remapping units and decodes
--  their VER/CAP/ECAP registers through the C MMIO glue.  Translation
--  is left disabled (TE stays 0), so DMA keeps running in passthrough
--  mode until per-device second-level tables are wired up.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;

package body Iommu is

   type C_Info is record
      Present        : Interfaces.C.int;
      Unit_Count     : Unsigned_32;
      Register_Base  : Unsigned_64;
      Version        : Unsigned_32;
      Capability     : Unsigned_64;
      Ext_Capability : Unsigned_64;
   end record
     with Convention => C;

   G_Info : aliased C_Info :=
     (Present        => 0,
      Unit_Count     => 0,
      Register_Base  => 0,
      Version        => 0,
      Capability     => 0,
      Ext_Capability => 0);

   function Get_Info return System.Address is (G_Info'Address);

   function Mmio_Read16 (A : Unsigned_64) return Unsigned_16
     with Import, Convention => C, External_Name => "mmio_read16";
   function Mmio_Read32 (A : Unsigned_64) return Unsigned_32
     with Import, Convention => C, External_Name => "mmio_read32";
   function Mmio_Read64 (A : Unsigned_64) return Unsigned_64
     with Import, Convention => C, External_Name => "mmio_read64";

   procedure Log_None
     with Import, Convention => C, External_Name => "iommu_log_none";
   procedure Log_Unit (Index : Unsigned_32; Base : Unsigned_64;
                       Version : Unsigned_32; Cap : Unsigned_64;
                       Ecap : Unsigned_64)
     with Import, Convention => C, External_Name => "iommu_log_unit";
   procedure Log_Summary (Units : Unsigned_32)
     with Import, Convention => C, External_Name => "iommu_log_summary";

   --  DMAR fixed header is 48 bytes; remapping structures follow.
   DMAR_Header_Length : constant Unsigned_64 := 48;

   --  DRHD structure: type u16, length u16, flags u8, reserved u8,
   --  segment u16, register_base u64 (offset 8).
   DRHD_Type          : constant Unsigned_16 := 0;
   DRHD_Base_Offset   : constant Unsigned_64 := 8;

   --  Registers inside a remapping unit, relative to register_base.
   Reg_Version        : constant Unsigned_64 := 16#00#;
   Reg_Capability     : constant Unsigned_64 := 16#08#;
   Reg_Ext_Capability : constant Unsigned_64 := 16#10#;

   procedure Init (Dmar_Physical : Unsigned_64) is
      Dmar_Length : Unsigned_32;
      Dmar_End    : Unsigned_64;
      Offset      : Unsigned_64;
      Units       : Unsigned_32 := 0;
   begin
      if Dmar_Physical = 0 then
         Log_None;
         return;
      end if;

      Dmar_Length := Mmio_Read32 (Dmar_Physical + 4);
      if Unsigned_64 (Dmar_Length) < DMAR_Header_Length then
         Log_None;
         return;
      end if;

      Dmar_End := Dmar_Physical + Unsigned_64 (Dmar_Length);
      Offset   := Dmar_Physical + DMAR_Header_Length;

      while Offset + 4 <= Dmar_End loop
         declare
            S_Type : constant Unsigned_16 := Mmio_Read16 (Offset);
            S_Len  : constant Unsigned_16 := Mmio_Read16 (Offset + 2);
         begin
            exit when S_Len < 4;
            if S_Type = DRHD_Type then
               declare
                  Base : constant Unsigned_64 :=
                    Mmio_Read64 (Offset + DRHD_Base_Offset);
                  Version : constant Unsigned_32 :=
                    Mmio_Read32 (Base + Reg_Version);
                  Cap  : constant Unsigned_64 := Mmio_Read64 (Base + Reg_Capability);
                  Ecap : constant Unsigned_64 :=
                    Mmio_Read64 (Base + Reg_Ext_Capability);
               begin
                  if Units = 0 then
                     G_Info.Register_Base  := Base;
                     G_Info.Version        := Version;
                     G_Info.Capability     := Cap;
                     G_Info.Ext_Capability := Ecap;
                  end if;
                  Log_Unit (Units, Base, Version, Cap, Ecap);
                  Units := Units + 1;
               end;
            end if;
            Offset := Offset + Unsigned_64 (S_Len);
         end;
      end loop;

      G_Info.Unit_Count := Units;
      if Units > 0 then
         G_Info.Present := 1;
      end if;
      Log_Summary (Units);
   end Init;

   procedure Enable is
   begin
      --  Translation Enable (GCMD.TE) is deliberately left clear: with no
      --  root/context tables programmed, enabling it would hang all DMA.
      --  Passthrough mode is the safe default for this bring-up stage.
      null;
   end Enable;

end Iommu;
