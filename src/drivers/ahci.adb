--  Zhaoxu OS - AHCI (SATA) host controller driver, SPARK/Ada edition.
--
--  The kernel identity maps the first 4 GiB of physical memory, so the HBA
--  MMIO window and every DMA structure are addressed by their physical
--  addresses.  Each command moves at most eight sectors through a bounce
--  page, which keeps the PRDT trivial and tolerates non-contiguous kernel
--  heap buffers.  The public C API is identical to the former C driver.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;
use type Interfaces.C.int;

package body Ahci is
   Max_Disks      : constant := 8;
   Max_Ports      : constant := 32;
   Sector_Size    : constant := 512;
   Bounce_Sectors : constant := 8;

   Hba_Cap     : constant := 16#00#;
   Hba_Ghc     : constant := 16#04#;
   Hba_Pi      : constant := 16#0C#;
   Hba_Version : constant := 16#10#;

   Port_Clb  : constant := 16#00#;
   Port_Clbu : constant := 16#04#;
   Port_Fb   : constant := 16#08#;
   Port_Fbu  : constant := 16#0C#;
   Port_Is   : constant := 16#10#;
   Port_Cmd  : constant := 16#18#;
   Port_Tfd  : constant := 16#20#;
   Port_Sig  : constant := 16#24#;
   Port_Ssts : constant := 16#28#;
   Port_Serr : constant := 16#30#;
   Port_Ci   : constant := 16#38#;

   Cmd_St  : constant := 2#00000001#;
   Cmd_Sud : constant := 2#00000010#;
   Cmd_Fre : constant := 2#00010000#;
   Cmd_Fr  : constant := 2#01000000_00000000#;
   Cmd_Cr  : constant := 2#10000000_00000000#;

   Is_Tfes : constant := 2#01000000_00000000_00000000_00000000#;
   Tfd_Err : constant := 1;

   type Model_Text is array (0 .. 40) of Character;
   type Disk_Record is record
      Used     : Unsigned_32 := 0;
      Port     : Unsigned_32 := 0;
      Sectors  : Unsigned_64 := 0;
      Clb_Phys : Unsigned_64 := 0;
      Fis_Phys : Unsigned_64 := 0;
      Ct_Phys  : Unsigned_64 := 0;
      Bnc_Phys : Unsigned_64 := 0;
   end record;

   type Disk_Array is array (0 .. Max_Disks - 1) of Disk_Record;
   type Word_Array is array (Natural range <>) of Unsigned_16;

   G_Disks : Disk_Array;
   G_Count : Unsigned_32 := 0;
   G_Hba   : Unsigned_64 := 0;

   G_Models : array (0 .. Max_Disks - 1) of aliased Model_Text;
   G_Empty  : aliased Model_Text := (others => Character'Val (0));

   --  C glue -------------------------------------------------------------

   --  MMIO must go through the volatile C helpers: an ordinary Ada pointer
   --  dereference lets the compiler hoist the register read out of the
   --  command-completion poll loop.
   function Mmio_Read8 (A : Unsigned_64) return Unsigned_8
     with Import, Convention => C, External_Name => "mmio_read8";
   function Mmio_Read16 (A : Unsigned_64) return Unsigned_16
     with Import, Convention => C, External_Name => "mmio_read16";
   function Mmio_Read32 (A : Unsigned_64) return Unsigned_32
     with Import, Convention => C, External_Name => "mmio_read32";
   procedure Mmio_Write8 (A : Unsigned_64; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "mmio_write8";
   procedure Mmio_Write16 (A : Unsigned_64; Value : Unsigned_16)
     with Import, Convention => C, External_Name => "mmio_write16";
   procedure Mmio_Write32 (A : Unsigned_64; Value : Unsigned_32)
     with Import, Convention => C, External_Name => "mmio_write32";

   function Pci_Read (Bus, Slot, Func, Off : Unsigned_32)
     return Unsigned_32
     with Import, Convention => C, External_Name => "pci_config_read32";

   procedure Pci_Write (Bus, Slot, Func, Off : Unsigned_32;
                        Value : Unsigned_32)
     with Import, Convention => C, External_Name => "pci_config_write32";

   function Pmm_Alloc_Page return Unsigned_64
     with Import, Convention => C, External_Name => "pmm_alloc_page";

   procedure Log_No_Controller
     with Import, Convention => C, External_Name => "ahci_log_no_controller";
   procedure Log_Abars (Abar : Unsigned_64)
     with Import, Convention => C, External_Name => "ahci_log_abar";
   procedure Log_Controller (Bus, Slot, Func : Unsigned_32;
                             Abar : Unsigned_64; Ports : Unsigned_32;
                             Major, Minor : Unsigned_32)
     with Import, Convention => C, External_Name => "ahci_log_controller";
   procedure Log_Port_Code (Port : Unsigned_32; Code : Unsigned_32)
     with Import, Convention => C, External_Name => "ahci_log_port_code";
   procedure Log_Disk (Port : Unsigned_32; Model : System.Address;
                       Sectors : Unsigned_64)
     with Import, Convention => C, External_Name => "ahci_log_disk";
   procedure Log_Summary (Count : Unsigned_32)
     with Import, Convention => C, External_Name => "ahci_log_summary";
   procedure Log_Cmd_Fail (Port : Unsigned_32; Cmd : Unsigned_32;
                           Lba : Unsigned_64; Count : Unsigned_32;
                           Ci : Unsigned_32; Isr : Unsigned_32;
                           Tfd : Unsigned_32; Serr : Unsigned_32)
     with Import, Convention => C, External_Name => "ahci_log_cmd_fail";

   --  Address and memory helpers ----------------------------------------

   function Addr_Of (Value : Unsigned_64) return Address is
     (To_Address (Integer_Address (Value)));

   function Read8 (A : Unsigned_64) return Unsigned_8 is
     (Mmio_Read8 (A));

   function Read16 (A : Unsigned_64) return Unsigned_16 is
     (Mmio_Read16 (A));

   function Read32 (A : Unsigned_64) return Unsigned_32 is
     (Mmio_Read32 (A));

   procedure Write8 (A : Unsigned_64; Value : Unsigned_8) is
   begin
      Mmio_Write8 (A, Value);
   end Write8;

   procedure Write16 (A : Unsigned_64; Value : Unsigned_16) is
   begin
      Mmio_Write16 (A, Value);
   end Write16;

   procedure Write32 (A : Unsigned_64; Value : Unsigned_32) is
   begin
      Mmio_Write32 (A, Value);
   end Write32;

   procedure Zero (A : Unsigned_64; Bytes : Unsigned_32) is
   begin
      for I in Unsigned_32 range 0 .. Bytes - 1 loop
         Write8 (A + Unsigned_64 (I), 0);
      end loop;
   end Zero;

   procedure Copy (Dst, Src : Unsigned_64; Bytes : Unsigned_32) is
   begin
      for I in Unsigned_32 range 0 .. Bytes - 1 loop
         Write8 (Dst + Unsigned_64 (I), Read8 (Src + Unsigned_64 (I)));
      end loop;
   end Copy;

   --  HBA / port register access ----------------------------------------

   function Hba_Read (Offset : Unsigned_32) return Unsigned_32 is
   begin
      return Read32 (G_Hba + Unsigned_64 (Offset));
   end Hba_Read;

   procedure Hba_Write (Offset : Unsigned_32; Value : Unsigned_32) is
   begin
      Write32 (G_Hba + Unsigned_64 (Offset), Value);
   end Hba_Write;

   function Port_Addr (Port : Unsigned_32; Offset : Unsigned_32)
     return Unsigned_64 is
   begin
      return G_Hba + 16#100# + Unsigned_64 (Port) * 16#80#
             + Unsigned_64 (Offset);
   end Port_Addr;

   function Port_Read (Port : Unsigned_32; Offset : Unsigned_32)
     return Unsigned_32 is
   begin
      return Read32 (Port_Addr (Port, Offset));
   end Port_Read;

   procedure Port_Write (Port : Unsigned_32; Offset : Unsigned_32;
                         Value : Unsigned_32) is
   begin
      Write32 (Port_Addr (Port, Offset), Value);
   end Port_Write;

   --  Controller discovery ------------------------------------------------

   G_Ctrl_Bus  : Unsigned_32 := 0;
   G_Ctrl_Slot : Unsigned_32 := 0;
   G_Ctrl_Func : Unsigned_32 := 0;

   function Find_Controller return Boolean is
      Id  : Unsigned_32;
      Cls : Unsigned_32;
   begin
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
                     G_Ctrl_Bus := Bus;
                     G_Ctrl_Slot := Slot;
                     G_Ctrl_Func := Func;
                     return True;
                  end if;
               end if;
            end loop;
         end loop;
      end loop;
      return False;
   end Find_Controller;

   function Read_Abar return Unsigned_64 is
      Bar  : constant Unsigned_32 :=
        Pci_Read (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#24#);
      High : Unsigned_32;
   begin
      if (Bar and 1) /= 0 then
         return 0;
      end if;
      if (Bar and 16#06#) = 16#04# then
         High := Pci_Read (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#28#);
         return Shift_Left (Unsigned_64 (High), 32)
                or Unsigned_64 (Bar and 16#FFFF_FFF0#);
      end if;
      return Unsigned_64 (Bar and 16#FFFF_FFF0#);
   end Read_Abar;

   --  Port engine ---------------------------------------------------------

   function Port_Stop (Port : Unsigned_32) return Integer is
      Command : Unsigned_32;
   begin
      Command := Port_Read (Port, Port_Cmd);
      Command := Command and not Cmd_St;
      Port_Write (Port, Port_Cmd, Command);
      for I in Integer range 1 .. 1_000_000 loop
         exit when (Port_Read (Port, Port_Cmd) and Cmd_Cr) = 0;
      end loop;
      if (Port_Read (Port, Port_Cmd) and Cmd_Cr) /= 0 then
         return -1;
      end if;

      Command := Port_Read (Port, Port_Cmd);
      Command := Command and not Cmd_Fre;
      Port_Write (Port, Port_Cmd, Command);
      for I in Integer range 1 .. 1_000_000 loop
         exit when (Port_Read (Port, Port_Cmd) and Cmd_Fr) = 0;
      end loop;
      if (Port_Read (Port, Port_Cmd) and Cmd_Fr) /= 0 then
         return -1;
      end if;
      return 0;
   end Port_Stop;

   function Port_Start (D : Disk_Record) return Integer is
      Port    : constant Unsigned_32 := D.Port;
      Command : Unsigned_32;
   begin
      Port_Write (Port, Port_Clb, Unsigned_32 (D.Clb_Phys and 16#FFFF_FFFF#));
      Port_Write (Port, Port_Clbu, Unsigned_32 (Shift_Right (D.Clb_Phys, 32)));
      Port_Write (Port, Port_Fb, Unsigned_32 (D.Fis_Phys and 16#FFFF_FFFF#));
      Port_Write (Port, Port_Fbu, Unsigned_32 (Shift_Right (D.Fis_Phys, 32)));
      Port_Write (Port, Port_Is, 16#FFFF_FFFF#);
      Port_Write (Port, Port_Serr, 16#FFFF_FFFF#);

      Command := Port_Read (Port, Port_Cmd);
      Command := Command or Cmd_Fre;
      Port_Write (Port, Port_Cmd, Command);
      for I in Integer range 1 .. 1_000_000 loop
         exit when (Port_Read (Port, Port_Cmd) and Cmd_Fr) /= 0;
      end loop;
      if (Port_Read (Port, Port_Cmd) and Cmd_Fr) = 0 then
         return -1;
      end if;

      Command := Port_Read (Port, Port_Cmd);
      Command := Command or Cmd_St or Cmd_Sud;
      Port_Write (Port, Port_Cmd, Command);
      for I in Integer range 1 .. 1_000_000 loop
         exit when (Port_Read (Port, Port_Cmd) and Cmd_Cr) /= 0;
      end loop;
      if (Port_Read (Port, Port_Cmd) and Cmd_Cr) = 0 then
         return -1;
      end if;
      return 0;
   end Port_Start;

   function Command (D         : Disk_Record;
                     Cmd_Code  : Unsigned_8;
                     Lba       : Unsigned_64;
                     Count     : Unsigned_16;
                     Buf_Phys  : Unsigned_64;
                     Bytes     : Unsigned_32;
                     Is_Write  : Boolean) return Integer is
      Port     : constant Unsigned_32 := D.Port;
      Prdtl    : Unsigned_32 := 0;
      Header0  : Unsigned_32;
      Timeout  : Integer := 2_000_000;
      Status   : Unsigned_32;
   begin
      Zero (D.Clb_Phys, 1024);
      Zero (D.Ct_Phys, 4096);

      Write8 (D.Ct_Phys + 0, 16#27#);
      Write8 (D.Ct_Phys + 1, 16#80#);
      Write8 (D.Ct_Phys + 2, Cmd_Code);
      Write8 (D.Ct_Phys + 3, 0);
      Write8 (D.Ct_Phys + 4, Unsigned_8 (Lba and 16#FF#));
      Write8 (D.Ct_Phys + 5, Unsigned_8 (Shift_Right (Lba, 8) and 16#FF#));
      Write8 (D.Ct_Phys + 6, Unsigned_8 (Shift_Right (Lba, 16) and 16#FF#));
      Write8 (D.Ct_Phys + 7, 16#40#);
      Write8 (D.Ct_Phys + 8, Unsigned_8 (Shift_Right (Lba, 24) and 16#FF#));
      Write8 (D.Ct_Phys + 9, Unsigned_8 (Shift_Right (Lba, 32) and 16#FF#));
      Write8 (D.Ct_Phys + 10, Unsigned_8 (Shift_Right (Lba, 40) and 16#FF#));
      Write8 (D.Ct_Phys + 11, 0);
      Write8 (D.Ct_Phys + 12, Unsigned_8 (Count and 16#FF#));
      Write8 (D.Ct_Phys + 13, Unsigned_8 (Shift_Right (Count, 8) and 16#FF#));
      Write8 (D.Ct_Phys + 14, 0);
      Write8 (D.Ct_Phys + 15, 0);

      if Bytes > 0 then
         Write32 (D.Ct_Phys + 16#80#,
                  Unsigned_32 (Buf_Phys and 16#FFFF_FFFF#));
         Write32 (D.Ct_Phys + 16#84#, Unsigned_32 (Shift_Right (Buf_Phys, 32)));
         Write32 (D.Ct_Phys + 16#88#, 0);
         Write32 (D.Ct_Phys + 16#8C#, Bytes - 1);
         Prdtl := 1;
      end if;

      Header0 := 5 or Shift_Left (Prdtl, 16) or 16#80#;
      if Is_Write then
         Header0 := Header0 or 16#40#;
      end if;
      Write32 (D.Clb_Phys + 0, Header0);
      Write32 (D.Clb_Phys + 4, 0);
      Write32 (D.Clb_Phys + 8, Unsigned_32 (D.Ct_Phys and 16#FFFF_FFFF#));
      Write32 (D.Clb_Phys + 12, Unsigned_32 (Shift_Right (D.Ct_Phys, 32)));

      Port_Write (Port, Port_Is, 16#FFFF_FFFF#);
      Port_Write (Port, Port_Serr, 16#FFFF_FFFF#);
      Port_Write (Port, Port_Ci, 1);

      while (Port_Read (Port, Port_Ci) and 1) /= 0 and then Timeout > 0 loop
         Timeout := Timeout - 1;
      end loop;
      if Timeout <= 0 then
         Log_Cmd_Fail (Port, Unsigned_32 (Cmd_Code), Lba,
                       Unsigned_32 (Count), Port_Read (Port, Port_Ci),
                       Port_Read (Port, Port_Is), Port_Read (Port, Port_Tfd),
                       Port_Read (Port, Port_Serr));
         return -1;
      end if;

      Status := Port_Read (Port, Port_Is);
      if (Status and Is_Tfes) /= 0 then
         Log_Cmd_Fail (Port, Unsigned_32 (Cmd_Code), Lba,
                       Unsigned_32 (Count), Port_Read (Port, Port_Ci),
                       Status, Port_Read (Port, Port_Tfd),
                       Port_Read (Port, Port_Serr));
         return -1;
      end if;
      if (Port_Read (Port, Port_Tfd) and Tfd_Err) /= 0 then
         Log_Cmd_Fail (Port, Unsigned_32 (Cmd_Code), Lba,
                       Unsigned_32 (Count), Port_Read (Port, Port_Ci),
                       Status, Port_Read (Port, Port_Tfd),
                       Port_Read (Port, Port_Serr));
         return -1;
      end if;
      return 0;
   end Command;

   procedure Identify (D : Disk_Record; Words : out Word_Array;
                       Ok : out Boolean) is
   begin
      Zero (D.Bnc_Phys, Sector_Size);
      if Command (D, 16#EC#, 0, 0, D.Bnc_Phys, Sector_Size, False) /= 0 then
         Ok := False;
         return;
      end if;
      for I in Words'Range loop
         Words (I) := Read16 (D.Bnc_Phys + Unsigned_64 (I) * 2);
      end loop;
      Ok := True;
   end Identify;

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

   procedure Probe (Port : Unsigned_32) is
      Ssts    : Unsigned_32;
      D       : Disk_Record;
      Words   : Word_Array (0 .. 255);
      Ok      : Boolean;
      Sectors : Unsigned_64;
      Index   : constant Unsigned_32 := G_Count;
   begin
      if G_Count >= Max_Disks then
         return;
      end if;
      Ssts := Port_Read (Port, Port_Ssts);
      if (Ssts and 16#0F#) /= 3 then
         return;
      end if;
      --  ATAPI devices (ATAPI signature 0xEB140101) do not answer the ATA
      --  IDENTIFY command; skip them instead of burning the timeout.
      if Port_Read (Port, Port_Sig) /= 16#0000_0101# then
         Log_Port_Code (Port, 6);
         return;
      end if;

      D.Port := Port;
      D.Clb_Phys := Pmm_Alloc_Page;
      D.Fis_Phys := Pmm_Alloc_Page;
      D.Ct_Phys := Pmm_Alloc_Page;
      D.Bnc_Phys := Pmm_Alloc_Page;
      if D.Clb_Phys = 0 or else D.Fis_Phys = 0
        or else D.Ct_Phys = 0 or else D.Bnc_Phys = 0
      then
         Log_Port_Code (Port, 1);
         return;
      end if;

      if Port_Stop (Port) /= 0 then
         Log_Port_Code (Port, 2);
         return;
      end if;
      if Port_Start (D) /= 0 then
         Log_Port_Code (Port, 3);
         return;
      end if;

      Identify (D, Words, Ok);
      if not Ok then
         Log_Port_Code (Port, 4);
         return;
      end if;

      Sectors :=
        Unsigned_64 (Words (100))
        or Shift_Left (Unsigned_64 (Words (101)), 16)
        or Shift_Left (Unsigned_64 (Words (102)), 32)
        or Shift_Left (Unsigned_64 (Words (103)), 48);
      if Sectors = 0 then
         Sectors := Unsigned_64 (Words (60))
                    or Shift_Left (Unsigned_64 (Words (61)), 16);
      end if;
      if Sectors = 0 then
         Log_Port_Code (Port, 5);
         return;
      end if;

      D.Sectors := Sectors;
      Build_Model (Words, G_Models (Natural (Index)));
      D.Used := 1;
      G_Disks (Natural (Index)) := D;
      Log_Disk (Port, G_Models (Natural (Index))'Address, Sectors);
      G_Count := G_Count + 1;
   end Probe;

   --  Public API ---------------------------------------------------------

   function Init return Interfaces.C.int is
      Abar      : Unsigned_64;
      Command   : Unsigned_32;
      Cap       : Unsigned_32;
      Ghc       : Unsigned_32;
      Implemented : Unsigned_32;
      Version   : Unsigned_32;
   begin
      G_Count := 0;
      G_Hba := 0;

      if not Find_Controller then
         Log_No_Controller;
         return -1;
      end if;

      Abar := Read_Abar;
      if Abar = 0 or else Abar >= 16#1_0000_0000# then
         Log_Abars (Abar);
         return -1;
      end if;

      Command := Pci_Read (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#04#);
      Pci_Write (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#04#,
                 (Command and 16#FFFF#) or 16#0006#);

      G_Hba := Abar;

      Cap := Hba_Read (Hba_Cap);
      Ghc := Hba_Read (Hba_Ghc);
      Ghc := Ghc or 16#8000_0000#;
      Hba_Write (Hba_Ghc, Ghc);

      Version := Hba_Read (Hba_Version);
      Log_Controller (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, Abar,
                      (Cap and 16#1F#) + 1,
                      Shift_Right (Version, 16),
                      Version and 16#FFFF#);

      Implemented := Hba_Read (Hba_Pi);
      for Port in Unsigned_32 range 0 .. Max_Ports - 1 loop
         if (Implemented and Shift_Left (Unsigned_32 (1), Natural (Port)))
           /= 0
         then
            Probe (Port);
         end if;
      end loop;

      Log_Summary (G_Count);
      return (if G_Count > 0 then 0 else -1);
   end Init;

   function Disk_Count return Interfaces.C.int is
   begin
      return Interfaces.C.int (G_Count);
   end Disk_Count;

   function Valid (Disk : Interfaces.C.int) return Boolean is
   begin
      return Disk >= 0 and then Unsigned_32 (Disk) < G_Count;
   end Valid;

   function Disk_Sectors (Disk : Interfaces.C.int) return Unsigned_64 is
   begin
      if not Valid (Disk) then
         return 0;
      end if;
      return G_Disks (Natural (Disk)).Sectors;
   end Disk_Sectors;

   function Disk_Read (Disk   : Interfaces.C.int;
                       Lba    : Unsigned_64;
                       Count  : Unsigned_32;
                       Buffer : System.Address)
     return Interfaces.C.int is
      D        : Disk_Record;
      Source   : Unsigned_64 := Unsigned_64 (To_Integer (Buffer));
      Cur_Lba  : Unsigned_64 := Lba;
      Remaining : Unsigned_32 := Count;
      Chunk    : Unsigned_32;
      Bytes    : Unsigned_32;
   begin
      if not Valid (Disk) or else Buffer = System.Null_Address then
         return -1;
      end if;
      D := G_Disks (Natural (Disk));
      while Remaining > 0 loop
         if Remaining > Bounce_Sectors then
            Chunk := Bounce_Sectors;
         else
            Chunk := Remaining;
         end if;
         Bytes := Chunk * Sector_Size;
         if Command (D, 16#25#, Cur_Lba, Unsigned_16 (Chunk), D.Bnc_Phys,
                     Bytes, False) /= 0
         then
            return -1;
         end if;
         Copy (Source, D.Bnc_Phys, Bytes);
         Source := Source + Unsigned_64 (Bytes);
         Cur_Lba := Cur_Lba + Unsigned_64 (Chunk);
         Remaining := Remaining - Chunk;
      end loop;
      return 0;
   end Disk_Read;

   function Disk_Write (Disk   : Interfaces.C.int;
                        Lba    : Unsigned_64;
                        Count  : Unsigned_32;
                        Buffer : System.Address)
     return Interfaces.C.int is
      D        : Disk_Record;
      Source   : Unsigned_64 := Unsigned_64 (To_Integer (Buffer));
      Cur_Lba  : Unsigned_64 := Lba;
      Remaining : Unsigned_32 := Count;
      Chunk    : Unsigned_32;
      Bytes    : Unsigned_32;
   begin
      if not Valid (Disk) or else Buffer = System.Null_Address then
         return -1;
      end if;
      D := G_Disks (Natural (Disk));
      while Remaining > 0 loop
         if Remaining > Bounce_Sectors then
            Chunk := Bounce_Sectors;
         else
            Chunk := Remaining;
         end if;
         Bytes := Chunk * Sector_Size;
         if Cur_Lba + Unsigned_64 (Chunk) > D.Sectors then
            return -1;
         end if;
         Copy (D.Bnc_Phys, Source, Bytes);
         if Command (D, 16#35#, Cur_Lba, Unsigned_16 (Chunk), D.Bnc_Phys,
                     Bytes, True) /= 0
         then
            return -1;
         end if;
         Source := Source + Unsigned_64 (Bytes);
         Cur_Lba := Cur_Lba + Unsigned_64 (Chunk);
         Remaining := Remaining - Chunk;
      end loop;
      return 0;
   end Disk_Write;

   function Disk_Flush (Disk : Interfaces.C.int) return Interfaces.C.int is
   begin
      if not Valid (Disk) then
         return -1;
      end if;
      return Interfaces.C.int
        (Command (G_Disks (Natural (Disk)), 16#EA#, 0, 0,
                  G_Disks (Natural (Disk)).Bnc_Phys, 0, True));
   end Disk_Flush;

   function Disk_Port (Disk : Interfaces.C.int) return Interfaces.C.int is
   begin
      if not Valid (Disk) then
         return -1;
      end if;
      return Interfaces.C.int (G_Disks (Natural (Disk)).Port);
   end Disk_Port;

   function Disk_Model (Disk : Interfaces.C.int) return System.Address is
   begin
      if not Valid (Disk) then
         return G_Empty'Address;
      end if;
      return G_Models (Natural (Disk))'Address;
   end Disk_Model;
end Ahci;
