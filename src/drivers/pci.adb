--  Zhaoxu OS - PCI bus enumeration and configuration access (SPARK driver)
--
--  Config cycles go through Pcie, which prefers the memory-mapped ECAM
--  window and falls back to the legacy 0xCF8/0xCFC pair.  Printing is
--  delegated to kernel/drivers/pci_ui.c because Ada cannot call kprintf.

with Interfaces; use Interfaces;
with Pcie;
pragma Elaborate (Pcie);

package body Pci is
   function Pcie_Active return Unsigned_32
     with Import, Convention => C, External_Name => "pcie_active";

   procedure Report_Device (Bus   : Unsigned_32;
                            Slot  : Unsigned_32;
                            Func  : Unsigned_32;
                            Id    : Unsigned_32;
                            Class : Unsigned_32)
     with Import, Convention => C, External_Name => "pci_report_device";

   procedure Report_Header
     with Import, Convention => C, External_Name => "pci_report_header";

   procedure Report_Summary (Found : Unsigned_32; Ecam : Unsigned_32)
     with Import, Convention => C, External_Name => "pci_report_summary";

   function Config_Read (Bus   : Unsigned_32;
                         Slot  : Unsigned_32;
                         Func  : Unsigned_32;
                         Off   : Unsigned_32)
     return Unsigned_32 is
   begin
      return Pcie.Config_Read (Bus, Slot, Func, Off);
   end Config_Read;

   procedure Config_Write (Bus   : Unsigned_32;
                           Slot  : Unsigned_32;
                           Func  : Unsigned_32;
                           Off   : Unsigned_32;
                           Value : Unsigned_32) is
   begin
      Pcie.Config_Write (Bus, Slot, Func, Off, Value);
   end Config_Write;

   function Enumerate return Unsigned_32 is
      Found : Unsigned_32 := 0;
      Id    : Unsigned_32;
      Header : Unsigned_32;
      Funcs : Unsigned_32;
   begin
      Report_Header;
      for Bus in Unsigned_32 range 0 .. 255 loop
         for Slot in Unsigned_32 range 0 .. 31 loop
            Id := Config_Read (Bus, Slot, 0, 16#00#);
            if (Id and 16#FFFF#) /= 16#FFFF# then
               Header := Shift_Right (Config_Read (Bus, Slot, 0, 16#0C#), 16)
                         and 16#FF#;
               if (Header and 16#80#) /= 0 then
                  Funcs := 8;
               else
                  Funcs := 1;
               end if;
               for F in Unsigned_32 range 0 .. Funcs - 1 loop
                  Id := Config_Read (Bus, Slot, F, 16#00#);
                  if (Id and 16#FFFF#) /= 16#FFFF# then
                     Report_Device (Bus, Slot, F, Id,
                                    Config_Read (Bus, Slot, F, 16#08#));
                     Found := Found + 1;
                  end if;
               end loop;
            end if;
         end loop;
      end loop;
      Report_Summary (Found, Pcie_Active);
      return Found;
   end Enumerate;
end Pci;
