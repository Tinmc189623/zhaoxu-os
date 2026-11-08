--  Zhaoxu OS - PIIX4 IDE (ATA/ATAPI) driver, SPARK/Ada.
--
--  Polled PIO implementation covering both legacy channels: ATA IDENTIFY
--  and READ for hard disks, IDENTIFY PACKET DEVICE plus PACKET command
--  passthrough for ATAPI devices.  The q35 machine exposes its disks
--  through AHCI instead, so this driver is exercised on -M pc and on
--  real hardware with a PIIX4-compatible controller.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;
with System.Address_To_Access_Conversions;
use type Interfaces.C.int;

package body Ide is
   Max_Drives : constant := 4;
   Sector_Size : constant := 512;

   --  status bits
   St_Bsy : constant := 16#80#;
   St_Drdy : constant := 16#40#;
   St_Df  : constant := 16#20#;
   St_Drq : constant := 16#08#;
   St_Err : constant := 16#01#;

   --  register offsets
   R_Data  : constant := 0;
   R_Error : constant := 1;
   R_Feat  : constant := 1;
   R_Count : constant := 2;
   R_Lba0  : constant := 3;
   R_Lba1  : constant := 4;
   R_Lba2  : constant := 5;
   R_Drive : constant := 6;
   R_Status : constant := 7;
   R_Command : constant := 7;

   Cmd_Read     : constant := 16#20#;
   Cmd_Read_Lba48 : constant := 16#24#;
   Cmd_Identify : constant := 16#EC#;
   Cmd_Identify_Packet : constant := 16#A1#;
   Cmd_Packet   : constant := 16#A0#;

   type Model_Text is array (0 .. 40) of Character;
   type Word_Array is array (Natural range <>) of Unsigned_16;

   type Drive_Record is record
      Present : Boolean := False;
      Atapi   : Boolean := False;
      Bus     : Unsigned_32 := 0;
      Unit    : Unsigned_32 := 0;
      Sectors : Unsigned_64 := 0;
   end record;

   type Drive_Array is array (0 .. Max_Drives - 1) of Drive_Record;

   G_Drives : Drive_Array;
   G_Count  : Unsigned_32 := 0;
   G_Models : array (0 .. Max_Drives - 1) of aliased Model_Text;
   G_Empty  : aliased Model_Text := (others => Character'Val (0));

   package U8_Conv is new System.Address_To_Access_Conversions (Unsigned_8);
   package U16_Conv is new System.Address_To_Access_Conversions (Unsigned_16);

   procedure Outb (Port : Unsigned_16; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "outb_port";
   procedure Outw (Port : Unsigned_16; Value : Unsigned_16)
     with Import, Convention => C, External_Name => "outw_port";
   function Inb (Port : Unsigned_16) return Unsigned_8
     with Import, Convention => C, External_Name => "inb_port";
   function Inw (Port : Unsigned_16) return Unsigned_16
     with Import, Convention => C, External_Name => "inw_port";

   --  memory helpers ------------------------------------------------------

   function Addr_Of (Value : Unsigned_64) return Address is
     (To_Address (Integer_Address (Value)));

   function Read8 (A : Unsigned_64) return Unsigned_8 is
     (U8_Conv.To_Pointer (Addr_Of (A)).all);

   procedure Write8 (A : Unsigned_64; Value : Unsigned_8) is
   begin
      U8_Conv.To_Pointer (Addr_Of (A)).all := Value;
   end Write8;

   procedure Zero (A : Unsigned_64; Bytes : Unsigned_32) is
   begin
      for I in Unsigned_32 range 0 .. Bytes - 1 loop
         Write8 (A + Unsigned_64 (I), 0);
      end loop;
   end Zero;

   --  channel register access ---------------------------------------------

   type Channel is record
      Base   : Unsigned_16 := 0;
      Ctrl   : Unsigned_16 := 0;
      Active : Boolean := False;
   end record;

   G_Channels : array (0 .. 1) of Channel;

   function Status (Ch : Channel) return Unsigned_8 is
   begin
      return Inb (Ch.Base + R_Status);
   end Status;

   procedure Delay_400ns (Ch : Channel) is
      Dummy : Unsigned_8;
   begin
      for I in 1 .. 4 loop
         Dummy := Inb (Ch.Ctrl);
      end loop;
   end Delay_400ns;

   function Wait_Not_Busy (Ch : Channel) return Boolean is
      S : Unsigned_8;
   begin
      for I in Integer range 1 .. 1_000_000 loop
         S := Status (Ch);
         if (S and St_Bsy) = 0 then
            return True;
         end if;
      end loop;
      return False;
   end Wait_Not_Busy;

   function Wait_Drq (Ch : Channel) return Boolean is
      S : Unsigned_8;
   begin
      for I in Integer range 1 .. 2_000_000 loop
         S := Status (Ch);
         if (S and St_Err) /= 0 or else (S and St_Df) /= 0 then
            return False;
         end if;
         if (S and St_Bsy) = 0 and then (S and St_Drq) /= 0 then
            return True;
         end if;
      end loop;
      return False;
   end Wait_Drq;

   procedure Choose_Drive (Ch : Channel; Unit : Unsigned_32) is
   begin
      Outb (Ch.Base + R_Drive,
            16#A0# or Unsigned_8 (Shift_Left (Unit, 4)));
      Delay_400ns (Ch);
   end Choose_Drive;

   procedure Clear_Regs (Ch : Channel) is
   begin
      Outb (Ch.Base + R_Feat, 0);
      Outb (Ch.Base + R_Count, 0);
      Outb (Ch.Base + R_Lba0, 0);
      Outb (Ch.Base + R_Lba1, 0);
      Outb (Ch.Base + R_Lba2, 0);
   end Clear_Regs;

   procedure Read_Words (Ch : Channel; Words : out Word_Array) is
   begin
      for I in Words'Range loop
         Words (I) := Inw (Ch.Base + R_Data);
      end loop;
   end Read_Words;

   procedure Read_Words_To (Ch : Channel; Buffer : Unsigned_64;
                            Words : Unsigned_32) is
      W : Unsigned_16;
   begin
      for I in Unsigned_32 range 0 .. Words - 1 loop
         W := Inw (Ch.Base + R_Data);
         Write8 (Buffer + Unsigned_64 (I) * 2, Unsigned_8 (W and 16#FF#));
         Write8 (Buffer + Unsigned_64 (I) * 2 + 1,
                 Unsigned_8 (Shift_Right (W, 8)));
      end loop;
   end Read_Words_To;

   procedure Build_Model (Words : Word_Array; Model : out Model_Text) is
      W : Unsigned_16;
   begin
      for I in 0 .. 39 loop
         W := Words (27 + I / 2);
         if I mod 2 = 0 then
            Model (I) := Character'Val (Natural (Shift_Right (W, 8)));
         else
            Model (I) := Character'Val (Natural (W and 16#FF#));
         end if;
      end loop;
      Model (40) := Character'Val (0);
      for I in reverse 0 .. 39 loop
         if Model (I) = ' ' or else Model (I) = Character'Val (0) then
            Model (I) := Character'Val (0);
         else
            exit;
         end if;
      end loop;
   end Build_Model;

   procedure Identify_Drive (Ch : Channel; Unit : Unsigned_32;
                             Is_Atapi : in out Boolean; Found : out Boolean) is
      Words : Word_Array (0 .. 255);
      S     : Unsigned_8;
      Sig1  : Unsigned_8;
      Sig2  : Unsigned_8;
   begin
      Found := False;
      Choose_Drive (Ch, Unit);
      if not Wait_Not_Busy (Ch) then
         return;
      end if;
      --  signature registers distinguish ATA from ATAPI
      Sig1 := Inb (Ch.Base + R_Lba1);
      Sig2 := Inb (Ch.Base + R_Lba2);
      if Sig1 = 16#14# and then Sig2 = 16#EB# then
         Is_Atapi := True;
      else
         Is_Atapi := False;
      end if;

      Clear_Regs (Ch);
      if Is_Atapi then
         Outb (Ch.Base + R_Command, Cmd_Identify_Packet);
      else
         Outb (Ch.Base + R_Command, Cmd_Identify);
      end if;

      if not Wait_Drq (Ch) then
         return;
      end if;
      Read_Words (Ch, Words);
      S := Status (Ch);
      if (S and (St_Err or St_Df)) /= 0 then
         return;
      end if;
      Found := True;
      declare
         D : Drive_Record;
         Index : constant Unsigned_32 := G_Count;
      begin
         D.Present := True;
         D.Atapi := Is_Atapi;
         D.Bus := (if Ch.Base = 16#1F0# then 0 else 1);
         D.Unit := Unit;
         if not Is_Atapi then
            D.Sectors :=
              Unsigned_64 (Words (60))
              or Shift_Left (Unsigned_64 (Words (61)), 16);
            if D.Sectors = 0 then
               D.Sectors :=
                 Unsigned_64 (Words (100))
                 or Shift_Left (Unsigned_64 (Words (101)), 16)
                 or Shift_Left (Unsigned_64 (Words (102)), 32)
                 or Shift_Left (Unsigned_64 (Words (103)), 48);
            end if;
         end if;
         Build_Model (Words, G_Models (Natural (Index)));
         G_Drives (Natural (Index)) := D;
         G_Count := G_Count + 1;
      end;
   end Identify_Drive;

   procedure Probe_Channel (Ch : Channel) is
      Sig : Unsigned_8;
      Is_Atapi : Boolean;
      Found : Boolean;
   begin
      Sig := Inb (Ch.Ctrl);
      if Sig = 16#FF# then
         return; --  floating bus: no controller on this channel
      end if;
      for Unit in Unsigned_32 range 0 .. 1 loop
         Choose_Drive (Ch, Unit);
         declare
            S1 : constant Unsigned_8 := Inb (Ch.Base + R_Lba1);
            S2 : constant Unsigned_8 := Inb (Ch.Base + R_Lba2);
         begin
            if (S1 = 0 and then S2 = 0)
              or else (S1 = 16#14# and then S2 = 16#EB#)
              or else (S1 = 16#3C# and then S2 = 16#3C#)
            then
               Is_Atapi := False;
               Identify_Drive (Ch, Unit, Is_Atapi, Found);
            end if;
         end;
      end loop;
   end Probe_Channel;

   --  public API ----------------------------------------------------------

   function Init return Interfaces.C.int is
   begin
      G_Count := 0;
      for I in G_Drives'Range loop
         G_Drives (I) :=
           (Present => False, Atapi => False, Bus => 0, Unit => 0,
            Sectors => 0);
      end loop;
      G_Channels :=
        ((Base => 16#1F0#, Ctrl => 16#3F6#, Active => True),
         (Base => 16#170#, Ctrl => 16#376#, Active => True));
      for C in 0 .. 1 loop
         Probe_Channel (G_Channels (C));
      end loop;
      return Interfaces.C.int (G_Count);
   end Init;

   function Drive_Count return Interfaces.C.int is
   begin
      return Interfaces.C.int (G_Count);
   end Drive_Count;

   function Valid (Drive : Interfaces.C.int) return Boolean is
   begin
      return Drive >= 0 and then Unsigned_32 (Drive) < G_Count;
   end Valid;

   function Drive_Atapi (Drive : Interfaces.C.int) return Interfaces.C.int is
   begin
      if not Valid (Drive) then
         return 0;
      end if;
      return (if G_Drives (Natural (Drive)).Atapi then 1 else 0);
   end Drive_Atapi;

   function Drive_Bus (Drive : Interfaces.C.int) return Interfaces.C.int is
   begin
      if not Valid (Drive) then
         return -1;
      end if;
      return Interfaces.C.int (G_Drives (Natural (Drive)).Bus);
   end Drive_Bus;

   function Drive_Unit (Drive : Interfaces.C.int) return Interfaces.C.int is
   begin
      if not Valid (Drive) then
         return -1;
      end if;
      return Interfaces.C.int (G_Drives (Natural (Drive)).Unit);
   end Drive_Unit;

   function Drive_Sectors (Drive : Interfaces.C.int) return Unsigned_64 is
   begin
      if not Valid (Drive) then
         return 0;
      end if;
      return G_Drives (Natural (Drive)).Sectors;
   end Drive_Sectors;

   function Drive_Model (Drive : Interfaces.C.int) return System.Address is
   begin
      if not Valid (Drive) then
         return G_Empty'Address;
      end if;
      return G_Models (Natural (Drive))'Address;
   end Drive_Model;

   function Read_Block (Drive  : Interfaces.C.int;
                        Lba    : Unsigned_64;
                        Count  : Unsigned_32;
                        Buffer : System.Address)
     return Interfaces.C.int is
      D : Drive_Record;
      Ch : Channel;
      Remaining : Unsigned_32 := Count;
      Cur_Lba   : Unsigned_64 := Lba;
      Source    : Unsigned_64 := Unsigned_64 (To_Integer (Buffer));
      Chunk     : Unsigned_32;
   begin
      if not Valid (Drive) or else Buffer = System.Null_Address then
         return -1;
      end if;
      D := G_Drives (Natural (Drive));
      if D.Atapi then
         return -1;
      end if;
      Ch := G_Channels (Natural (D.Bus));
      while Remaining > 0 loop
         if Remaining > 128 then
            Chunk := 128;
         else
            Chunk := Remaining;
         end if;
         if Cur_Lba + Unsigned_64 (Chunk) > D.Sectors then
            return -1;
         end if;

         Choose_Drive (Ch, D.Unit);
         if not Wait_Not_Busy (Ch) then
            return -1;
         end if;
         Outb (Ch.Base + R_Feat, 0);
         Outb (Ch.Base + R_Count, Unsigned_8 (Chunk and 16#FF#));
         Outb (Ch.Base + R_Lba0, Unsigned_8 (Cur_Lba and 16#FF#));
         Outb (Ch.Base + R_Lba1,
               Unsigned_8 (Shift_Right (Cur_Lba, 8) and 16#FF#));
         Outb (Ch.Base + R_Lba2,
               Unsigned_8 (Shift_Right (Cur_Lba, 16) and 16#FF#));
         Outb (Ch.Base + R_Drive,
               16#E0# or Unsigned_8 (Shift_Left (D.Unit, 4))
               or Unsigned_8 (Shift_Right (Cur_Lba, 24) and 16#0F#));
         Delay_400ns (Ch);
         Outb (Ch.Base + R_Command, Cmd_Read);

         for Sector in Unsigned_32 range 0 .. Chunk - 1 loop
            if not Wait_Drq (Ch) then
               return -1;
            end if;
            Read_Words_To (Ch, Source, 256);
            Source := Source + Sector_Size;
         end loop;

         Cur_Lba := Cur_Lba + Unsigned_64 (Chunk);
         Remaining := Remaining - Chunk;
      end loop;
      return 0;
   end Read_Block;

   function Atapi_Packet (Drive    : Interfaces.C.int;
                          Cdb      : System.Address;
                          Cdb_Len  : Unsigned_32;
                          Data     : System.Address;
                          Data_Len : Unsigned_32)
     return Interfaces.C.int is
      D : Drive_Record;
      Ch : Channel;
      Cdb_Addr : Unsigned_64;
      Data_Addr : Unsigned_64;
      W : Unsigned_16;
      Byte_Count : Unsigned_32;
      To_Read : Unsigned_32;
   begin
      if not Valid (Drive) or else Cdb = System.Null_Address then
         return -1;
      end if;
      D := G_Drives (Natural (Drive));
      if not D.Atapi then
         return -1;
      end if;
      if Cdb_Len < 12 or else Cdb_Len > 16 then
         return -1;
      end if;
      Ch := G_Channels (Natural (D.Bus));

      Choose_Drive (Ch, D.Unit);
      if not Wait_Not_Busy (Ch) then
         return -1;
      end if;
      Outb (Ch.Base + R_Feat, 0);
      Outb (Ch.Base + R_Count, 0);
      Outb (Ch.Base + R_Lba0, 0);
      Outb (Ch.Base + R_Lba1, 16#08#);
      Outb (Ch.Base + R_Lba2, 16#00#);
      Outb (Ch.Base + R_Command, Cmd_Packet);

      if not Wait_Drq (Ch) then
         return -1;
      end if;

      Cdb_Addr := Unsigned_64 (To_Integer (Cdb));
      for I in 0 .. 5 loop
         W := Unsigned_16 (Read8 (Cdb_Addr + Unsigned_64 (I) * 2))
              or Shift_Left (
                   Unsigned_16 (Read8 (Cdb_Addr + Unsigned_64 (I) * 2 + 1)),
                   8);
         Outw (Ch.Base + R_Data, W);
      end loop;

      if Data_Len = 0 or else Data = System.Null_Address then
         return 0;
      end if;

      if not Wait_Drq (Ch) then
         return -1;
      end if;

      Byte_Count := Unsigned_32 (Inb (Ch.Base + R_Lba2))
                    or Shift_Left (Unsigned_32 (Inb (Ch.Base + R_Lba1)), 8);
      if Byte_Count = 0 or else Byte_Count > Data_Len then
         To_Read := Data_Len;
      else
         To_Read := Byte_Count;
      end if;

      Data_Addr := Unsigned_64 (To_Integer (Data));
      declare
         Words : constant Unsigned_32 := (To_Read + 1) / 2;
         B0, B1 : Unsigned_8;
      begin
         for I in Unsigned_32 range 0 .. Words - 1 loop
            W := Inw (Ch.Base + R_Data);
            B0 := Unsigned_8 (W and 16#FF#);
            B1 := Unsigned_8 (Shift_Right (W, 8));
            if I * 2 < To_Read then
               Write8 (Data_Addr + Unsigned_64 (I) * 2, B0);
            end if;
            if I * 2 + 1 < To_Read then
               Write8 (Data_Addr + Unsigned_64 (I) * 2 + 1, B1);
            end if;
         end loop;
      end;

      --  wait for the command to finish (DRQ clear) or fail
      for I in Integer range 1 .. 2_000_000 loop
         declare
            S : constant Unsigned_8 := Status (Ch);
         begin
            if (S and St_Err) /= 0 or else (S and St_Df) /= 0 then
               return -1;
            end if;
            exit when (S and St_Bsy) = 0 and then (S and St_Drq) = 0;
         end;
      end loop;
      return 0;
   end Atapi_Packet;
end Ide;
