--  Zhaoxu OS - ACPI driver, SPARK/Ada.
--
--  All table walking and _S5 parsing is done here; the C glue provides
--  logging, port I/O and CPU control only.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;

use type Interfaces.C.int;
use type Interfaces.Unsigned_16;
use type Interfaces.Unsigned_32;
use type Interfaces.Unsigned_64;

package body Acpi is
   --  The kernel identity-maps up to 64 GiB of physical memory, including
   --  ACPI/NVS regions above 4 GiB, so XSDT addresses are usable as-is.
   Physical_Limit : constant Unsigned_64 := 16#10_0000_0000#;

   --  C glue -------------------------------------------------------------
   procedure Outb (Port : Unsigned_16; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "outb_port";
   procedure Outw (Port : Unsigned_16; Value : Unsigned_16)
     with Import, Convention => C, External_Name => "outw_port";
   function Inb (Port : Unsigned_16) return Unsigned_8
     with Import, Convention => C, External_Name => "inb_port";

   function Mmio_Read8 (A : Unsigned_64) return Unsigned_8
     with Import, Convention => C, External_Name => "mmio_read8";
   function Mmio_Read16 (A : Unsigned_64) return Unsigned_16
     with Import, Convention => C, External_Name => "mmio_read16";
   function Mmio_Read32 (A : Unsigned_64) return Unsigned_32
     with Import, Convention => C, External_Name => "mmio_read32";
   function Mmio_Read64 (A : Unsigned_64) return Unsigned_64
     with Import, Convention => C, External_Name => "mmio_read64";
   procedure Mmio_Write8 (A : Unsigned_64; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "mmio_write8";

   procedure Cpu_Cli
     with Import, Convention => C, External_Name => "acpi_cpu_cli";
   procedure Cpu_Halt
     with Import, Convention => C, External_Name => "acpi_cpu_halt";
   procedure Spin (Iterations : Unsigned_32)
     with Import, Convention => C, External_Name => "acpi_spin";
   procedure Triple_Fault
     with Import, Convention => C, External_Name => "acpi_triple_fault";

   procedure Log_Fail (Reason : Address; Value : Unsigned_64)
     with Import, Convention => C, External_Name => "acpi_log_fail";
   procedure Log_Tables (Revision : Unsigned_32; Oem : Address;
                         Tables : Unsigned_32; Use_Xsdt : Unsigned_32)
     with Import, Convention => C, External_Name => "acpi_log_tables";
   procedure Log_Fadt (Revision : Unsigned_32; Pm1a : Unsigned_32;
                       Pm1b : Unsigned_32; Smi : Unsigned_32;
                       Slp_Valid : Unsigned_32; Slp : Unsigned_32)
     with Import, Convention => C, External_Name => "acpi_log_fadt";
   procedure Log_No_Fadt
     with Import, Convention => C, External_Name => "acpi_log_no_fadt";
   procedure Log_Reset (Space : Unsigned_32; A : Unsigned_64;
                        Value : Unsigned_32)
     with Import, Convention => C, External_Name => "acpi_log_reset";
   procedure Log_Fallback
     with Import, Convention => C, External_Name => "acpi_log_fallback";
   procedure Log_Halted
     with Import, Convention => C, External_Name => "acpi_log_halted";
   procedure Log_Ext_Checksum
     with Import, Convention => C, External_Name => "acpi_log_ext_checksum";

   Reason_Missing : aliased constant String :=
     "RSDP unavailable" & Character'Val (0);
   Reason_Signature : aliased constant String :=
     "bad RSDP signature" & Character'Val (0);
   Reason_Checksum : aliased constant String :=
     "bad RSDP checksum" & Character'Val (0);
   Reason_Root : aliased constant String :=
     "no usable root table" & Character'Val (0);

   --  Mirrors struct acpi_info in kernel/include/acpi.h. -----------------
   type Acpi_Info is record
      Present             : Interfaces.C.int;
      Rsdp_Revision       : Unsigned_32;
      Oem_Id              : Interfaces.C.char_array (0 .. 6);
      Table_Count         : Unsigned_32;
      Fadt_Revision       : Unsigned_32;
      Pm1a_Control        : Unsigned_16;
      Pm1b_Control        : Unsigned_16;
      Smi_Command         : Unsigned_32;
      Slp_Typa            : Unsigned_8;
      Slp_Typa_Valid      : Interfaces.C.int;
      Reset_Supported     : Interfaces.C.int;
      Reset_Address_Space : Unsigned_8;
      Reset_Value         : Unsigned_8;
      Reset_Address       : Unsigned_64;
   end record
     with Convention => C;

   pragma Compile_Time_Error
     (Acpi_Info'Size /= 448, "acpi_info must stay 56 bytes (C layout)");

   G_Info : aliased Acpi_Info :=
     (Present             => 0,
      Rsdp_Revision       => 0,
      Oem_Id              => (others => Interfaces.C.nul),
      Table_Count         => 0,
      Fadt_Revision       => 0,
      Pm1a_Control        => 0,
      Pm1b_Control        => 0,
      Smi_Command         => 0,
      Slp_Typa            => 0,
      Slp_Typa_Valid      => 0,
      Reset_Supported     => 0,
      Reset_Address_Space => 0,
      Reset_Value         => 0,
      Reset_Address       => 0);

   --  First valid DMAR table found while walking the root table; the
   --  VT-d IOMMU driver (drivers/iommu.adb) consumes it.
   G_Dmar : Unsigned_64 := 0;

   --  Memory helpers -----------------------------------------------------
   function Read8 (A : Unsigned_64) return Unsigned_8 is
     (Mmio_Read8 (A));
   function Read16 (A : Unsigned_64) return Unsigned_16 is
     (Mmio_Read16 (A));
   function Read32 (A : Unsigned_64) return Unsigned_32 is
     (Mmio_Read32 (A));
   function Read64 (A : Unsigned_64) return Unsigned_64 is
     (Mmio_Read64 (A));

   function Signature_Matches (Base : Unsigned_64; Sig : String)
     return Boolean is
   begin
      for I in Sig'Range loop
         if Read8 (Base + Unsigned_64 (I - Sig'First)) /=
           Unsigned_8 (Character'Pos (Sig (I)))
         then
            return False;
         end if;
      end loop;
      return True;
   end Signature_Matches;

   function Checksum_Ok (Base : Unsigned_64; Length : Unsigned_32)
     return Boolean is
      Sum : Unsigned_8 := 0;
   begin
      if Length = 0 then
         return False;
      end if;
      for I in Unsigned_32 range 0 .. Length - 1 loop
         Sum := Sum + Read8 (Base + Unsigned_64 (I));
      end loop;
      return Sum = 0;
   end Checksum_Ok;

   --  AML helpers --------------------------------------------------------
   function Aml_Integer (A : Unsigned_64; Value : out Unsigned_32;
                         Consumed : out Unsigned_32) return Boolean is
      Op : constant Unsigned_8 := Read8 (A);
   begin
      case Op is
         when 16#00# =>
            Value := 0;
            Consumed := 1;
            return True;
         when 16#01# =>
            Value := 1;
            Consumed := 1;
            return True;
         when 16#FF# =>
            Value := 16#FFFF_FFFF#;
            Consumed := 1;
            return True;
         when 16#0A# =>
            Value := Unsigned_32 (Read8 (A + 1));
            Consumed := 2;
            return True;
         when 16#0B# =>
            Value := Unsigned_32 (Read8 (A + 1))
                     or Shift_Left (Unsigned_32 (Read8 (A + 2)), 8);
            Consumed := 3;
            return True;
         when 16#0C# =>
            Value := Unsigned_32 (Read8 (A + 1))
                     or Shift_Left (Unsigned_32 (Read8 (A + 2)), 8)
                     or Shift_Left (Unsigned_32 (Read8 (A + 3)), 16)
                     or Shift_Left (Unsigned_32 (Read8 (A + 4)), 24);
            Consumed := 5;
            return True;
         when others =>
            return False;
      end case;
   end Aml_Integer;

   --  Locate Name(_S5, Package(2){ SLP_TYPa, SLP_TYPb }) in the DSDT.
   function Parse_S5 (Dsdt : Unsigned_64; Length : Unsigned_32;
                      Slp : out Unsigned_8) return Boolean is
      Prefix : constant array (0 .. 4) of Unsigned_8 :=
        (16#08#, 16#5F#, 16#53#, 16#35#, 16#5F#);
      I : Unsigned_32 := 0;
      Match : Boolean;
      P : Unsigned_64;
      Lead : Unsigned_8;
      Extra : Unsigned_32;
      Pkg_Len : Unsigned_32;
      Consumed : Unsigned_32;
      Elements : Unsigned_64;
      Count : Unsigned_8;
      Value : Unsigned_32;
      Value_Len : Unsigned_32;
   begin
      Slp := 0;
      if Length < 16 then
         return False;
      end if;

      while I + 16 < Length loop
         Match := True;
         for J in Prefix'Range loop
            if Read8 (Dsdt + Unsigned_64 (I) + Unsigned_64 (J)) /=
              Prefix (J)
            then
               Match := False;
               exit;
            end if;
         end loop;

         if Match then
            P := Dsdt + Unsigned_64 (I) + 5;
            if Read8 (P) = 16#12# then          --  PackageOp
               Lead := Read8 (P + 1);
               Extra := Unsigned_32 (Shift_Right (Lead, 6));
               if Extra = 0 then
                  Pkg_Len := Unsigned_32 (Lead and 16#3F#);
                  Consumed := 1;
               else
                  Pkg_Len := Unsigned_32 (Lead and 16#0F#);
                  for K in 0 .. Extra - 1 loop
                     Pkg_Len := Pkg_Len
                       or Shift_Left
                            (Unsigned_32 (Read8 (P + 2 + Unsigned_64 (K))),
                             Natural (4 + 8 * K));
                  end loop;
                  Consumed := 1 + Extra;
               end if;

               if Pkg_Len >= 2 and then Pkg_Len <= 1024
                 and then Unsigned_64 (I) + 6
                   + Unsigned_64 (Consumed) + Unsigned_64 (Pkg_Len)
                   <= Unsigned_64 (Length)
               then
                  Elements := P + 1 + Unsigned_64 (Consumed);
                  Count := Read8 (Elements);
                  if Count >= 1
                    and then Aml_Integer (Elements + 1, Value, Value_Len)
                    and then Value <= 7
                  then
                     Slp := Unsigned_8 (Value);
                     return True;
                  end if;
               end if;
            end if;
         end if;

         I := I + 1;
      end loop;
      return False;
   end Parse_S5;

   --  Table walking ------------------------------------------------------
   procedure Parse_Fadt (Fadt : Unsigned_64) is
      Length : constant Unsigned_32 := Read32 (Fadt + 4);
      Flags : Unsigned_32;
      Address : Unsigned_64;
      Dsdt : Unsigned_64;
      Dsdt_Length : Unsigned_32;
      Slp : Unsigned_8;
   begin
      G_Info.Fadt_Revision := Unsigned_32 (Read8 (Fadt + 8));
      if Length < 116 then
         return;
      end if;

      G_Info.Pm1a_Control := Read16 (Fadt + 64);
      G_Info.Pm1b_Control := Read16 (Fadt + 68);
      G_Info.Smi_Command := Read32 (Fadt + 48);

      Flags := Read32 (Fadt + 112);
      if (Flags and Shift_Left (Unsigned_32 (1), 10)) /= 0
        and then Length >= 129
      then
         Address := Read64 (Fadt + 120);
         if Address /= 0 then
            G_Info.Reset_Supported := 1;
            G_Info.Reset_Address_Space := Read8 (Fadt + 116);
            G_Info.Reset_Address := Address;
            G_Info.Reset_Value := Read8 (Fadt + 128);
         end if;
      end if;

      if Length >= 184 and then G_Info.Pm1a_Control = 0
        and then Read8 (Fadt + 172) = 1
      then
         Address := Read64 (Fadt + 176);
         if Address /= 0 and then Address <= 16#FFFF# then
            G_Info.Pm1a_Control := Unsigned_16 (Address);
         end if;
      end if;
      if Length >= 196 and then G_Info.Pm1b_Control = 0
        and then Read8 (Fadt + 184) = 1
      then
         Address := Read64 (Fadt + 188);
         if Address /= 0 and then Address <= 16#FFFF# then
            G_Info.Pm1b_Control := Unsigned_16 (Address);
         end if;
      end if;

      if Length >= 148 then
         Dsdt := Read64 (Fadt + 140);           --  X_DSDT
         if Dsdt = 0 or else Dsdt >= Physical_Limit then
            Dsdt := Unsigned_64 (Read32 (Fadt + 40));
         end if;
         if Dsdt /= 0 and then Dsdt < Physical_Limit
           and then Signature_Matches (Dsdt, "DSDT")
         then
            Dsdt_Length := Read32 (Dsdt + 4);
            if Dsdt_Length >= 36 and then Dsdt_Length <= 16#10_0000# then
               if Parse_S5 (Dsdt, Dsdt_Length, Slp) then
                  G_Info.Slp_Typa := Slp;
                  G_Info.Slp_Typa_Valid := 1;
               end if;
            end if;
         end if;
      end if;
   end Parse_Fadt;

   procedure Scan_Root (Root : Unsigned_64; Is_Xsdt : Boolean;
                        Fadt : out Unsigned_64) is
      Length : Unsigned_32;
      Entry_Size : Unsigned_32;
      Count : Unsigned_32;
      Address : Unsigned_64;
      Table_Length : Unsigned_32;
   begin
      Fadt := 0;
      G_Info.Table_Count := 0;

      if Is_Xsdt then
         if not Signature_Matches (Root, "XSDT") then
            return;
         end if;
      else
         if not Signature_Matches (Root, "RSDT") then
            return;
         end if;
      end if;

      Length := Read32 (Root + 4);
      if Length < 36 or else Length > 16#10_0000#
        or else not Checksum_Ok (Root, Length)
      then
         return;
      end if;

      if Is_Xsdt then
         Entry_Size := 8;
      else
         Entry_Size := 4;
      end if;
      Count := (Length - 36) / Entry_Size;

      for I in Unsigned_32 range 0 .. Count - 1 loop
         if Is_Xsdt then
            Address := Read64 (Root + 36 + Unsigned_64 (I) * 8);
         else
            Address := Unsigned_64 (Read32 (Root + 36 + Unsigned_64 (I) * 4));
         end if;

         if Address /= 0 and then Address < Physical_Limit then
            Table_Length := Read32 (Address + 4);
            if Table_Length >= 36 and then Table_Length <= 16#10_0000#
              and then Checksum_Ok (Address, Table_Length)
            then
               G_Info.Table_Count := G_Info.Table_Count + 1;
               if Fadt = 0 and then Signature_Matches (Address, "FACP") then
                  Fadt := Address;
               end if;
               if G_Dmar = 0 and then Signature_Matches (Address, "DMAR") then
                  G_Dmar := Address;
               end if;
            end if;
         end if;
      end loop;
   end Scan_Root;

   --  Public API ---------------------------------------------------------
   procedure Init (Rsdp_Physical : Unsigned_64) is
      Root : Unsigned_64;
      Use_Xsdt : Boolean := False;
      Fadt : Unsigned_64;
      Rsdp_Length : Unsigned_32;
      Xsdt_Address : Unsigned_64;
   begin
      G_Info :=
        (Present             => 0,
         Rsdp_Revision       => 0,
         Oem_Id              => (others => Interfaces.C.nul),
         Table_Count         => 0,
         Fadt_Revision       => 0,
         Pm1a_Control        => 0,
         Pm1b_Control        => 0,
         Smi_Command         => 0,
         Slp_Typa            => 0,
         Slp_Typa_Valid      => 0,
         Reset_Supported     => 0,
         Reset_Address_Space => 0,
         Reset_Value         => 0,
         Reset_Address       => 0);

      G_Dmar := 0;

      if Rsdp_Physical = 0 or else Rsdp_Physical >= Physical_Limit then
         Log_Fail (Reason_Missing'Address, Rsdp_Physical);
         return;
      end if;
      if not Signature_Matches (Rsdp_Physical, "RSD PTR ") then
         Log_Fail (Reason_Signature'Address, Rsdp_Physical);
         return;
      end if;
      if not Checksum_Ok (Rsdp_Physical, 20) then
         Log_Fail (Reason_Checksum'Address, Rsdp_Physical);
         return;
      end if;

      G_Info.Rsdp_Revision := Unsigned_32 (Read8 (Rsdp_Physical + 15));
      for I in Interfaces.C.size_t range 0 .. 5 loop
         G_Info.Oem_Id (I) :=
           Interfaces.C.char
             (Character'Val (Natural (Read8 (Rsdp_Physical + 9 +
                                          Unsigned_64 (I)))));
      end loop;
      G_Info.Oem_Id (6) := Interfaces.C.nul;

      Root := Unsigned_64 (Read32 (Rsdp_Physical + 16));
      if G_Info.Rsdp_Revision >= 2 then
         Rsdp_Length := Read32 (Rsdp_Physical + 20);
         Xsdt_Address := Read64 (Rsdp_Physical + 24);
         if Rsdp_Length < 36 or else Rsdp_Length > 4096
           or else not Checksum_Ok (Rsdp_Physical, Rsdp_Length)
         then
            Log_Ext_Checksum;
         end if;
         --  XSDT is authoritative on ACPI 2.0+: it carries full 64-bit
         --  table addresses, including tables above 4 GiB.  The 32-bit
         --  RSDT is only a legacy fallback when the XSDT pointer is
         --  absent or the XSDT itself is unusable.
         if Xsdt_Address /= 0 and then Xsdt_Address < Physical_Limit then
            Root := Xsdt_Address;
            Use_Xsdt := True;
         end if;
      end if;

      if Root = 0 or else Root >= Physical_Limit then
         Log_Fail (Reason_Root'Address, Root);
         return;
      end if;

      Scan_Root (Root, Use_Xsdt, Fadt);
      if Fadt = 0 and then Use_Xsdt then
         Root := Unsigned_64 (Read32 (Rsdp_Physical + 16));
         if Root /= 0 and then Root < Physical_Limit then
            Scan_Root (Root, False, Fadt);
            Use_Xsdt := False;
         end if;
      end if;
      G_Info.Present := 1;

      Log_Tables (G_Info.Rsdp_Revision, G_Info.Oem_Id'Address,
                  G_Info.Table_Count, (if Use_Xsdt then 1 else 0));
      if Fadt = 0 then
         Log_No_Fadt;
         return;
      end if;

      Parse_Fadt (Fadt);
      Log_Fadt (G_Info.Fadt_Revision,
                Unsigned_32 (G_Info.Pm1a_Control),
                Unsigned_32 (G_Info.Pm1b_Control),
                G_Info.Smi_Command,
                Unsigned_32 (G_Info.Slp_Typa_Valid),
                Unsigned_32 (G_Info.Slp_Typa));
      if G_Info.Reset_Supported /= 0 then
         Log_Reset (Unsigned_32 (G_Info.Reset_Address_Space),
                    G_Info.Reset_Address,
                    Unsigned_32 (G_Info.Reset_Value));
      else
         Log_Fallback;
      end if;
   end Init;

   function Get_Info return System.Address is
   begin
      return G_Info'Address;
   end Get_Info;

   function Dmar_Address return Unsigned_64 is
   begin
      return G_Dmar;
   end Dmar_Address;

   procedure Write_Sleep_State (Sleep_Type : Unsigned_8) is
      Value : constant Unsigned_16 :=
        Shift_Left (Unsigned_16 (Sleep_Type and 7), 10)
        or Shift_Left (Unsigned_16 (1), 13);
   begin
      if G_Info.Pm1a_Control /= 0 then
         Outw (G_Info.Pm1a_Control, Value);
         Spin (50000);
      end if;
      if G_Info.Pm1b_Control /= 0 then
         Outw (G_Info.Pm1b_Control, Value);
         Spin (50000);
      end if;
   end Write_Sleep_State;

   procedure Poweroff is
      Attempted : array (0 .. 3) of Unsigned_8;
      Count : Unsigned_32 := 0;

      procedure Add_Unique (Value : Unsigned_8) is
         Seen : Boolean := False;
      begin
         for J in Natural range 0 .. Natural (Count) - 1 loop
            if Attempted (J) = Value then
               Seen := True;
            end if;
         end loop;
         if not Seen and then Count < 4 then
            Attempted (Natural (Count)) := Value;
            Count := Count + 1;
         end if;
      end Add_Unique;
   begin
      Cpu_Cli;

      if G_Info.Slp_Typa_Valid /= 0 then
         Add_Unique (G_Info.Slp_Typa);
      else
         Add_Unique (5);
      end if;
      Add_Unique (5);
      Add_Unique (0);

      for I in Natural range 0 .. Natural (Count) - 1 loop
         Write_Sleep_State (Attempted (I));
      end loop;

      --  QEMU/Bochs fixed ACPI ports (q35 and i440fx PM base).
      Outw (16#604#, 16#2000#);
      Spin (50000);
      Outw (16#B004#, 16#2000#);
      Spin (50000);

      Log_Halted;
      loop
         Cpu_Halt;
      end loop;
   end Poweroff;

   procedure Reboot is
   begin
      Cpu_Cli;

      if G_Info.Reset_Supported /= 0 then
         if G_Info.Reset_Address_Space = 1
           and then G_Info.Reset_Address <= 16#FFFF#
         then
            Outb (Unsigned_16 (G_Info.Reset_Address), G_Info.Reset_Value);
            Spin (100000);
         elsif G_Info.Reset_Address_Space = 0 then
            Mmio_Write8 (G_Info.Reset_Address, G_Info.Reset_Value);
            Spin (100000);
         end if;
      end if;

      --  PCI reset control register.
      Outb (16#CF9#, 16#02#);
      Outb (16#80#, 0);
      Spin (10000);
      Outb (16#CF9#, 16#06#);
      Spin (100000);

      --  Keyboard controller pulse reset line.
      for I in 1 .. 1000 loop
         exit when (Inb (16#64#) and 16#02#) = 0;
      end loop;
      Outb (16#64#, 16#FE#);
      Spin (100000);

      --  Last resort: triple fault.
      Triple_Fault;
      loop
         Cpu_Halt;
      end loop;
   end Reboot;

end Acpi;
