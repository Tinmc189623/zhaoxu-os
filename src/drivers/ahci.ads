with Interfaces; use Interfaces;
with Interfaces.C;
with System;

package Ahci is
   function Init return Interfaces.C.int
     with Export, Convention => C, External_Name => "ahci_init";

   function Disk_Count return Interfaces.C.int
     with Export, Convention => C, External_Name => "ahci_disk_count";

   function Disk_Sectors (Disk : Interfaces.C.int) return Unsigned_64
     with Export, Convention => C, External_Name => "ahci_disk_sectors";

   function Disk_Read (Disk   : Interfaces.C.int;
                       Lba    : Unsigned_64;
                       Count  : Unsigned_32;
                       Buffer : System.Address)
     return Interfaces.C.int
     with Export, Convention => C, External_Name => "ahci_disk_read";

   function Disk_Write (Disk   : Interfaces.C.int;
                        Lba    : Unsigned_64;
                        Count  : Unsigned_32;
                        Buffer : System.Address)
     return Interfaces.C.int
     with Export, Convention => C, External_Name => "ahci_disk_write";

   function Disk_Flush (Disk : Interfaces.C.int) return Interfaces.C.int
     with Export, Convention => C, External_Name => "ahci_disk_flush";

   function Disk_Port (Disk : Interfaces.C.int) return Interfaces.C.int
     with Export, Convention => C, External_Name => "ahci_disk_port";

   function Disk_Model (Disk : Interfaces.C.int) return System.Address
     with Export, Convention => C, External_Name => "ahci_disk_model";
end Ahci;
