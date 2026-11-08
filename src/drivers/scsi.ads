with Interfaces; use Interfaces;
with Interfaces.C;
with System;

package Scsi is
   function Init return Interfaces.C.int
     with Export, Convention => C, External_Name => "scsi_init";

   function Device_Count return Interfaces.C.int
     with Export, Convention => C, External_Name => "scsi_device_count";

   function Vendor (Dev : Interfaces.C.int) return System.Address
     with Export, Convention => C, External_Name => "scsi_vendor";

   function Product (Dev : Interfaces.C.int) return System.Address
     with Export, Convention => C, External_Name => "scsi_product";

   function Revision (Dev : Interfaces.C.int) return System.Address
     with Export, Convention => C, External_Name => "scsi_revision";

   function Capacity (Dev : Interfaces.C.int) return Unsigned_64
     with Export, Convention => C, External_Name => "scsi_capacity";

   function Read_Blocks (Dev    : Interfaces.C.int;
                         Lba    : Unsigned_64;
                         Count  : Unsigned_32;
                         Buffer : System.Address)
     return Interfaces.C.int
     with Export, Convention => C, External_Name => "scsi_read";
end Scsi;
