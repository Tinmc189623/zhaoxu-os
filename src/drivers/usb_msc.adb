--  Zhaoxu OS - USB Mass Storage Class driver, SPARK/Ada edition.
--
--  Implements the Bulk-Only Transport (BOT) state machine and the SCSI
--  transparent command subset needed for a read/write 512-byte block
--  device: TEST UNIT READY, INQUIRY, READ CAPACITY(10), READ(10), WRITE(10)
--  and SYNCHRONIZE CACHE(10).  Every CBW, CSW and data buffer is a page
--  allocated through dev_alloc_page, so the trusted driver process can hand
--  physical addresses to the host controller directly.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;
with Xhci;
use type Interfaces.C.int;

package body Usb_Msc is
   Max_Disks     : constant := 4;
   Sector_Size   : constant := 512;
   Chunk_Sectors : constant := 8;
   Page_Size     : constant := 4096;
   Max_Model     : constant := 40;

   type Cdb_Array is array (0 .. 15) of Unsigned_8;
   type Model_Text is array (0 .. Max_Model) of Character;

   type Disk_Record is record
      Used             : Boolean := False;
      Xhci_Index       : Interfaces.C.int := 0;
      Slot             : Unsigned_8 := 0;
      Port             : Unsigned_8 := 0;
      Bulk_In          : Unsigned_8 := 0;
      Bulk_Out         : Unsigned_8 := 0;
      Bulk_In_Addr     : Unsigned_8 := 0;
      Bulk_Out_Addr    : Unsigned_8 := 0;
      Interface_Number : Unsigned_8 := 0;
      Sectors          : Unsigned_64 := 0;
      Tag              : Unsigned_32 := 1;
   end record;

   type Disk_Array is array (0 .. Max_Disks - 1) of Disk_Record;

   G_Disks  : Disk_Array;
   G_Count  : Unsigned_32 := 0;
   G_Models : array (0 .. Max_Disks - 1) of aliased Model_Text;
   G_Empty  : aliased Model_Text := (others => Character'Val (0));

   G_Cbw   : Unsigned_64 := 0;
   G_Csw   : Unsigned_64 := 0;
   G_Data  : Unsigned_64 := 0;
   G_Sense : Unsigned_64 := 0;
   G_Setup : Unsigned_64 := 0;

   --  C glue ------------------------------------------------------------

   function Mmio_Read8 (A : Unsigned_64) return Unsigned_8
     with Import, Convention => C, External_Name => "mmio_read8";
   procedure Mmio_Write8 (A : Unsigned_64; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "mmio_write8";

   function Pmm_Alloc_Page return Unsigned_64
     with Import, Convention => C, External_Name => "pmm_alloc_page";

   procedure Buffer_Zero (Dst : Address; Bytes : Unsigned_32)
     with Import, Convention => C, External_Name => "buffer_zero";

   procedure Usb_Delay_Ms (Ms : Unsigned_32)
     with Import, Convention => C, External_Name => "usb_delay_ms";

   procedure Log_Disk (Index : Unsigned_32; Model : Address;
                       Sectors : Unsigned_64)
     with Import, Convention => C, External_Name => "usb_msc_log_disk";
   procedure Log_Summary (Count : Unsigned_32)
     with Import, Convention => C, External_Name => "usb_msc_log_summary";
   procedure Log_Error (Stage : Unsigned_32; Status : Interfaces.C.int)
     with Import, Convention => C, External_Name => "usb_msc_log_error";

   --  Small helpers -----------------------------------------------------

   function Addr (Value : Unsigned_64) return Address is
     (To_Address (Integer_Address (Value)));

   function Read8 (A : Unsigned_64) return Unsigned_8 is
     (Mmio_Read8 (A));

   procedure Write8 (A : Unsigned_64; Value : Unsigned_8) is
   begin
      Mmio_Write8 (A, Value);
   end Write8;

   --  Write one little-endian 16-bit field into a DMA page.
   procedure Put16Le (A : Unsigned_64; Value : Unsigned_16) is
   begin
      Write8 (A, Unsigned_8 (Value and 16#FF#));
      Write8 (A + 1, Unsigned_8 (Shift_Right (Value, 8)));
   end Put16Le;

   --  Write one little-endian 32-bit field into a DMA page.
   procedure Put32Le (A : Unsigned_64; Value : Unsigned_32) is
   begin
      Write8 (A, Unsigned_8 (Value and 16#FF#));
      Write8 (A + 1, Unsigned_8 (Shift_Right (Value, 8) and 16#FF#));
      Write8 (A + 2, Unsigned_8 (Shift_Right (Value, 16) and 16#FF#));
      Write8 (A + 3, Unsigned_8 (Shift_Right (Value, 24) and 16#FF#));
   end Put32Le;

   --  Read one little-endian 32-bit field, as used by CBW and CSW.
   function Get32Le (A : Unsigned_64) return Unsigned_32 is
     (Unsigned_32 (Read8 (A))
      or Shift_Left (Unsigned_32 (Read8 (A + 1)), 8)
      or Shift_Left (Unsigned_32 (Read8 (A + 2)), 16)
      or Shift_Left (Unsigned_32 (Read8 (A + 3)), 24));

   --  Read one big-endian 32-bit field, as used by SCSI CDBs.
   function Get32Be (A : Unsigned_64) return Unsigned_32 is
     (Shift_Left (Unsigned_32 (Read8 (A)), 24)
      or Shift_Left (Unsigned_32 (Read8 (A + 1)), 16)
      or Shift_Left (Unsigned_32 (Read8 (A + 2)), 8)
      or Unsigned_32 (Read8 (A + 3)));

   --  Allocate and clear one DMA page below the 4 GiB identity window.
   function Alloc_Page return Unsigned_64 is
      Page : Unsigned_64;
   begin
      Page := Pmm_Alloc_Page;
      if Page /= 0 then
         Buffer_Zero (Addr (Page), Page_Size);
      end if;
      return Page;
   end Alloc_Page;

   --  Bound check for the public C API.
   function Valid (Disk : Interfaces.C.int) return Boolean is
     (Disk >= 0 and then Unsigned_32 (Disk) < G_Count);

   --  Copy a literal fallback into the fixed-size model field.
   procedure Set_Model (Model : out Model_Text; Text : String) is
      Pos : Natural := 0;
   begin
      Model := (others => Character'Val (0));
      for C of Text loop
         exit when Pos >= Max_Model;
         Model (Pos) := C;
         Pos := Pos + 1;
      end loop;
      Model (Pos) := Character'Val (0);
   end Set_Model;

   --  Build a printable model string from the INQUIRY vendor/product
   --  fields.  Bytes outside printable ASCII are dropped.
   procedure Build_Model (Data : Unsigned_64; Model : out Model_Text) is
      Pos : Natural := 0;

      procedure Append_Char (C : Character) is
      begin
         if Pos < Max_Model then
            Model (Pos) := C;
            Pos := Pos + 1;
         end if;
      end Append_Char;

      procedure Append_Range (Start : Unsigned_64; Length : Natural) is
         C : Character;
      begin
         if Length = 0 then
            return;
         end if;
         for I in 0 .. Length - 1 loop
            C := Character'Val
              (Natural (Read8 (Start + Unsigned_64 (I))));
            if C >= ' ' and then C <= '~' then
               Append_Char (C);
            end if;
         end loop;
      end Append_Range;
   begin
      Model := (others => Character'Val (0));
      Append_Char ('U');
      Append_Char ('S');
      Append_Char ('B');
      Append_Char (' ');
      Append_Range (Data + 8, 8);
      Append_Char (' ');
      Append_Range (Data + 16, 16);

      while Pos > 0 and then Model (Pos - 1) = ' ' loop
         Pos := Pos - 1;
      end loop;
      Model (Pos) := Character'Val (0);
      if Pos = 0 then
         Set_Model (Model, "USB mass storage");
      end if;
   end Build_Model;

   --  BOT transport ------------------------------------------------------

   --  Lay out the 31-byte Command Block Wrapper for one BOT command.
   procedure Build_Cbw (D          : Disk_Record;
                        Cdb        : Cdb_Array;
                        Cdb_Length : Unsigned_8;
                        Data_Length : Unsigned_32;
                        Direction_In : Boolean) is
      A : constant Unsigned_64 := G_Cbw;
   begin
      Buffer_Zero (Addr (A), 31);
      Write8 (A, 16#55#);
      Write8 (A + 1, 16#53#);
      Write8 (A + 2, 16#42#);
      Write8 (A + 3, 16#43#);
      Put32Le (A + 4, D.Tag);
      Put32Le (A + 8, Data_Length);
      if Direction_In then
         Write8 (A + 12, 16#80#);
      else
         Write8 (A + 12, 0);
      end if;
      Write8 (A + 13, 0);
      Write8 (A + 14, Cdb_Length);
      if Cdb_Length > 0 then
         for I in 0 .. Natural (Cdb_Length) - 1 loop
            Write8 (A + 15 + Unsigned_64 (I), Cdb (I));
         end loop;
      end if;
   end Build_Cbw;

   --  Run one BOT transaction: CBW, optional data stage, then CSW.
   function Transport (D            : in out Disk_Record;
                       Cdb          : Cdb_Array;
                       Cdb_Length   : Unsigned_8;
                       Data_Phys    : Unsigned_64;
                       Data_Length  : Unsigned_32;
                       Direction_In : Boolean;
                       Allow_Short  : Boolean;
                       Status       : out Unsigned_8;
                       Residue      : out Unsigned_32) return Boolean is
      Tag : constant Unsigned_32 := D.Tag;
   begin
      Status := 0;
      Residue := 0;
      if Cdb_Length > 16 or else Data_Length > 4096 then
         return False;
      end if;
      if Data_Length > 0 and then Data_Phys = 0 then
         return False;
      end if;

      Build_Cbw (D, Cdb, Cdb_Length, Data_Length, Direction_In);
      if D.Tag = 16#FFFF_FFFF# then
         D.Tag := 1;
      else
         D.Tag := D.Tag + 1;
      end if;

      if Xhci.Bulk_Transfer (D.Xhci_Index, False, G_Cbw, 31) /= 0 then
         return False;
      end if;
      if Data_Length > 0 then
         if Xhci.Bulk_Transfer (D.Xhci_Index, Direction_In,
                                Data_Phys, Data_Length) /= 0
         then
            return False;
         end if;
      end if;
      if Xhci.Bulk_Transfer (D.Xhci_Index, True, G_Csw, 13) /= 0 then
         return False;
      end if;

      if Read8 (G_Csw) /= 16#55# or else Read8 (G_Csw + 1) /= 16#53#
        or else Read8 (G_Csw + 2) /= 16#42#
        or else Read8 (G_Csw + 3) /= 16#53#
      then
         return False;
      end if;
      if Get32Le (G_Csw + 4) /= Tag then
         return False;
      end if;

      Status := Read8 (G_Csw + 12);
      Residue := Get32Le (G_Csw + 8);
      if Status = 0 and then not Allow_Short and then Residue /= 0 then
         return False;
      end if;
      return True;
   end Transport;

   procedure Request_Sense (D : in out Disk_Record);

   --  Send CLEAR_FEATURE(ENDPOINT_HALT) for one bulk endpoint.
   procedure Clear_Halt (D : Disk_Record; Endpoint_Addr : Unsigned_8) is
      A : constant Unsigned_64 := G_Setup;
   begin
      Write8 (A, 16#02#);
      Write8 (A + 1, 1);
      Put16Le (A + 2, 0);
      Put16Le (A + 4, Unsigned_16 (Endpoint_Addr));
      Put16Le (A + 6, 0);
      if Xhci.Control_Transfer (D.Xhci_Index, G_Setup, 0, 0, False) = 0 then
         null;
      end if;
   end Clear_Halt;

   --  Recover from a BOT phase error: class reset, clear halts, reset rings.
   procedure Reset_Recovery (D : in out Disk_Record) is
      A : constant Unsigned_64 := G_Setup;
   begin
      --  Bulk-Only Mass Storage Reset: class request, interface recipient.
      Write8 (A, 16#21#);
      Write8 (A + 1, 16#FF#);
      Put16Le (A + 2, 0);
      Put16Le (A + 4, Unsigned_16 (D.Interface_Number));
      Put16Le (A + 6, 0);
      if Xhci.Control_Transfer (D.Xhci_Index, G_Setup, 0, 0, False) = 0 then
         null;
      end if;

      Clear_Halt (D, D.Bulk_In_Addr);
      Clear_Halt (D, D.Bulk_Out_Addr);
      if Xhci.Reset_Endpoint (D.Xhci_Index, D.Bulk_In) = 0 then
         null;
      end if;
      if Xhci.Reset_Endpoint (D.Xhci_Index, D.Bulk_Out) = 0 then
         null;
      end if;
      if Xhci.Reset_Endpoint (D.Xhci_Index, 1) = 0 then
         null;
      end if;
   end Reset_Recovery;

   --  Drain the device's sense data after a failed command.
   procedure Request_Sense (D : in out Disk_Record) is
      Cdb : Cdb_Array := (0 => 16#03#, 4 => 18, others => 0);
      Status : Unsigned_8;
      Residue : Unsigned_32;
   begin
      if Transport (D, Cdb, 6, G_Sense, 18, True, True,
                    Status, Residue)
      then
         null;
      end if;
   end Request_Sense;

   --  Run one SCSI command and translate CSW status into success/failure.
   function Command (D            : in out Disk_Record;
                     Cdb          : Cdb_Array;
                     Cdb_Length   : Unsigned_8;
                     Data_Phys    : Unsigned_64;
                     Data_Length  : Unsigned_32;
                     Direction_In : Boolean;
                     Allow_Short  : Boolean) return Boolean is
      Status  : Unsigned_8;
      Residue : Unsigned_32;
   begin
      if not Transport (D, Cdb, Cdb_Length, Data_Phys, Data_Length,
                        Direction_In, Allow_Short, Status, Residue)
      then
         Reset_Recovery (D);
         return False;
      end if;

      if Status = 0 then
         return True;
      elsif Status = 1 then
         Request_Sense (D);
         return False;
      else
         Reset_Recovery (D);
         return False;
      end if;
   end Command;

   --  SCSI commands ------------------------------------------------------

   --  Retry TEST UNIT READY while the medium spins up.
   function Test_Unit_Ready (D : in out Disk_Record) return Boolean is
      Cdb : Cdb_Array := (0 => 16#00#, others => 0);
   begin
      for Attempt in 1 .. 5 loop
         if Command (D, Cdb, 6, 0, 0, False, True) then
            return True;
         end if;
         Usb_Delay_Ms (50);
      end loop;
      return False;
   end Test_Unit_Ready;

   --  READ CAPACITY(10): last LBA and 512-byte block size.
   function Read_Capacity (D : in out Disk_Record) return Boolean is
      Cdb : Cdb_Array := (0 => 16#25#, others => 0);
      Max_Lba : Unsigned_32;
      Block_Size : Unsigned_32;
   begin
      if not Command (D, Cdb, 10, G_Data, 8, True, False) then
         return False;
      end if;
      Max_Lba := Get32Be (G_Data);
      Block_Size := Get32Be (G_Data + 4);
      if Block_Size /= Sector_Size then
         return False;
      end if;
      --  READ CAPACITY(10) saturates at 0xFFFFFFFF for larger media; those
      --  need READ CAPACITY(16) and READ(16)/WRITE(16), which this driver
      --  does not implement yet.  Refuse rather than wrap the LBA.
      if Max_Lba = 16#FFFF_FFFF# then
         return False;
      end if;
      D.Sectors := Unsigned_64 (Max_Lba) + 1;
      return D.Sectors > 0;
   end Read_Capacity;

   --  INQUIRY: build the block device model string from vendor/product.
   procedure Inquiry (D : in out Disk_Record; Model : out Model_Text) is
      Cdb : Cdb_Array := (0 => 16#12#, 4 => 36, others => 0);
   begin
      Model := (others => Character'Val (0));
      if Command (D, Cdb, 6, G_Data, 36, True, True) then
         Build_Model (G_Data, Model);
      else
         Set_Model (Model, "USB mass storage");
      end if;
   end Inquiry;

   --  Store a 32-bit LBA in big-endian form at CDB bytes 2..5.
   procedure Put_Cdb_Lba (Cdb : in out Cdb_Array; Lba : Unsigned_64) is
   begin
      Cdb (2) := Unsigned_8 (Shift_Right (Lba, 24) and 16#FF#);
      Cdb (3) := Unsigned_8 (Shift_Right (Lba, 16) and 16#FF#);
      Cdb (4) := Unsigned_8 (Shift_Right (Lba, 8) and 16#FF#);
      Cdb (5) := Unsigned_8 (Lba and 16#FF#);
   end Put_Cdb_Lba;

   --  SCSI READ(10) for at most eight 512-byte blocks.
   function Read_10 (D      : in out Disk_Record;
                     Lba    : Unsigned_64;
                     Blocks : Unsigned_16;
                     Buffer : Unsigned_64) return Boolean is
      Cdb : Cdb_Array := (others => 0);
   begin
      Cdb (0) := 16#28#;
      Put_Cdb_Lba (Cdb, Lba);
      Cdb (7) := Unsigned_8 (Shift_Right (Blocks, 8));
      Cdb (8) := Unsigned_8 (Blocks and 16#FF#);
      return Command (D, Cdb, 10, Buffer,
                      Unsigned_32 (Blocks) * Sector_Size, True, False);
   end Read_10;

   --  SCSI WRITE(10) for at most eight 512-byte blocks.
   function Write_10 (D      : in out Disk_Record;
                      Lba    : Unsigned_64;
                      Blocks : Unsigned_16;
                      Buffer : Unsigned_64) return Boolean is
      Cdb : Cdb_Array := (others => 0);
   begin
      Cdb (0) := 16#2A#;
      Put_Cdb_Lba (Cdb, Lba);
      Cdb (7) := Unsigned_8 (Shift_Right (Blocks, 8));
      Cdb (8) := Unsigned_8 (Blocks and 16#FF#);
      return Command (D, Cdb, 10, Buffer,
                      Unsigned_32 (Blocks) * Sector_Size, False, False);
   end Write_10;

   --  SCSI SYNCHRONIZE CACHE(10) for the block flush path.
   function Flush_Device (D : in out Disk_Record) return Boolean is
      Cdb : Cdb_Array := (others => 0);
   begin
      Cdb (0) := 16#35#;
      return Command (D, Cdb, 10, 0, 0, False, True);
   end Flush_Device;

   --  Public block API ---------------------------------------------------

   --  Start xHCI, probe BOT interfaces and publish the block devices.
   function Init return Interfaces.C.int is
      Count : Interfaces.C.int;
      Info  : Xhci.Device_Info;
   begin
      G_Count := 0;

      if Xhci.Init /= 0 then
         Log_Error (1, -1);
         return 0;
      end if;
      Count := Xhci.Device_Count;
      if Count <= 0 then
         return 0;
      end if;

      G_Cbw := Alloc_Page;
      G_Csw := Alloc_Page;
      G_Data := Alloc_Page;
      G_Sense := Alloc_Page;
      G_Setup := Alloc_Page;
      if G_Cbw = 0 or else G_Csw = 0 or else G_Data = 0
        or else G_Sense = 0 or else G_Setup = 0
      then
         Log_Error (1, -2);
         return -1;
      end if;

      for I in 0 .. Natural (Count) - 1 loop
         if G_Count >= Unsigned_32 (Max_Disks) then
            exit;
         end if;

         Xhci.Get_Device (Interfaces.C.int (I), Info);
         if Info.Used and then Info.Class = 8
           and then Info.Subclass = 6
           and then Info.Protocol = 16#50#
           and then Info.Bulk_In_Id /= 0
           and then Info.Bulk_Out_Id /= 0
         then
            declare
               D : Disk_Record;
               Model : Model_Text;
               Slot : constant Natural := Natural (G_Count);
            begin
               D.Xhci_Index := Interfaces.C.int (I);
               D.Slot := Info.Slot_Id;
               D.Port := Info.Port;
               D.Bulk_In := Info.Bulk_In_Id;
               D.Bulk_Out := Info.Bulk_Out_Id;
               D.Bulk_In_Addr := Info.Bulk_In_Addr;
               D.Bulk_Out_Addr := Info.Bulk_Out_Addr;
               D.Interface_Number := Info.Interface_Number;
               D.Tag := 1;

               if Test_Unit_Ready (D) then
                  if Read_Capacity (D) then
                     Inquiry (D, Model);
                     G_Models (Slot) := Model;
                     G_Disks (Slot) := D;
                     G_Count := G_Count + 1;
                     Log_Disk (Unsigned_32 (Slot),
                               G_Models (Slot)'Address, D.Sectors);
                  else
                     Log_Error (3, -1);
                  end if;
               else
                  Log_Error (2, -1);
               end if;
            end;
         end if;
      end loop;

      Log_Summary (G_Count);
      return Interfaces.C.int (G_Count);
   end Init;

   --  Number of USB mass-storage disks that passed TUR and capacity.
   function Disk_Count return Interfaces.C.int is
   begin
      return Interfaces.C.int (G_Count);
   end Disk_Count;

   --  Capacity in 512-byte sectors for the kernel block layer.
   function Disk_Sectors (Disk : Interfaces.C.int) return Unsigned_64 is
   begin
      if not Valid (Disk) then
         return 0;
      end if;
      return G_Disks (Natural (Disk)).Sectors;
   end Disk_Sectors;

   --  Read a caller-provided physical buffer in eight-sector chunks.
   function Disk_Read (Disk   : Interfaces.C.int;
                       Lba    : Unsigned_64;
                       Count  : Unsigned_32;
                       Buffer : System.Address)
     return Interfaces.C.int is
      D : Disk_Record;
      Source : Unsigned_64;
      Current_Lba : Unsigned_64;
      Remaining : Unsigned_32;
      Chunk : Unsigned_32;
   begin
      if not Valid (Disk) or else Buffer = Null_Address then
         return -1;
      end if;
      D := G_Disks (Natural (Disk));
      if Count = 0 then
         return 0;
      end if;
      if Lba + Unsigned_64 (Count) > D.Sectors then
         return -1;
      end if;

      Source := Unsigned_64 (To_Integer (Buffer));
      Current_Lba := Lba;
      Remaining := Count;
      while Remaining > 0 loop
         if Remaining > Chunk_Sectors then
            Chunk := Chunk_Sectors;
         else
            Chunk := Remaining;
         end if;
         if not Read_10 (D, Current_Lba, Unsigned_16 (Chunk), Source) then
            G_Disks (Natural (Disk)) := D;
            return -1;
         end if;
         Source := Source + Unsigned_64 (Chunk) * Sector_Size;
         Current_Lba := Current_Lba + Unsigned_64 (Chunk);
         Remaining := Remaining - Chunk;
      end loop;
      G_Disks (Natural (Disk)) := D;
      return 0;
   end Disk_Read;

   --  Write a caller-provided physical buffer in eight-sector chunks.
   function Disk_Write (Disk   : Interfaces.C.int;
                        Lba    : Unsigned_64;
                        Count  : Unsigned_32;
                        Buffer : System.Address)
     return Interfaces.C.int is
      D : Disk_Record;
      Source : Unsigned_64;
      Current_Lba : Unsigned_64;
      Remaining : Unsigned_32;
      Chunk : Unsigned_32;
   begin
      if not Valid (Disk) or else Buffer = Null_Address then
         return -1;
      end if;
      D := G_Disks (Natural (Disk));
      if Count = 0 then
         return 0;
      end if;
      if Lba + Unsigned_64 (Count) > D.Sectors then
         return -1;
      end if;

      Source := Unsigned_64 (To_Integer (Buffer));
      Current_Lba := Lba;
      Remaining := Count;
      while Remaining > 0 loop
         if Remaining > Chunk_Sectors then
            Chunk := Chunk_Sectors;
         else
            Chunk := Remaining;
         end if;
         if not Write_10 (D, Current_Lba, Unsigned_16 (Chunk), Source) then
            G_Disks (Natural (Disk)) := D;
            return -1;
         end if;
         Source := Source + Unsigned_64 (Chunk) * Sector_Size;
         Current_Lba := Current_Lba + Unsigned_64 (Chunk);
         Remaining := Remaining - Chunk;
      end loop;
      G_Disks (Natural (Disk)) := D;
      return 0;
   end Disk_Write;

   --  Flush the device cache through SYNCHRONIZE CACHE(10).
   function Disk_Flush (Disk : Interfaces.C.int) return Interfaces.C.int is
      D : Disk_Record;
   begin
      if not Valid (Disk) then
         return -1;
      end if;
      D := G_Disks (Natural (Disk));
      if Flush_Device (D) then
         G_Disks (Natural (Disk)) := D;
         return 0;
      end if;
      G_Disks (Natural (Disk)) := D;
      return -1;
   end Disk_Flush;

   --  Root port number, shown in the kernel's disk table.
   function Disk_Port (Disk : Interfaces.C.int) return Interfaces.C.int is
   begin
      if not Valid (Disk) then
         return -1;
      end if;
      return Interfaces.C.int (G_Disks (Natural (Disk)).Port);
   end Disk_Port;

   --  Stable C string for the disk table.
   function Disk_Model (Disk : Interfaces.C.int) return System.Address is
   begin
      if not Valid (Disk) then
         return G_Empty'Address;
      end if;
      return G_Models (Natural (Disk))'Address;
   end Disk_Model;
end Usb_Msc;
