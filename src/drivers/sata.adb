--  Zhaoxu OS - SATA link layer reporting, SPARK/Ada.
--
--  Finds the AHCI controller through PCI config space and exposes the
--  HBA capability register, the implemented-port mask and the per-port
--  SSTS link state (detection + negotiated generation).  Actual block
--  transfers are handled by drivers/ahci.adb.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;
use type Interfaces.C.int;

package body Sata is
   Hba_Pi  : constant := 16#0C#;
   Port_Base : constant := 16#100#;
   Port_Step : constant := 16#80#;
   Port_Ssts : constant := 16#28#;

   G_Abar  : Unsigned_64 := 0;
   G_Mask  : Unsigned_32 := 0;
   G_Ports : Unsigned_32 := 0;

   function Pci_Read (Bus, Slot, Func, Off : Unsigned_32)
     return Unsigned_32
     with Import, Convention => C, External_Name => "pci_config_read32";

   function Mmio_Read32 (A : Unsigned_64) return Unsigned_32
     with Import, Convention => C, External_Name => "mmio_read32";

   function Read32 (Addr : Unsigned_64) return Unsigned_32 is
   begin
      return Mmio_Read32 (Addr);
   end Read32;

   function Reg (Offset : Unsigned_32) return Unsigned_32 is
   begin
      return Read32 (G_Abar + Unsigned_64 (Offset));
   end Reg;

   function Port_Reg (Port : Unsigned_32; Offset : Unsigned_32)
     return Unsigned_32 is
   begin
      return Read32 (G_Abar + Port_Base + Unsigned_64 (Port) * Port_Step
                     + Unsigned_64 (Offset));
   end Port_Reg;

   function Init return Interfaces.C.int is
      Id : Unsigned_32;
      Cls : Unsigned_32;
      Bar : Unsigned_32;
      High : Unsigned_32;
      Devices : Unsigned_32 := 0;
      Found : Boolean := False;
   begin
      G_Abar := 0;
      G_Mask := 0;
      G_Ports := 0;

      for Bus in Unsigned_32 range 0 .. 255 loop
         for Slot in Unsigned_32 range 0 .. 31 loop
            for Func in Unsigned_32 range 0 .. 7 loop
               Id := Pci_Read (Bus, Slot, Func, 16#00#);
               if (Id and 16#FFFF#) /= 16#FFFF# then
                  Cls := Pci_Read (Bus, Slot, Func, 16#08#);
                  if (Cls / 16#1_0000_00#) = 16#01#
                    and then ((Cls / 16#1_0000#) mod 256) = 16#06#
                    and then ((Cls / 16#100#) mod 256) = 16#01#
                  then
                     Bar := Pci_Read (Bus, Slot, Func, 16#24#);
                     if (Bar and 16#06#) = 16#04# then
                        High := Pci_Read (Bus, Slot, Func, 16#28#);
                        G_Abar := Shift_Left (Unsigned_64 (High), 32)
                                  or Unsigned_64 (Bar and 16#FFFF_FFF0#);
                     else
                        G_Abar := Unsigned_64 (Bar and 16#FFFF_FFF0#);
                     end if;
                     Found := True;
                  end if;
               end if;
            end loop;
         end loop;
      end loop;

      if not Found or else G_Abar = 0
        or else G_Abar >= 16#1_0000_0000#
      then
         return 0;
      end if;

      G_Mask := Reg (Hba_Pi);
      for Port in Unsigned_32 range 0 .. 31 loop
         if (G_Mask and Shift_Left (Unsigned_32 (1), Natural (Port))) /= 0 then
            G_Ports := G_Ports + 1;
            if (Port_Reg (Port, Port_Ssts) and 16#0F#) = 3 then
               Devices := Devices + 1;
            end if;
         end if;
      end loop;
      return Interfaces.C.int (Devices);
   end Init;

   function Abar return Unsigned_64 is (G_Abar);
   function Port_Mask return Unsigned_32 is (G_Mask);
   function Port_Count return Interfaces.C.int is
     (Interfaces.C.int (G_Ports));

   function Port_Det (Port : Interfaces.C.int) return Unsigned_32 is
   begin
      if Port < 0 or else Port > 31 then
         return 0;
      end if;
      return Port_Reg (Unsigned_32 (Port), Port_Ssts) and 16#0F#;
   end Port_Det;

   function Port_Gen (Port : Interfaces.C.int) return Unsigned_32 is
      Ssts : Unsigned_32;
   begin
      if Port < 0 or else Port > 31 then
         return 0;
      end if;
      Ssts := Port_Reg (Unsigned_32 (Port), Port_Ssts);
      return Shift_Right (Ssts and 16#F0#, 4);
   end Port_Gen;
end Sata;
