--  Zhaoxu OS - xHCI host controller driver, SPARK/Ada edition.
--
--  This is deliberately a polled driver.  The Ring3 service owns no PCI
--  interrupt vector, so command completions and transfer events are taken
--  from the event ring inside the service loop.  All DMA structures live in
--  pages obtained through dev_alloc_page; the trusted driver process has an
--  identity map of the first 4 GiB, so those physical addresses are directly
--  dereferenceable.

with Interfaces; use Interfaces;
with Interfaces.C;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;
use type Interfaces.C.int;

package body Xhci is
   Command_Entries : constant := 256;
   Event_Entries   : constant := 256;
   Ep_Entries      : constant := 64;
   Page_Size       : constant := 4096;
   Max_Transfer    : constant := 4096;

   Trb_Normal             : constant := 1;
   Trb_Setup              : constant := 2;
   Trb_Data               : constant := 3;
   Trb_Status             : constant := 4;
   Trb_Link               : constant := 6;
   Trb_Enable_Slot        : constant := 9;
   Trb_Disable_Slot       : constant := 10;
   Trb_Address_Device     : constant := 11;
   Trb_Configure_Endpoint : constant := 12;
   Trb_Evaluate_Context   : constant := 13;
   Trb_Reset_Endpoint     : constant := 14;
   Trb_Set_Tr_Dequeue     : constant := 16;
   Trb_Transfer_Event     : constant := 32;
   Trb_Command_Completion : constant := 33;

   Cc_Success : constant := 1;
   Cc_Stall   : constant := 6;
   Cc_Short   : constant := 13;

   Ctrl_Cycle : constant := 1;
   Ctrl_Tc    : constant := 2;
   Ctrl_Chain : constant := 16#10#;
   Ctrl_Ioc   : constant := 16#20#;
   Ctrl_Idt   : constant := 16#40#;
   Ctrl_Dir   : constant := 16#1_0000#;
   Trt_In     : constant := 2 * 16#1_0000#;
   Trt_Out    : constant := 3 * 16#1_0000#;

   Hcc_Csz : constant := 16#04#;
   Usb_Cmd_Run   : constant := 1;
   Usb_Cmd_Reset : constant := 2;
   Usb_Sts_Hch   : constant := 1;
   Usb_Sts_Cnr   : constant := 16#800#;
   Port_Ccs      : constant := 1;
   Port_Pr       : constant := 16#10#;
   Port_Pls_Mask : constant := 16#1E0#;
   Port_Speed_Mask : constant := 16#3C00#;
   Port_Prc      : constant := 16#0020_0000#;
   Port_Change   : constant := 16#00FE_0000#;

   type Ring_Record is record
      Phys    : Unsigned_64 := 0;
      Entries : Unsigned_32 := 0;
      Enqueue : Unsigned_32 := 0;
      Cycle   : Unsigned_32 := 1;
   end record;

   type Device_Record is record
      Used             : Boolean := False;
      Slot_Id          : Unsigned_8 := 0;
      Port             : Unsigned_8 := 0;
      Speed            : Unsigned_8 := 0;
      Ep0_Max          : Unsigned_16 := 0;
      Vendor           : Unsigned_16 := 0;
      Product          : Unsigned_16 := 0;
      Class            : Unsigned_8 := 0;
      Subclass         : Unsigned_8 := 0;
      Protocol         : Unsigned_8 := 0;
      Interface_Number : Unsigned_8 := 0;
      Config_Value     : Unsigned_8 := 0;
      Bulk_In_Addr     : Unsigned_8 := 0;
      Bulk_Out_Addr    : Unsigned_8 := 0;
      Bulk_In_Id       : Unsigned_8 := 0;
      Bulk_Out_Id      : Unsigned_8 := 0;
      Bulk_In_Max      : Unsigned_16 := 0;
      Bulk_Out_Max     : Unsigned_16 := 0;
      Ep0_Ring         : Ring_Record;
      Bulk_In_Ring     : Ring_Record;
      Bulk_Out_Ring    : Ring_Record;
      Device_Ctx       : Unsigned_64 := 0;
      Input_Ctx        : Unsigned_64 := 0;
   end record;

   type Device_Array is array (0 .. Max_Devices - 1) of Device_Record;

   G_Devices : Device_Array;
   G_Count   : Unsigned_32 := 0;
   G_Empty_Info : Device_Info;

   G_Bar       : Unsigned_64 := 0;
   G_Op        : Unsigned_64 := 0;
   G_Db        : Unsigned_64 := 0;
   G_Rt        : Unsigned_64 := 0;
   G_Ctx       : Unsigned_32 := 64;
   G_Max_Slots : Unsigned_32 := 0;
   G_Max_Ports : Unsigned_32 := 0;

   G_Dcbaa      : Unsigned_64 := 0;
   G_Erst       : Unsigned_64 := 0;
   G_Event_Phys : Unsigned_64 := 0;
   G_Cmd_Phys   : Unsigned_64 := 0;
   G_Data_Phys  : Unsigned_64 := 0;
   G_Setup_Phys : Unsigned_64 := 0;

   G_Cmd_Ring      : Ring_Record;
   G_Event_Dequeue : Unsigned_32 := 0;
   G_Event_Cycle   : Unsigned_32 := 1;

   G_Ctrl_Bus  : Unsigned_32 := 0;
   G_Ctrl_Slot : Unsigned_32 := 0;
   G_Ctrl_Func : Unsigned_32 := 0;

   --  C glue ------------------------------------------------------------

   function Mmio_Read8 (A : Unsigned_64) return Unsigned_8
     with Import, Convention => C, External_Name => "mmio_read8";
   function Mmio_Read16 (A : Unsigned_64) return Unsigned_16
     with Import, Convention => C, External_Name => "mmio_read16";
   function Mmio_Read32 (A : Unsigned_64) return Unsigned_32
     with Import, Convention => C, External_Name => "mmio_read32";
   procedure Mmio_Write8 (A : Unsigned_64; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "mmio_write8";
   procedure Mmio_Write32 (A : Unsigned_64; Value : Unsigned_32)
     with Import, Convention => C, External_Name => "mmio_write32";
   procedure Mmio_Write64 (A : Unsigned_64; Value : Unsigned_64)
     with Import, Convention => C, External_Name => "mmio_write64";

   function Pci_Read (Bus, Slot, Func, Off : Unsigned_32)
     return Unsigned_32
     with Import, Convention => C, External_Name => "pci_config_read32";

   procedure Pci_Write (Bus, Slot, Func, Off : Unsigned_32;
                        Value : Unsigned_32)
     with Import, Convention => C, External_Name => "pci_config_write32";

   function Pmm_Alloc_Page return Unsigned_64
     with Import, Convention => C, External_Name => "pmm_alloc_page";

   procedure Buffer_Zero (Dst : Address; Bytes : Unsigned_32)
     with Import, Convention => C, External_Name => "buffer_zero";

   procedure Log_No_Controller
     with Import, Convention => C, External_Name => "xhci_log_no_controller";
   procedure Log_Controller (Bus, Slot, Func : Unsigned_32;
                             Bar : Unsigned_64; Ports : Unsigned_32;
                             Version : Unsigned_32)
     with Import, Convention => C, External_Name => "xhci_log_controller";
   procedure Log_Error (Stage, Code : Unsigned_32)
     with Import, Convention => C, External_Name => "xhci_log_error";
   procedure Log_Device (Port, Speed, Vendor, Product : Unsigned_32;
                         Class, Subclass, Protocol : Unsigned_32)
     with Import, Convention => C, External_Name => "xhci_log_device";
   procedure Log_Summary (Count : Unsigned_32)
     with Import, Convention => C, External_Name => "xhci_log_summary";

   --  Address helpers ---------------------------------------------------

   --  Convert a physical address into an Ada address for buffer_zero.
   function Addr (Value : Unsigned_64) return Address is
     (To_Address (Integer_Address (Value)));

   --  Volatile 32-bit MMIO accessors used by the register and ring code.
   function Read32 (A : Unsigned_64) return Unsigned_32 is
     (Mmio_Read32 (A));

   procedure Write32 (A : Unsigned_64; Value : Unsigned_32) is
   begin
      Mmio_Write32 (A, Value);
   end Write32;

   procedure Write64 (A : Unsigned_64; Value : Unsigned_64) is
   begin
      Mmio_Write64 (A, Value);
   end Write64;

   --  Small helper for the MaxSlotsEn and loop bounds.
   function Min32 (A, B : Unsigned_32) return Unsigned_32 is
     (if A < B then A else B);

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

   --  TRB rings ----------------------------------------------------------

   --  Write one 16-byte TRB into a ring page.
   procedure Write_Trb (Ring_Phys : Unsigned_64; Index : Unsigned_32;
                        P0, P1, P2, P3 : Unsigned_32) is
      Base : constant Unsigned_64 :=
        Ring_Phys + Unsigned_64 (Index) * 16;
   begin
      Write32 (Base, P0);
      Write32 (Base + 4, P1);
      Write32 (Base + 8, P2);
      Write32 (Base + 12, P3);
   end Write_Trb;

   --  Clear a ring and install its wrap-around Link TRB.
   procedure Ring_Init (R : out Ring_Record; Phys_Value : Unsigned_64;
                        Entries : Unsigned_32) is
   begin
      R.Phys := Phys_Value;
      R.Entries := Entries;
      R.Enqueue := 0;
      R.Cycle := 1;
      Buffer_Zero (Addr (Phys_Value), Page_Size);
      Write_Trb (Phys_Value, Entries - 1,
                 Unsigned_32 (Phys_Value and 16#FFFF_FFFF#),
                 Unsigned_32 (Shift_Right (Phys_Value, 32)),
                 0,
                 (Trb_Link * 16#400#) or Ctrl_Tc or Ctrl_Cycle);
   end Ring_Init;

   --  Put an endpoint ring back at its first TRB after a halt.
   procedure Ring_Reset (R : in out Ring_Record) is
      Phys_Value : constant Unsigned_64 := R.Phys;
      Entries : constant Unsigned_32 := R.Entries;
   begin
      Ring_Init (R, Phys_Value, Entries);
   end Ring_Reset;

   --  Append one TRB, rewriting the Link TRB and flipping the cycle bit on
   --  wrap.  Trb_Phys returns the address needed to match command events.
   function Ring_Push (R : in out Ring_Record;
                       P0, P1, P2, P3 : Unsigned_32;
                       Trb_Phys : out Unsigned_64) return Integer is
   begin
      if R.Phys = 0 or else R.Entries < 2 then
         Trb_Phys := 0;
         return -1;
      end if;

      Trb_Phys := R.Phys + Unsigned_64 (R.Enqueue) * 16;
      Write_Trb (R.Phys, R.Enqueue, P0, P1, P2, P3 or R.Cycle);

      if R.Enqueue = R.Entries - 2 then
         Write_Trb (R.Phys, R.Entries - 1,
                    Unsigned_32 (R.Phys and 16#FFFF_FFFF#),
                    Unsigned_32 (Shift_Right (R.Phys, 32)),
                    0,
                    (Trb_Link * 16#400#) or Ctrl_Tc or R.Cycle);
         R.Cycle := R.Cycle xor 1;
         R.Enqueue := 0;
      else
         R.Enqueue := R.Enqueue + 1;
      end if;
      return 0;
   end Ring_Push;

   --  Event ring ---------------------------------------------------------

   --  Pop one event from the event ring, advancing ERDP and the cycle bit.
   function Next_Event (P0, P1, P2, P3 : out Unsigned_32) return Boolean is
      Base : Unsigned_64;
      Ctl  : Unsigned_32;
   begin
      P0 := 0;
      P1 := 0;
      P2 := 0;
      P3 := 0;
      if G_Event_Phys = 0 then
         return False;
      end if;

      Base := G_Event_Phys + Unsigned_64 (G_Event_Dequeue) * 16;
      Ctl := Read32 (Base + 12);
      if (Ctl and 1) /= G_Event_Cycle then
         return False;
      end if;

      P0 := Read32 (Base);
      P1 := Read32 (Base + 4);
      P2 := Read32 (Base + 8);
      P3 := Ctl;

      G_Event_Dequeue := G_Event_Dequeue + 1;
      if G_Event_Dequeue >= Event_Entries then
         G_Event_Dequeue := 0;
         G_Event_Cycle := G_Event_Cycle xor 1;
      end if;
      Write64 (G_Rt + 16#20# + 16#18#,
               G_Event_Phys + Unsigned_64 (G_Event_Dequeue) * 16 + 8);
      return True;
   end Next_Event;

   --  Ring one host-controller doorbell (0 is the command ring).
   procedure Doorbell (Slot, Target : Unsigned_32) is
   begin
      Write32 (G_Db + Unsigned_64 (Slot) * 4, Target);
   end Doorbell;

   --  Command / transfer completion --------------------------------------

   --  Wait for the Command Completion Event for one command TRB.
   function Wait_Command (Command_Phys : Unsigned_64;
                          Slot : out Unsigned_8) return Integer is
      P0, P1, P2, P3 : Unsigned_32;
      Code : Unsigned_32;
   begin
      Slot := 0;
      for I in 1 .. 2_000_000 loop
         if Next_Event (P0, P1, P2, P3) then
            if (Shift_Right (P3, 10) and 16#3F#) =
              Trb_Command_Completion
            then
               if (Unsigned_64 (P0) or
                     Shift_Left (Unsigned_64 (P1), 32)) = Command_Phys
               then
                  Code := Shift_Right (P2, 24) and 16#FF#;
                  Slot := Unsigned_8 (Shift_Right (P3, 24) and 16#FF#);
                  if Code = Cc_Success then
                     return 0;
                  else
                     Log_Error (8, Code);
                     return -1;
                  end if;
               end if;
            end if;
         end if;
      end loop;
      return -1;
   end Wait_Command;

   --  Wait for the Transfer Event of one endpoint and accept short packets.
   --  Matching the TRB pointer keeps a late event from a timed-out transfer
   --  from being mistaken for the current one.
   function Wait_Transfer (Slot, Endpoint : Unsigned_8;
                           Trb_Phys : Unsigned_64) return Integer is
      P0, P1, P2, P3 : Unsigned_32;
      Code : Unsigned_32;
   begin
      for I in 1 .. 2_000_000 loop
         if Next_Event (P0, P1, P2, P3) then
            if (Shift_Right (P3, 10) and 16#3F#) = Trb_Transfer_Event then
               if (Unsigned_64 (P0) or
                     Shift_Left (Unsigned_64 (P1), 32)) = Trb_Phys
                 and then (Shift_Right (P2, 16) and 16#FF#) =
                   Unsigned_32 (Endpoint)
                 and then (Shift_Right (P3, 24) and 16#FF#) =
                   Unsigned_32 (Slot)
               then
                  Code := Shift_Right (P2, 24) and 16#FF#;
                  if Code = Cc_Success or else Code = Cc_Short then
                     return 0;
                  end if;
                  if Code /= Cc_Stall then
                     Log_Error (9, Code);
                  end if;
                  return -1;
               end if;
            end if;
         end if;
      end loop;
      return -1;
   end Wait_Transfer;

   --  Queue one command TRB, ring the command doorbell and wait for it.
   function Run_Command (Param : Unsigned_64; Slot : Unsigned_8;
                         Trb_Type : Unsigned_32;
                         Slot_Out : out Unsigned_8;
                         Endpoint : Unsigned_8 := 0) return Integer is
      P3 : Unsigned_32;
      Trb_Phys : Unsigned_64;
   begin
      P3 := (Trb_Type * 16#400#) or (Unsigned_32 (Slot) * 16#100_0000#);
      if Trb_Type = Trb_Reset_Endpoint or else
        Trb_Type = Trb_Set_Tr_Dequeue
      then
         P3 := P3 or (Unsigned_32 (Endpoint) * 16#1_0000#);
      end if;

      if Ring_Push (G_Cmd_Ring,
                    Unsigned_32 (Param and 16#FFFF_FFFF#),
                    Unsigned_32 (Shift_Right (Param, 32)),
                    0, P3, Trb_Phys) /= 0
      then
         Slot_Out := 0;
         return -1;
      end if;
      Doorbell (0, 0);
      return Wait_Command (Trb_Phys, Slot_Out);
   end Run_Command;

   --  Best-effort slot teardown after a failed enumeration.
   procedure Disable_Slot (Slot : Unsigned_8) is
      Dummy : Unsigned_8;
   begin
      if Run_Command (0, Slot, Trb_Disable_Slot, Dummy) /= 0 then
         null;
      end if;
   end Disable_Slot;

   --  Controller discovery -----------------------------------------------

   --  Find the first PCI function whose class is 0C/03/30 (xHCI).
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
                  if (Cls / 16#1_0000_00#) = 16#0C#
                    and then ((Cls / 16#1_0000#) mod 256) = 16#03#
                    and then ((Cls / 16#100#) mod 256) = 16#30#
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

   --  Read BAR0, including the upper half when it is a 64-bit memory BAR.
   function Read_Bar return Unsigned_64 is
      Bar  : constant Unsigned_32 :=
        Pci_Read (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#10#);
      High : Unsigned_32 := 0;
   begin
      if (Bar and 1) /= 0 then
         return 0;
      end if;
      if (Bar and 16#06#) = 16#04# then
         High := Pci_Read (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#14#);
      end if;
      return Shift_Left (Unsigned_64 (High), 32)
             or Unsigned_64 (Bar and 16#FFFF_FFF0#);
   end Read_Bar;

   --  Clear the USB Legacy Support BIOS semaphore and SMI enables.
   procedure Take_Ownership is
      Hcc1 : Unsigned_32;
      Xecp : Unsigned_32;
      Base : Unsigned_64;
      Cap  : Unsigned_32;
      Id   : Unsigned_32;
      Next : Unsigned_32;
      Value : Unsigned_32;
   begin
      Hcc1 := Read32 (G_Bar + 16#10#);
      Xecp := Shift_Right (Hcc1, 16) and 16#FFFF#;
      if Xecp = 0 then
         return;
      end if;

      Base := G_Bar + Unsigned_64 (Xecp) * 4;
      for I in 1 .. 64 loop
         Cap := Read32 (Base);
         Id := Cap and 16#FF#;
         Next := Shift_Right (Cap, 8) and 16#FF#;
         if Id = 1 then
            Value := Read32 (Base + 4);
            Write32 (Base + 4, Value or 16#0100_0000#);
            for J in 1 .. 1_000_000 loop
               exit when (Read32 (Base + 4) and 16#0001_0000#) = 0;
            end loop;
            Write32 (Base + 8, 0);
            Write32 (Base + 12, 0);
            return;
         end if;
         exit when Next = 0;
         Base := Base + Unsigned_64 (Next) * 4;
      end loop;
   end Take_Ownership;

   --  Port handling ------------------------------------------------------

   --  Address of one PORTSC register in the operational register block.
   function Port_Addr (Port : Unsigned_32) return Unsigned_64 is
     (G_Op + 16#400# + Unsigned_64 (Port - 1) * 16#10#);

   --  Reset one root port and return the negotiated speed.
   function Reset_Port (Port : Unsigned_32;
                        Speed : out Unsigned_8) return Boolean is
      Sc : Unsigned_32;
      Settled : Boolean := False;
   begin
      Speed := 0;
      Sc := Read32 (Port_Addr (Port));
      if (Sc and Port_Ccs) = 0 then
         return False;
      end if;

      --  Clear stale change bits first; otherwise a PRC left over from a
      --  previous reset makes the wait below return before this one starts.
      Write32 (Port_Addr (Port), Port_Change or (Sc and 16#200#));
      Sc := Read32 (Port_Addr (Port));

      Write32 (Port_Addr (Port),
               (Sc and not 16#00FF_0002#) or Port_Pr);
      for I in 1 .. 1_000_000 loop
         Sc := Read32 (Port_Addr (Port));
         exit when (Sc and Port_Prc) /= 0;
      end loop;
      if (Sc and Port_Prc) = 0 then
         Log_Error (3, Port);
         return False;
      end if;

      Write32 (Port_Addr (Port), Port_Change or (Sc and 16#200#));
      for I in 1 .. 1_000_000 loop
         Sc := Read32 (Port_Addr (Port));
         if (Sc and Port_Pls_Mask) = 0 then
            Settled := True;
            exit;
         end if;
      end loop;

      Speed := Unsigned_8 (Shift_Right (Sc and Port_Speed_Mask, 10));
      if not Settled or else (Sc and Port_Ccs) = 0 or else Speed = 0 then
         Log_Error (3, Port);
         return False;
      end if;
      return True;
   end Reset_Port;

   --  Input context construction -----------------------------------------

   --  Build the slot + EP0 input context used by Address Device and
   --  Evaluate Context.
   procedure Build_Address_Context (D : Device_Record;
                                    Ep0_Max : Unsigned_16) is
      Input : constant Unsigned_64 := D.Input_Ctx;
      Slot  : constant Unsigned_64 := Input + 32;
      Ep0   : constant Unsigned_64 :=
        Input + 32 + Unsigned_64 (G_Ctx);
   begin
      Buffer_Zero (Addr (Input), Page_Size);
      Write32 (Input + 4, 3);
      Write32 (Slot, (Unsigned_32 (D.Speed) * 16#10_0000#)
                or (Unsigned_32 (1) * 16#0800_0000#));
      Write32 (Slot + 4, Unsigned_32 (D.Port) * 16#1_0000#);
      Write32 (Slot + 8, 0);
      Write32 (Slot + 12, 0);

      Write32 (Ep0, 0);
      Write32 (Ep0 + 4, (Unsigned_32 (3) * 2) or (Unsigned_32 (4) * 8)
                or (Unsigned_32 (Ep0_Max) * 16#1_0000#));
      Write32 (Ep0 + 8,
               Unsigned_32 (D.Ep0_Ring.Phys and 16#FFFF_FFFF#) or 1);
      Write32 (Ep0 + 12,
               Unsigned_32 (Shift_Right (D.Ep0_Ring.Phys, 32)));
      Write32 (Ep0 + 16, 8);
   end Build_Address_Context;

   --  Fill one bulk endpoint context in the Configure Endpoint input context.
   procedure Put_Bulk_Context (D : Device_Record; Endpoint_Id : Unsigned_8;
                               Ep_Type : Unsigned_32;
                               Max_Packet : Unsigned_16;
                               Ring_Phys : Unsigned_64) is
      Input : constant Unsigned_64 := D.Input_Ctx;
      Base  : constant Unsigned_64 :=
        Input + 32 + (Unsigned_64 (Endpoint_Id) + 1) * Unsigned_64 (G_Ctx);
   begin
      Write32 (Base, 0);
      Write32 (Base + 4, (Unsigned_32 (3) * 2) or (Ep_Type * 8)
                or (Unsigned_32 (Max_Packet) * 16#1_0000#));
      Write32 (Base + 8,
               Unsigned_32 (Ring_Phys and 16#FFFF_FFFF#) or 1);
      Write32 (Base + 12, Unsigned_32 (Shift_Right (Ring_Phys, 32)));
      Write32 (Base + 16, 1024);
   end Put_Bulk_Context;

   --  Build the full input context for Configure Endpoint (EP0 + BOT pair).
   procedure Build_Configure_Context (D : Device_Record) is
      Input : constant Unsigned_64 := D.Input_Ctx;
      Slot  : constant Unsigned_64 := Input + 32;
      Ep0   : constant Unsigned_64 :=
        Input + 32 + Unsigned_64 (G_Ctx);
      Add   : Unsigned_32;
      Last  : Unsigned_32;
   begin
      Buffer_Zero (Addr (Input), Page_Size);

      --  Add the slot and the two new endpoints.  EP0 is already running
      --  from Address Device, so it is deliberately not re-added here.
      Add := 1;
      Add := Add or Shift_Left (Unsigned_32 (1), Natural (D.Bulk_In_Id));
      Add := Add or Shift_Left (Unsigned_32 (1), Natural (D.Bulk_Out_Id));
      Write32 (Input + 4, Add);

      Last := Unsigned_32 (D.Bulk_In_Id);
      if Unsigned_32 (D.Bulk_Out_Id) > Last then
         Last := Unsigned_32 (D.Bulk_Out_Id);
      end if;
      if Last < 1 then
         Last := 1;
      end if;

      Write32 (Slot, (Unsigned_32 (D.Speed) * 16#10_0000#)
                or (Last * 16#0800_0000#));
      Write32 (Slot + 4, Unsigned_32 (D.Port) * 16#1_0000#);
      Write32 (Slot + 8, 0);
      Write32 (Slot + 12, 0);

      Write32 (Ep0, 0);
      Write32 (Ep0 + 4, (Unsigned_32 (3) * 2) or (Unsigned_32 (4) * 8)
                or (Unsigned_32 (D.Ep0_Max) * 16#1_0000#));
      Write32 (Ep0 + 8,
               Unsigned_32 (D.Ep0_Ring.Phys and 16#FFFF_FFFF#) or 1);
      Write32 (Ep0 + 12,
               Unsigned_32 (Shift_Right (D.Ep0_Ring.Phys, 32)));
      Write32 (Ep0 + 16, 8);

      Put_Bulk_Context (D, D.Bulk_Out_Id, 2, D.Bulk_Out_Max,
                        D.Bulk_Out_Ring.Phys);
      Put_Bulk_Context (D, D.Bulk_In_Id, 6, D.Bulk_In_Max,
                        D.Bulk_In_Ring.Phys);
   end Build_Configure_Context;

   --  Descriptor helpers -------------------------------------------------

   --  Write a little-endian 16-bit descriptor field.
   procedure Put16Le (A : Unsigned_64; Value : Unsigned_16) is
   begin
      Mmio_Write8 (A, Unsigned_8 (Value and 16#FF#));
      Mmio_Write8 (A + 1, Unsigned_8 (Shift_Right (Value, 8)));
   end Put16Le;

   --  Read a little-endian 16-bit descriptor field.
   function Get16Le (A : Unsigned_64) return Unsigned_16 is
     (Unsigned_16 (Mmio_Read8 (A))
      or Shift_Left (Unsigned_16 (Mmio_Read8 (A + 1)), 8));

   --  Build a GET_DESCRIPTOR setup packet in the setup page.
   procedure Put_Get_Descriptor (Type_Index : Unsigned_16;
                                 Length : Unsigned_16) is
      A : constant Unsigned_64 := G_Setup_Phys;
   begin
      Mmio_Write8 (A, 16#80#);
      Mmio_Write8 (A + 1, 6);
      Put16Le (A + 2, Type_Index);
      Put16Le (A + 4, 0);
      Put16Le (A + 6, Length);
   end Put_Get_Descriptor;

   --  One EP0 control transfer: Setup, optional Data, then Status.
   function Control_Transfer_Index (D : in out Device_Record;
                                    Setup_Phys : Unsigned_64;
                                    Buffer_Phys : Unsigned_64;
                                    Length : Unsigned_32;
                                    Direction_In : Boolean)
     return Integer is
      P3_Setup : Unsigned_32;
      P3_Data  : Unsigned_32;
      P3_Stat  : Unsigned_32;
      P0       : Unsigned_32;
      P1       : Unsigned_32;
      Trb_Phys : Unsigned_64;
   begin
      if Length > Max_Transfer or else Setup_Phys = 0 then
         return -1;
      end if;

      P0 := Read32 (Setup_Phys);
      P1 := Read32 (Setup_Phys + 4);
      P3_Setup := (Trb_Setup * 16#400#) or Ctrl_Idt or Ctrl_Chain;
      if Length = 0 then
         null;
      elsif Direction_In then
         P3_Setup := P3_Setup or Trt_In;
      else
         P3_Setup := P3_Setup or Trt_Out;
      end if;

      --  IDT is set, so the eight setup bytes go in the TRB parameter
      --  field itself; the parameter is not a DMA pointer here.
      if Ring_Push (D.Ep0_Ring, P0, P1, 8, P3_Setup, Trb_Phys) /= 0 then
         return -1;
      end if;

      if Length > 0 then
         P3_Data := (Trb_Data * 16#400#) or Ctrl_Chain;
         if Direction_In then
            P3_Data := P3_Data or Ctrl_Dir;
         end if;
         if Ring_Push (D.Ep0_Ring,
                       Unsigned_32 (Buffer_Phys and 16#FFFF_FFFF#),
                       Unsigned_32 (Shift_Right (Buffer_Phys, 32)),
                       Length,
                       P3_Data,
                       Trb_Phys) /= 0
         then
            return -1;
         end if;
         P3_Stat := (Trb_Status * 16#400#) or Ctrl_Ioc;
         if not Direction_In then
            P3_Stat := P3_Stat or Ctrl_Dir;
         end if;
      else
         --  A control transfer without a data stage always uses an IN
         --  status stage, so the direction bit is set here as well.
         P3_Stat := (Trb_Status * 16#400#) or Ctrl_Ioc or Ctrl_Dir;
      end if;

      if Ring_Push (D.Ep0_Ring, 0, 0, 0, P3_Stat, Trb_Phys) /= 0 then
         return -1;
      end if;
      Doorbell (Unsigned_32 (D.Slot_Id), 1);
      return Wait_Transfer (D.Slot_Id, 1, Trb_Phys);
   end Control_Transfer_Index;

   --  GET_DESCRIPTOR(device) and update EP0 max packet when needed.
   function Read_Device_Descriptor (D : in out Device_Record)
     return Boolean is
      Data : constant Unsigned_64 := G_Data_Phys;
      Actual_Max : Unsigned_16;
      Dummy : Unsigned_8;
   begin
      Put_Get_Descriptor (16#0100#, 18);
      if Control_Transfer_Index (D, G_Setup_Phys, Data, 18, True) /= 0 then
         Log_Error (6, 1);
         return False;
      end if;

      D.Class := Mmio_Read8 (Data + 4);
      D.Subclass := Mmio_Read8 (Data + 5);
      D.Protocol := Mmio_Read8 (Data + 6);
      D.Vendor := Get16Le (Data + 8);
      D.Product := Get16Le (Data + 10);

      --  bMaxPacketSize0 is a byte count on USB 2 and an exponent on USB 3.
      if D.Speed >= 4 then
         Actual_Max := 512;
      else
         Actual_Max := Unsigned_16 (Mmio_Read8 (Data + 7));
      end if;
      if Actual_Max /= 0 and then Actual_Max /= D.Ep0_Max then
         Build_Address_Context (D, Actual_Max);
         if Run_Command (D.Input_Ctx, D.Slot_Id, Trb_Evaluate_Context,
                         Dummy) = 0
         then
            D.Ep0_Max := Actual_Max;
         end if;
      end if;
      return True;
   end Read_Device_Descriptor;

   --  Walk the configuration descriptor for the BOT interface and its two
   --  bulk endpoints.
   procedure Parse_Config (D : in out Device_Record;
                           Total : Unsigned_16) is
      Base  : constant Unsigned_64 := G_Data_Phys;
      Index : Unsigned_32 := 0;
      Len   : Unsigned_8;
      Typ   : Unsigned_8;
      J     : Unsigned_32;
      ELen  : Unsigned_8;
      ETyp  : Unsigned_8;
      EAddr : Unsigned_8;
      EAttr : Unsigned_8;
      EMax  : Unsigned_16;
   begin
      while Index + 2 <= Unsigned_32 (Total) loop
         Len := Mmio_Read8 (Base + Unsigned_64 (Index));
         if Len < 2 then
            return;
         end if;
         Typ := Mmio_Read8 (Base + Unsigned_64 (Index) + 1);

         if Typ = 2 and then Len >= 9 then
            D.Config_Value := Mmio_Read8 (Base + Unsigned_64 (Index) + 5);
         elsif Typ = 4 and then Len >= 9 then
            D.Class := Mmio_Read8 (Base + Unsigned_64 (Index) + 5);
            D.Subclass := Mmio_Read8 (Base + Unsigned_64 (Index) + 6);
            D.Protocol := Mmio_Read8 (Base + Unsigned_64 (Index) + 7);
            D.Interface_Number :=
              Mmio_Read8 (Base + Unsigned_64 (Index) + 2);

            if D.Class = 8 and then D.Subclass = 6
              and then D.Protocol = 16#50#
            then
               J := Index + Unsigned_32 (Len);
               while J + 2 <= Unsigned_32 (Total) loop
                  ELen := Mmio_Read8 (Base + Unsigned_64 (J));
                  if ELen < 2 then
                     exit;
                  end if;
                  ETyp := Mmio_Read8 (Base + Unsigned_64 (J) + 1);
                  if ETyp = 4 then
                     exit;
                  end if;
                  if ETyp = 5 and then ELen >= 7 then
                     EAddr := Mmio_Read8 (Base + Unsigned_64 (J) + 2);
                     EAttr := Mmio_Read8 (Base + Unsigned_64 (J) + 3);
                     EMax := Get16Le (Base + Unsigned_64 (J) + 4)
                             and 16#07FF#;
                     if (EAttr and 3) = 2 then
                        if (EAddr and 16#80#) /= 0 then
                           if D.Bulk_In_Id = 0 then
                              D.Bulk_In_Addr := EAddr;
                              D.Bulk_In_Id :=
                                Unsigned_8 ((EAddr and 16#0F#) * 2 + 1);
                              D.Bulk_In_Max := EMax;
                           end if;
                        else
                           if D.Bulk_Out_Id = 0 then
                              D.Bulk_Out_Addr := EAddr;
                              D.Bulk_Out_Id :=
                                Unsigned_8 ((EAddr and 16#0F#) * 2);
                              D.Bulk_Out_Max := EMax;
                           end if;
                        end if;
                     end if;
                  end if;
                  J := J + Unsigned_32 (ELen);
               end loop;
               return;
            end if;
         end if;

         Index := Index + Unsigned_32 (Len);
      end loop;
   end Parse_Config;

   --  Read the configuration descriptor, first the header then the full body.
   function Read_Config_Descriptor (D : in out Device_Record)
     return Boolean is
      Data  : constant Unsigned_64 := G_Data_Phys;
      Total : Unsigned_16;
   begin
      Put_Get_Descriptor (16#0200#, 9);
      if Control_Transfer_Index (D, G_Setup_Phys, Data, 9, True) /= 0 then
         Log_Error (7, 1);
         return False;
      end if;

      Total := Get16Le (Data + 2);
      if Total < 9 then
         Log_Error (7, 2);
         return False;
      end if;
      if Total > 1024 then
         Total := 1024;
      end if;

      Put_Get_Descriptor (16#0200#, Total);
      if Control_Transfer_Index (D, G_Setup_Phys, Data,
                                 Unsigned_32 (Total), True) /= 0
      then
         Log_Error (7, 3);
         return False;
      end if;

      Parse_Config (D, Total);
      return True;
   end Read_Config_Descriptor;

   --  Issue SET_CONFIGURATION for the selected configuration value.
   function Set_Configuration (D : in out Device_Record) return Boolean is
      A : constant Unsigned_64 := G_Setup_Phys;
      Value : Unsigned_8 := D.Config_Value;
   begin
      if Value = 0 then
         Value := 1;
      end if;
      Mmio_Write8 (A, 0);
      Mmio_Write8 (A + 1, 9);
      Put16Le (A + 2, Unsigned_16 (Value));
      Put16Le (A + 4, 0);
      Put16Le (A + 6, 0);
      if Control_Transfer_Index (D, A, 0, 0, False) /= 0 then
         Log_Error (11, 1);
         return False;
      end if;
      return True;
   end Set_Configuration;

   --  Enable the two BOT bulk endpoints through Configure Endpoint.
   function Configure_Endpoints (D : in out Device_Record)
     return Boolean is
      Dummy : Unsigned_8;
   begin
      if D.Bulk_In_Id = 0 or else D.Bulk_Out_Id = 0 then
         return False;
      end if;
      Build_Configure_Context (D);
      if Run_Command (D.Input_Ctx, D.Slot_Id, Trb_Configure_Endpoint,
                      Dummy) /= 0
      then
         D.Bulk_In_Id := 0;
         D.Bulk_Out_Id := 0;
         Log_Error (10, 1);
         return False;
      end if;
      return True;
   end Configure_Endpoints;

   --  Device enumeration -------------------------------------------------

   --  Conservative EP0 packet size before the device descriptor is known.
   function Initial_Ep0_Max (Speed : Unsigned_8) return Unsigned_16 is
   begin
      if Speed >= 4 then
         return 512;
      elsif Speed = 3 then
         return 64;
      else
         return 8;
      end if;
   end Initial_Ep0_Max;

   --  Address, describe and configure one device on a root port.
   procedure Enumerate_Port (Port : Unsigned_32; Speed : Unsigned_8) is
      Slot      : Unsigned_8;
      Slot_Out  : Unsigned_8;
      Index     : Unsigned_32 := G_Count;
      D         : Device_Record;
      Ep0_Max   : Unsigned_16;
      Has_Bot   : Boolean;
   begin
      if G_Count >= Max_Devices or else Speed = 0 or else Speed = 2 then
         return;
      end if;

      if Run_Command (0, 0, Trb_Enable_Slot, Slot) /= 0 then
         Log_Error (4, Port);
         return;
      end if;
      if Slot = 0 then
         Log_Error (4, Port);
         return;
      end if;

      D.Slot_Id := Slot;
      D.Port := Unsigned_8 (Port);
      D.Speed := Speed;
      D.Device_Ctx := Alloc_Page;
      D.Input_Ctx := Alloc_Page;
      D.Ep0_Ring.Phys := Alloc_Page;
      D.Bulk_In_Ring.Phys := Alloc_Page;
      D.Bulk_Out_Ring.Phys := Alloc_Page;

      if D.Device_Ctx = 0 or else D.Input_Ctx = 0
        or else D.Ep0_Ring.Phys = 0
        or else D.Bulk_In_Ring.Phys = 0
        or else D.Bulk_Out_Ring.Phys = 0
      then
         Log_Error (5, Port);
         Disable_Slot (Slot);
         return;
      end if;

      Ring_Init (D.Ep0_Ring, D.Ep0_Ring.Phys, Ep_Entries);
      Ring_Init (D.Bulk_In_Ring, D.Bulk_In_Ring.Phys, Ep_Entries);
      Ring_Init (D.Bulk_Out_Ring, D.Bulk_Out_Ring.Phys, Ep_Entries);

      Ep0_Max := Initial_Ep0_Max (Speed);
      D.Ep0_Max := Ep0_Max;
      Write64 (G_Dcbaa + Unsigned_64 (Slot) * 8, D.Device_Ctx);
      Build_Address_Context (D, Ep0_Max);

      if Run_Command (D.Input_Ctx, Slot, Trb_Address_Device,
                      Slot_Out) /= 0
        or else Slot_Out = 0
      then
         Log_Error (5, Port);
         Disable_Slot (Slot);
         return;
      end if;
      D.Slot_Id := Slot_Out;

      if not Read_Device_Descriptor (D) then
         Disable_Slot (D.Slot_Id);
         return;
      end if;

      Has_Bot := Read_Config_Descriptor (D)
                 and then D.Bulk_In_Id /= 0
                 and then D.Bulk_Out_Id /= 0;

      if Has_Bot then
         if not Set_Configuration (D) then
            Disable_Slot (D.Slot_Id);
            return;
         end if;
         if not Configure_Endpoints (D) then
            Disable_Slot (D.Slot_Id);
            return;
         end if;
      end if;

      D.Used := True;
      G_Devices (Natural (Index)) := D;
      G_Count := G_Count + 1;
      Log_Device (Port, Unsigned_32 (Speed),
                  Unsigned_32 (D.Vendor), Unsigned_32 (D.Product),
                  Unsigned_32 (D.Class), Unsigned_32 (D.Subclass),
                  Unsigned_32 (D.Protocol));
   end Enumerate_Port;

   --  Public API ---------------------------------------------------------

   --  Reset the controller, start both rings and enumerate root ports.
   function Init return Interfaces.C.int is
      Hcs1     : Unsigned_32;
      Hcc1     : Unsigned_32;
      Version  : Unsigned_16;
      Command  : Unsigned_32;
      Settled  : Boolean;
      Sc       : Unsigned_32;
      Speed    : Unsigned_8;
      Port_First : Unsigned_32;
      Port_Last  : Unsigned_32;
   begin
      G_Count := 0;
      G_Bar := 0;

      if not Find_Controller then
         Log_No_Controller;
         return -1;
      end if;

      G_Bar := Read_Bar;
      if G_Bar = 0 or else G_Bar >= 16#1_0000_0000#
        or else G_Bar + 16#1000# >= 16#1_0000_0000#
      then
         Log_Error (1, 1);
         return -1;
      end if;

      --  Turn on memory decoding and bus mastering before the first MMIO
      --  read; firmware sometimes leaves the BAR window disabled.
      Command := Pci_Read (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#04#);
      Pci_Write (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, 16#04#,
                 (Command and 16#FFFF#) or 16#0006#);

      Hcs1 := Read32 (G_Bar + 4);
      G_Max_Slots := Min32 (Hcs1 and 16#FF#, 32);
      G_Max_Ports := Shift_Right (Hcs1, 24) and 16#FF#;
      if G_Max_Slots = 0 or else G_Max_Ports = 0 then
         Log_Error (1, 2);
         return -1;
      end if;
      if G_Bar + 16#1000# + Unsigned_64 (G_Max_Ports) * 16#20#
        >= 16#1_0000_0000#
      then
         Log_Error (1, 3);
         return -1;
      end if;

      G_Op := G_Bar + Unsigned_64 (Mmio_Read8 (G_Bar));
      G_Db := G_Bar + Unsigned_64 (Read32 (G_Bar + 16#14#));
      G_Rt := G_Bar + Unsigned_64 (Read32 (G_Bar + 16#18#));
      Version := Mmio_Read16 (G_Bar + 2);
      Hcc1 := Read32 (G_Bar + 16#10#);
      if (Hcc1 and Hcc_Csz) /= 0 then
         G_Ctx := 64;
      else
         G_Ctx := 32;
      end if;

      --  Ask the firmware to drop legacy USB ownership before touching the
      --  controller; some machines leave SMI trapping enabled otherwise.
      Take_Ownership;

      Command := Read32 (G_Op);
      if (Command and Usb_Cmd_Run) /= 0 then
         Write32 (G_Op, Command and not Usb_Cmd_Run);
         for I in 1 .. 1_000_000 loop
            exit when (Read32 (G_Op + 4) and Usb_Sts_Hch) = 0;
         end loop;
      end if;

      Write32 (G_Op, Read32 (G_Op) or Usb_Cmd_Reset);
      Settled := False;
      for I in 1 .. 1_000_000 loop
         if (Read32 (G_Op) and Usb_Cmd_Reset) = 0
           and then (Read32 (G_Op + 4) and Usb_Sts_Cnr) = 0
         then
            Settled := True;
            exit;
         end if;
      end loop;
      if not Settled then
         Log_Error (1, 4);
         return -1;
      end if;

      G_Dcbaa := Alloc_Page;
      G_Erst := Alloc_Page;
      G_Event_Phys := Alloc_Page;
      G_Cmd_Phys := Alloc_Page;
      G_Data_Phys := Alloc_Page;
      G_Setup_Phys := Alloc_Page;
      if G_Dcbaa = 0 or else G_Erst = 0 or else G_Event_Phys = 0
        or else G_Cmd_Phys = 0 or else G_Data_Phys = 0
        or else G_Setup_Phys = 0
      then
         Log_Error (2, 1);
         return -1;
      end if;

      Ring_Init (G_Cmd_Ring, G_Cmd_Phys, Command_Entries);
      G_Event_Dequeue := 0;
      G_Event_Cycle := 1;

      Write64 (G_Erst, G_Event_Phys);
      Write32 (G_Erst + 8, Event_Entries);
      Write32 (G_Erst + 12, 0);

      Write32 (G_Op + 16#38#, G_Max_Slots);
      Write64 (G_Op + 16#30#, G_Dcbaa);
      Write64 (G_Op + 16#18#, (G_Cmd_Phys and not 16#F#) or 1);
      Write32 (G_Rt + 16#20# + 16#08#, 1);
      Write64 (G_Rt + 16#20# + 16#10#, G_Erst);
      Write64 (G_Rt + 16#20# + 16#18#, G_Event_Phys or 8);

      Write32 (G_Op, Read32 (G_Op) or Usb_Cmd_Run);
      Settled := False;
      for I in 1 .. 1_000_000 loop
         if (Read32 (G_Op + 4) and Usb_Sts_Hch) = 0 then
            Settled := True;
            exit;
         end if;
      end loop;
      if not Settled then
         Log_Error (1, 5);
         return -1;
      end if;

      Log_Controller (G_Ctrl_Bus, G_Ctrl_Slot, G_Ctrl_Func, G_Bar,
                      G_Max_Ports, Unsigned_32 (Version));

      --  USB 2 low-speed devices cannot carry the BOT endpoints we need.
      --  Everything else on a root port is addressed and configured here;
      --  Usb_Msc decides which ones speak the mass-storage class.
      Port_First := 1;
      Port_Last := G_Max_Ports;
      for Port in Unsigned_32 range Port_First .. Port_Last loop
         Sc := Read32 (Port_Addr (Port));
         if (Sc and Port_Ccs) /= 0 then
            if Reset_Port (Port, Speed) then
               Enumerate_Port (Port, Speed);
            end if;
         end if;
      end loop;

      Log_Summary (G_Count);
      return 0;
   end Init;

   --  Number of successfully addressed devices, storage or otherwise.
   function Device_Count return Interfaces.C.int is
   begin
      return Interfaces.C.int (G_Count);
   end Device_Count;

   --  Copy one device record into the public storage-class view.
   procedure Get_Device (Index : Interfaces.C.int; Info : out Device_Info) is
      D : Device_Record;
   begin
      Info := G_Empty_Info;
      if Index < 0 or else Unsigned_32 (Index) >= G_Count then
         return;
      end if;
      D := G_Devices (Natural (Index));
      Info.Used := D.Used;
      Info.Slot_Id := D.Slot_Id;
      Info.Port := D.Port;
      Info.Speed := D.Speed;
      Info.Ep0_Max := D.Ep0_Max;
      Info.Vendor := D.Vendor;
      Info.Product := D.Product;
      Info.Class := D.Class;
      Info.Subclass := D.Subclass;
      Info.Protocol := D.Protocol;
      Info.Interface_Number := D.Interface_Number;
      Info.Config_Value := D.Config_Value;
      Info.Bulk_In_Addr := D.Bulk_In_Addr;
      Info.Bulk_Out_Addr := D.Bulk_Out_Addr;
      Info.Bulk_In_Id := D.Bulk_In_Id;
      Info.Bulk_Out_Id := D.Bulk_Out_Id;
      Info.Bulk_In_Max := D.Bulk_In_Max;
      Info.Bulk_Out_Max := D.Bulk_Out_Max;
   end Get_Device;

   --  Bound check for the public C API.
   function Valid (Index : Interfaces.C.int) return Boolean is
     (Index >= 0 and then Unsigned_32 (Index) < G_Count);

   --  Public EP0 transfer entry used by Usb_Msc for class requests.
   function Control_Transfer (Index        : Interfaces.C.int;
                              Setup_Phys   : Unsigned_64;
                              Buffer_Phys  : Unsigned_64;
                              Length       : Unsigned_32;
                              Direction_In : Boolean)
     return Interfaces.C.int is
   begin
      if not Valid (Index) or else Setup_Phys = 0 then
         return -1;
      end if;
      if Length > 0 and then Buffer_Phys = 0 then
         return -1;
      end if;
      return Interfaces.C.int
        (Control_Transfer_Index (G_Devices (Natural (Index)),
                                 Setup_Phys, Buffer_Phys, Length,
                                 Direction_In));
   end Control_Transfer;

   --  Public bulk transfer entry for CBW, data and CSW.
   function Bulk_Transfer (Index       : Interfaces.C.int;
                           In_Endpoint : Boolean;
                           Buffer_Phys : Unsigned_64;
                           Length      : Unsigned_32)
     return Interfaces.C.int is
      D : Device_Record;
      Ep : Unsigned_8;
      Ring : Ring_Record;
      P3 : Unsigned_32;
      Trb_Phys : Unsigned_64;
   begin
      if not Valid (Index) then
         return -1;
      end if;
      D := G_Devices (Natural (Index));
      if In_Endpoint then
         Ep := D.Bulk_In_Id;
         Ring := D.Bulk_In_Ring;
      else
         Ep := D.Bulk_Out_Id;
         Ring := D.Bulk_Out_Ring;
      end if;
      if Ep = 0 then
         return -1;
      end if;
      if Length = 0 then
         return 0;
      end if;
      if Length > Max_Transfer or else Buffer_Phys = 0 then
         return -1;
      end if;

      P3 := (Trb_Normal * 16#400#) or Ctrl_Ioc;
      if Ring_Push (Ring,
                    Unsigned_32 (Buffer_Phys and 16#FFFF_FFFF#),
                    Unsigned_32 (Shift_Right (Buffer_Phys, 32)),
                    Length, P3, Trb_Phys) /= 0
      then
         return -1;
      end if;

      if In_Endpoint then
         D.Bulk_In_Ring := Ring;
      else
         D.Bulk_Out_Ring := Ring;
      end if;
      G_Devices (Natural (Index)) := D;

      Doorbell (Unsigned_32 (D.Slot_Id), Unsigned_32 (Ep));
      return Interfaces.C.int (Wait_Transfer (D.Slot_Id, Ep, Trb_Phys));
   end Bulk_Transfer;

   --  Public endpoint reset used by the BOT reset-recovery path.
   function Reset_Endpoint (Index       : Interfaces.C.int;
                            Endpoint_Id : Unsigned_8)
     return Interfaces.C.int is
      D : Device_Record;
      Ring : Ring_Record;
      Dummy : Unsigned_8;
   begin
      if not Valid (Index) then
         return -1;
      end if;
      D := G_Devices (Natural (Index));
      if Endpoint_Id = 0 then
         return -1;
      end if;

      if Endpoint_Id = 1 then
         Ring := D.Ep0_Ring;
      elsif Endpoint_Id = D.Bulk_In_Id then
         Ring := D.Bulk_In_Ring;
      elsif Endpoint_Id = D.Bulk_Out_Id then
         Ring := D.Bulk_Out_Ring;
      else
         return -1;
      end if;

      --  The reset itself may fail when the endpoint is already running;
      --  the dequeue-pointer command below is the part that matters.
      if Run_Command (0, D.Slot_Id, Trb_Reset_Endpoint,
                      Dummy, Endpoint_Id) /= 0
      then
         null;
      end if;
      Ring_Reset (Ring);

      if Endpoint_Id = 1 then
         D.Ep0_Ring := Ring;
      elsif Endpoint_Id = D.Bulk_In_Id then
         D.Bulk_In_Ring := Ring;
      else
         D.Bulk_Out_Ring := Ring;
      end if;
      G_Devices (Natural (Index)) := D;

      if Run_Command (Ring.Phys or 1, D.Slot_Id, Trb_Set_Tr_Dequeue,
                      Dummy, Endpoint_Id) /= 0
      then
         return -1;
      end if;
      return 0;
   end Reset_Endpoint;
end Xhci;
