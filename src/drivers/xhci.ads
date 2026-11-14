--  Zhaoxu OS - xHCI host controller interface.
--
--  The storage class driver (Usb_Msc) uses this package to enumerate root
--  ports, address devices, and move control/bulk transfers.  Everything is
--  polled: the driver service does not install a PCI interrupt handler, so
--  the event ring is drained from the request loop.

with Interfaces; use Interfaces;
with Interfaces.C;
with System;

package Xhci is
   Max_Devices : constant := 4;

   type Device_Info is record
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
   end record;

   --  Locate the first xHCI controller, reset it, and enumerate the devices
   --  on its root ports.  Returns 0 when a controller was initialised.
   function Init return Interfaces.C.int;

   --  Number of devices that finished addressing.  Non-storage devices are
   --  counted too; Usb_Msc filters by interface class.
   function Device_Count return Interfaces.C.int;

   --  Copy the descriptor information for one addressed device.
   procedure Get_Device (Index : Interfaces.C.int; Info : out Device_Info);

   --  One control transfer.  Setup_Phys points at the eight-byte setup
   --  packet, Buffer_Phys at the data buffer.  Length may be zero.
   function Control_Transfer (Index        : Interfaces.C.int;
                              Setup_Phys   : Unsigned_64;
                              Buffer_Phys  : Unsigned_64;
                              Length       : Unsigned_32;
                              Direction_In : Boolean)
     return Interfaces.C.int;

   --  One bulk transfer on the device's BOT endpoint.  The caller keeps the
   --  transfer at or below 4 KiB so a single Normal TRB covers it.
   function Bulk_Transfer (Index       : Interfaces.C.int;
                           In_Endpoint : Boolean;
                           Buffer_Phys : Unsigned_64;
                           Length      : Unsigned_32)
     return Interfaces.C.int;

   --  Reset one endpoint and put its transfer ring back at the first TRB.
   --  Used by the BOT reset-recovery path after a phase error or a stall.
   function Reset_Endpoint (Index       : Interfaces.C.int;
                            Endpoint_Id : Unsigned_8)
     return Interfaces.C.int;
end Xhci;
