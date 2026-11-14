--  Zhaoxu OS - USB Mass Storage Class (Bulk-Only Transport) driver.
--
--  Sits on top of the xHCI host controller and exposes the same block
--  device API as the AHCI driver.  The service dispatches disk numbers
--  below Ahci.Disk_Count to AHCI and the rest here.

with Interfaces; use Interfaces;
with Interfaces.C;
with System;

package Usb_Msc is
   --  Enumerate BOT devices and publish the disks that pass probing.
   function Init return Interfaces.C.int
     with Export, Convention => C, External_Name => "usb_msc_init";

   --  Number of USB mass-storage disks currently exported.
   function Disk_Count return Interfaces.C.int
     with Export, Convention => C, External_Name => "usb_msc_disk_count";

   --  Capacity in 512-byte sectors.
   function Disk_Sectors (Disk : Interfaces.C.int) return Unsigned_64
     with Export, Convention => C, External_Name => "usb_msc_disk_sectors";

   --  READ(10) through BOT, in eight-sector chunks.
   function Disk_Read (Disk   : Interfaces.C.int;
                       Lba    : Unsigned_64;
                       Count  : Unsigned_32;
                       Buffer : System.Address)
     return Interfaces.C.int
     with Export, Convention => C, External_Name => "usb_msc_disk_read";

   --  WRITE(10) through BOT, in eight-sector chunks.
   function Disk_Write (Disk   : Interfaces.C.int;
                        Lba    : Unsigned_64;
                        Count  : Unsigned_32;
                        Buffer : System.Address)
     return Interfaces.C.int
     with Export, Convention => C, External_Name => "usb_msc_disk_write";

   --  SYNCHRONIZE CACHE(10).
   function Disk_Flush (Disk : Interfaces.C.int) return Interfaces.C.int
     with Export, Convention => C, External_Name => "usb_msc_disk_flush";

   --  Root port number for the kernel disk table.
   function Disk_Port (Disk : Interfaces.C.int) return Interfaces.C.int
     with Export, Convention => C, External_Name => "usb_msc_disk_port";

   --  Null-terminated model string for the kernel disk table.
   function Disk_Model (Disk : Interfaces.C.int) return System.Address
     with Export, Convention => C, External_Name => "usb_msc_disk_model";
end Usb_Msc;
