--  Zhaoxu OS - PCI Express enhanced configuration access (ACPI MCFG)
--
--  Walks the RSDP/XSDT for the MCFG table and uses the memory-mapped ECAM
--  window for config space when the firmware provides one.  Falls back to
--  the legacy 0xCF8/0xCFC pair otherwise.  Part of the SPARK driver layer.

with Interfaces; use Interfaces;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;

package body Pcie is
   --  ACPI tables may live above 4 GiB and are identity-mapped by the
   --  kernel; ECAM itself is MMIO and stays restricted to low memory
   --  until the MMIO window above 4 GiB is mapped explicitly.
   Table_Limit : constant Unsigned_64 := 16#10_0000_0000#;

   function Mmio_Read8 (A : Unsigned_64) return Unsigned_8
     with Import, Convention => C, External_Name => "mmio_read8";
   function Mmio_Read16 (A : Unsigned_64) return Unsigned_16
     with Import, Convention => C, External_Name => "mmio_read16";
   function Mmio_Read32 (A : Unsigned_64) return Unsigned_32
     with Import, Convention => C, External_Name => "mmio_read32";
   procedure Mmio_Write32 (A : Unsigned_64; Value : Unsigned_32)
     with Import, Convention => C, External_Name => "mmio_write32";

   G_Base     : Unsigned_64 := 0;
   G_Bus_From : Unsigned_32 := 0;
   G_Bus_To   : Unsigned_32 := 0;
   G_Active   : Unsigned_32 := 0;

   procedure Outl (Port : Unsigned_16; Value : Unsigned_32)
     with Import, Convention => C, External_Name => "outl_port";
   procedure Outb (Port : Unsigned_16; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "outb_port";
   function Inl (Port : Unsigned_16) return Unsigned_32
     with Import, Convention => C, External_Name => "inl_port";

   function To_Addr (Value : Unsigned_64) return Address is
     (To_Address (Integer_Address (Value)));

   function Read_U8 (A : Unsigned_64) return Unsigned_8 is
     (Mmio_Read8 (A));

   function Read_U16 (A : Unsigned_64) return Unsigned_16 is
     (Mmio_Read16 (A));

   function Read_U32 (A : Unsigned_64) return Unsigned_32 is
     (Mmio_Read32 (A));

   function Read_U64 (A : Unsigned_64) return Unsigned_64 is
      Lo : constant Unsigned_64 := Unsigned_64 (Read_U32 (A));
      Hi : constant Unsigned_64 := Unsigned_64 (Read_U32 (A + 4));
   begin
      return Lo or Shift_Left (Hi, 32);
   end Read_U64;

   function Sig (A : Unsigned_64) return Unsigned_32 is
   begin
      return Read_U32 (A);
   end Sig;

   --  Walk one table level looking for a 4-byte signature.
   function Find_Table (Root_Phys : Unsigned_64;
                        Want      : Unsigned_32) return Unsigned_64 is
      Len  : Unsigned_32;
      Ents : Unsigned_32;
   begin
      if Root_Phys = 0 or else Root_Phys >= Table_Limit then
         return 0;
      end if;
      Len := Read_U32 (Root_Phys + 4);
      if Len < 36 or else Len > 16#10_0000# then
         return 0;
      end if;
      Ents := (Len - 36) / 8;
      for I in Unsigned_32 range 0 .. Ents - 1 loop
         declare
            T : constant Unsigned_64 := Read_U64 (Root_Phys + 36 + Unsigned_64 (I) * 8);
         begin
            if T /= 0 and then T < Table_Limit and then Sig (T) = Want then
               return T;
            end if;
         end;
      end loop;
      return 0;
   end Find_Table;

   procedure Init (Rsdp : Unsigned_64) is
      C_Mcfg : constant Unsigned_32 :=
        Character'Pos ('M') or Shift_Left (Unsigned_32 (Character'Pos ('C')), 8)
        or Shift_Left (Unsigned_32 (Character'Pos ('F')), 16)
        or Shift_Left (Unsigned_32 (Character'Pos ('G')), 24);
      C_Xsdt : constant Unsigned_32 :=
        Character'Pos ('X') or Shift_Left (Unsigned_32 (Character'Pos ('S')), 8)
        or Shift_Left (Unsigned_32 (Character'Pos ('D')), 16)
        or Shift_Left (Unsigned_32 (Character'Pos ('T')), 24);
      C_Rsdt : constant Unsigned_32 :=
        Character'Pos ('R') or Shift_Left (Unsigned_32 (Character'Pos ('S')), 8)
        or Shift_Left (Unsigned_32 (Character'Pos ('D')), 16)
        or Shift_Left (Unsigned_32 (Character'Pos ('T')), 24);
      Rev  : Unsigned_8;
      Root : Unsigned_64 := 0;
      Mcfg : Unsigned_64 := 0;
      Len  : Unsigned_32;
      Ents : Unsigned_32;
   begin
      G_Active := 0;
      G_Base := 0;
      if Rsdp = 0 or else Rsdp >= Table_Limit then
         return;
      end if;
      if Sig (Rsdp) /= 16#20525344# then --  "RSD "
         return;
      end if;
      Rev := Read_U8 (Rsdp + 15);
      if Rev >= 2 then
         Root := Read_U64 (Rsdp + 24);
         if Root /= 0 and then Sig (Root) = C_Xsdt then
            Len := Read_U32 (Root + 4);
            if Len >= 36 and then Len <= 16#10_0000# then
               Ents := (Len - 36) / 8;
               for I in Unsigned_32 range 0 .. Ents - 1 loop
                  declare
                     T : constant Unsigned_64 :=
                       Read_U64 (Root + 36 + Unsigned_64 (I) * 8);
                  begin
                     if T /= 0 and then T < Table_Limit
                       and then Sig (T) = C_Mcfg
                     then
                        Mcfg := T;
                     end if;
                  end;
               end loop;
            end if;
         end if;
      elsif Root = 0 then
         Root := Unsigned_64 (Read_U32 (Rsdp + 16));
         if Root /= 0 and then Sig (Root) = C_Rsdt then
            Len := Read_U32 (Root + 4);
            if Len >= 36 and then Len <= 16#10_0000# then
               Ents := (Len - 36) / 4;
               for I in Unsigned_32 range 0 .. Ents - 1 loop
                  declare
                     T : constant Unsigned_64 :=
                       Unsigned_64 (Read_U32 (Root + 36 + Unsigned_64 (I) * 4));
                  begin
                     if T /= 0 and then T < Table_Limit
                       and then Sig (T) = C_Mcfg
                     then
                        Mcfg := T;
                     end if;
                  end;
               end loop;
            end if;
         end if;
      end if;

      if Root = 0 then
         return;
      end if;

      --  The XSDT loop above may not have found MCFG; fall back to RSDT.
      if Mcfg = 0 then
         declare
            Rsdt : constant Unsigned_64 := Unsigned_64 (Read_U32 (Rsdp + 16));
         begin
            if Rsdt /= 0 and then Rsdt < Table_Limit
              and then Sig (Rsdt) = C_Rsdt
            then
               Len := Read_U32 (Rsdt + 4);
               if Len >= 36 and then Len <= 16#10_0000# then
                  Ents := (Len - 36) / 4;
                  for I in Unsigned_32 range 0 .. Ents - 1 loop
                     declare
                        T : constant Unsigned_64 :=
                          Unsigned_64 (Read_U32 (Rsdt + 36 + Unsigned_64 (I) * 4));
                     begin
                        if T /= 0 and then T < Table_Limit
                          and then Sig (T) = C_Mcfg
                        then
                           Mcfg := T;
                        end if;
                     end;
                  end loop;
               end if;
            end if;
         end;
      end if;

      if Mcfg = 0 then
         return;
      end if;

      Len := Read_U32 (Mcfg + 4);
      if Len < 60 then
         return;
      end if;
      Ents := (Len - 44) / 16;
      for I in Unsigned_32 range 0 .. Ents - 1 loop
         declare
            E : constant Unsigned_64 := Mcfg + 44 + Unsigned_64 (I) * 16;
            B : constant Unsigned_64 := Read_U64 (E);
            From_Bus : constant Unsigned_32 := Unsigned_32 (Read_U8 (E + 10));
            To_Bus   : constant Unsigned_32 := Unsigned_32 (Read_U8 (E + 11));
         begin
            if G_Base = 0 and then B /= 0 and then B < 16#1_0000_0000# then
               G_Base := B;
               G_Bus_From := From_Bus;
               G_Bus_To := To_Bus;
               G_Active := 1;
            end if;
         end;
      end loop;
   end Init;

   function Active return Unsigned_32 is (G_Active);

   function Ecam_Base return Unsigned_64 is (G_Base);

   function Config_Read (Bus  : Unsigned_32;
                         Dev  : Unsigned_32;
                         Func : Unsigned_32;
                         Off  : Unsigned_32)
     return Unsigned_32 is
   begin
      if G_Active = 1 and then Bus >= G_Bus_From and then Bus <= G_Bus_To then
         declare
            A : constant Unsigned_64 :=
              G_Base
              + Shift_Left (Unsigned_64 (Bus - G_Bus_From), 20)
              + Shift_Left (Unsigned_64 (Dev), 15)
              + Shift_Left (Unsigned_64 (Func), 12)
              + Unsigned_64 (Off and 16#FFC#);
         begin
            if A < 16#1_0000_0000# then
               return Read_U32 (A);
            end if;
         end;
      end if;
      Outl (16#CF8#,
            16#8000_0000#
            or Shift_Left (Bus, 16)
            or Shift_Left (Dev, 11)
            or Shift_Left (Func, 8)
            or (Off and 16#FC#));
      return Inl (16#CFC#);
   end Config_Read;

   procedure Config_Write (Bus   : Unsigned_32;
                           Dev   : Unsigned_32;
                           Func  : Unsigned_32;
                           Off   : Unsigned_32;
                           Value : Unsigned_32) is
   begin
      if G_Active = 1 and then Bus >= G_Bus_From and then Bus <= G_Bus_To then
         declare
            A : constant Unsigned_64 :=
              G_Base
              + Shift_Left (Unsigned_64 (Bus - G_Bus_From), 20)
              + Shift_Left (Unsigned_64 (Dev), 15)
              + Shift_Left (Unsigned_64 (Func), 12)
              + Unsigned_64 (Off and 16#FFC#);
         begin
            if A < 16#1_0000_0000# then
               Mmio_Write32 (A, Value);
               return;
            end if;
         end;
      end if;
      Outl (16#CF8#,
            16#8000_0000#
            or Shift_Left (Bus, 16)
            or Shift_Left (Dev, 11)
            or Shift_Left (Func, 8)
            or (Off and 16#FC#));
      Outl (16#CFC#, Value);
   end Config_Write;
end Pcie;
