--  Zhaoxu OS - SCSI command layer over ATAPI, SPARK/Ada.
--
--  ATAPI is packet SCSI: the transport lives in drivers/ide.adb and this
--  package builds the CDBs (INQUIRY, READ CAPACITY(10), READ(10)) and
--  parses the replies.  Every ATAPI device found by the IDE driver becomes
--  one SCSI device.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;
with System.Address_To_Access_Conversions;
with Ide;
use type Interfaces.C.int;

package body Scsi is
   Max_Devices : constant := 4;
   Block_Size  : constant := 2048;

   type Text8  is array (0 .. 8) of Character;
   type Text16 is array (0 .. 16) of Character;
   type Text4  is array (0 .. 4) of Character;
   type Byte_Buf is array (Natural range <>) of Unsigned_8;

   type Device_Record is record
      Used     : Boolean := False;
      Ide_Dev  : Interfaces.C.int := 0;
      Capacity : Unsigned_64 := 0;
   end record;

   G_Devices : array (0 .. Max_Devices - 1) of Device_Record;
   G_Count   : Unsigned_32 := 0;

   G_Vendor   : array (0 .. Max_Devices - 1) of aliased Text8;
   G_Product  : array (0 .. Max_Devices - 1) of aliased Text16;
   G_Revision : array (0 .. Max_Devices - 1) of aliased Text4;
   G_Empty    : aliased Text8 := (others => Character'Val (0));

   procedure Write8 (A : Unsigned_64; Value : Unsigned_8) is
      package U8_Conv is new System.Address_To_Access_Conversions (Unsigned_8);
   begin
      U8_Conv.To_Pointer (To_Address (Integer_Address (A))).all := Value;
   end Write8;

   function Read8 (A : Unsigned_64) return Unsigned_8 is
      package U8_Conv is new System.Address_To_Access_Conversions (Unsigned_8);
   begin
      return U8_Conv.To_Pointer (To_Address (Integer_Address (A))).all;
   end Read8;

   function To_U64 (Addr : System.Address) return Unsigned_64 is
     (Unsigned_64 (To_Integer (Addr)));

   procedure Zero (Addr : System.Address; Len : Unsigned_32) is
      Base : constant Unsigned_64 := To_U64 (Addr);
   begin
      for I in Unsigned_32 range 0 .. Len - 1 loop
         Write8 (Base + Unsigned_64 (I), 0);
      end loop;
   end Zero;

   procedure Copy_Text (Src : System.Address; Src_Off : Natural;
                        Dst : out Text8; Len : Natural) is
      Base : constant Unsigned_64 := To_U64 (Src);
   begin
      for I in 0 .. Len - 1 loop
         Dst (I) := Character'Val (Natural (Read8 (Base + Unsigned_64 (Src_Off + I))));
      end loop;
      for I in reverse 0 .. Len - 1 loop
         if Dst (I) = ' ' or else Dst (I) = Character'Val (0) then
            Dst (I) := Character'Val (0);
         else
            exit;
         end if;
      end loop;
      Dst (Len) := Character'Val (0);
   end Copy_Text;

   procedure Copy_Text16 (Src : System.Address; Src_Off : Natural;
                          Dst : out Text16; Len : Natural) is
      Base : constant Unsigned_64 := To_U64 (Src);
   begin
      for I in 0 .. Len - 1 loop
         Dst (I) := Character'Val (Natural (Read8 (Base + Unsigned_64 (Src_Off + I))));
      end loop;
      for I in reverse 0 .. Len - 1 loop
         if Dst (I) = ' ' or else Dst (I) = Character'Val (0) then
            Dst (I) := Character'Val (0);
         else
            exit;
         end if;
      end loop;
      Dst (Len) := Character'Val (0);
   end Copy_Text16;

   procedure Copy_Text4 (Src : System.Address; Src_Off : Natural;
                         Dst : out Text4; Len : Natural) is
      Base : constant Unsigned_64 := To_U64 (Src);
   begin
      for I in 0 .. Len - 1 loop
         Dst (I) := Character'Val (Natural (Read8 (Base + Unsigned_64 (Src_Off + I))));
      end loop;
      for I in reverse 0 .. Len - 1 loop
         if Dst (I) = ' ' or else Dst (I) = Character'Val (0) then
            Dst (I) := Character'Val (0);
         else
            exit;
         end if;
      end loop;
      Dst (Len) := Character'Val (0);
   end Copy_Text4;

   function Inquiry (Ide_Dev : Interfaces.C.int; Inq : System.Address)
     return Boolean is
      Cdb : aliased Byte_Buf (0 .. 11) :=
        (16#12#, 0, 0, 0, 36, 0, 0, 0, 0, 0, 0, 0);
   begin
      Zero (Inq, 36);
      return Ide.Atapi_Packet (Ide_Dev, Cdb'Address, 12, Inq, 36) = 0;
   end Inquiry;

   function Read_Capacity (Ide_Dev : Interfaces.C.int;
                           Cap : System.Address) return Unsigned_64 is
      Cdb : aliased Byte_Buf (0 .. 11) :=
        (16#25#, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
      Base : constant Unsigned_64 := To_U64 (Cap);
      Last : Unsigned_32;
   begin
      Zero (Cap, 8);
      if Ide.Atapi_Packet (Ide_Dev, Cdb'Address, 12, Cap, 8) /= 0 then
         return 0;
      end if;
      Last := Shift_Left (Unsigned_32 (Read8 (Base)), 24)
              or Shift_Left (Unsigned_32 (Read8 (Base + 1)), 16)
              or Shift_Left (Unsigned_32 (Read8 (Base + 2)), 8)
              or Unsigned_32 (Read8 (Base + 3));
      return Unsigned_64 (Last) + 1;
   end Read_Capacity;

   --  public API ----------------------------------------------------------

   function Init return Interfaces.C.int is
      Devs : constant Interfaces.C.int := Ide.Init;
      Inq  : aliased Byte_Buf (0 .. 63);
      Cap  : aliased Byte_Buf (0 .. 15);
      Index : Unsigned_32 := 0;
   begin
      G_Count := 0;
      for I in G_Devices'Range loop
         G_Devices (I) := (Used => False, Ide_Dev => 0, Capacity => 0);
      end loop;
      if Devs <= 0 then
         return 0;
      end if;

      for D in Interfaces.C.int range 0 .. Devs - 1 loop
         if Index < Max_Devices
           and then Ide.Drive_Atapi (D) = 1
         then
            if Inquiry (D, Inq'Address) then
               declare
                  N : constant Natural := Natural (Index);
                  Drec : Device_Record;
               begin
                  Drec.Used := True;
                  Drec.Ide_Dev := D;
                  Drec.Capacity := Read_Capacity (D, Cap'Address);
                  Copy_Text (Inq'Address, 8, G_Vendor (N), 8);
                  Copy_Text16 (Inq'Address, 16, G_Product (N), 16);
                  Copy_Text4 (Inq'Address, 32, G_Revision (N), 4);
                  G_Devices (N) := Drec;
                  G_Count := G_Count + 1;
                  Index := Index + 1;
               end;
            end if;
         end if;
      end loop;
      return Interfaces.C.int (G_Count);
   end Init;

   function Device_Count return Interfaces.C.int is
   begin
      return Interfaces.C.int (G_Count);
   end Device_Count;

   function Valid (Dev : Interfaces.C.int) return Boolean is
   begin
      return Dev >= 0 and then Unsigned_32 (Dev) < G_Count;
   end Valid;

   function Vendor (Dev : Interfaces.C.int) return System.Address is
   begin
      if not Valid (Dev) then
         return G_Empty'Address;
      end if;
      return G_Vendor (Natural (Dev))'Address;
   end Vendor;

   function Product (Dev : Interfaces.C.int) return System.Address is
   begin
      if not Valid (Dev) then
         return G_Empty'Address;
      end if;
      return G_Product (Natural (Dev))'Address;
   end Product;

   function Revision (Dev : Interfaces.C.int) return System.Address is
   begin
      if not Valid (Dev) then
         return G_Empty'Address;
      end if;
      return G_Revision (Natural (Dev))'Address;
   end Revision;

   function Capacity (Dev : Interfaces.C.int) return Unsigned_64 is
   begin
      if not Valid (Dev) then
         return 0;
      end if;
      return G_Devices (Natural (Dev)).Capacity;
   end Capacity;

   function Read_Blocks (Dev    : Interfaces.C.int;
                         Lba    : Unsigned_64;
                         Count  : Unsigned_32;
                         Buffer : System.Address)
     return Interfaces.C.int is
      D          : Device_Record;
      Cur_Lba    : Unsigned_64 := Lba;
      Remaining  : Unsigned_32 := Count;
      Destination : Unsigned_64 := Unsigned_64 (To_Integer (Buffer));
   begin
      if not Valid (Dev) or else Buffer = System.Null_Address then
         return -1;
      end if;
      D := G_Devices (Natural (Dev));
      while Remaining > 0 loop
         declare
            Cdb : aliased Byte_Buf (0 .. 11) :=
              (16#28#, 0,
               Unsigned_8 (Shift_Right (Cur_Lba, 24) and 16#FF#),
               Unsigned_8 (Shift_Right (Cur_Lba, 16) and 16#FF#),
               Unsigned_8 (Shift_Right (Cur_Lba, 8) and 16#FF#),
               Unsigned_8 (Cur_Lba and 16#FF#),
               0, 0, 1, 0, 0, 0);
         begin
            if Ide.Atapi_Packet (D.Ide_Dev, Cdb'Address, 12,
                                 System.Address
                                   (To_Address (Integer_Address (Destination))),
                                 Block_Size) /= 0
            then
               return -1;
            end if;
         end;
         Destination := Destination + Block_Size;
         Cur_Lba := Cur_Lba + 1;
         Remaining := Remaining - 1;
      end loop;
      return 0;
   end Read_Blocks;
end Scsi;
