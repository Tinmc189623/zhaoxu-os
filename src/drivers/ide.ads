with Interfaces; use Interfaces;
with Interfaces.C;
with System;

package Ide is
   function Init return Interfaces.C.int
     with Export, Convention => C, External_Name => "ide_init";

   function Drive_Count return Interfaces.C.int
     with Export, Convention => C, External_Name => "ide_drive_count";

   function Drive_Atapi (Drive : Interfaces.C.int) return Interfaces.C.int
     with Export, Convention => C, External_Name => "ide_drive_atapi";

   function Drive_Bus (Drive : Interfaces.C.int) return Interfaces.C.int
     with Export, Convention => C, External_Name => "ide_drive_bus";

   function Drive_Unit (Drive : Interfaces.C.int) return Interfaces.C.int
     with Export, Convention => C, External_Name => "ide_drive_unit";

   function Drive_Sectors (Drive : Interfaces.C.int) return Unsigned_64
     with Export, Convention => C, External_Name => "ide_drive_sectors";

   function Drive_Model (Drive : Interfaces.C.int) return System.Address
     with Export, Convention => C, External_Name => "ide_drive_model";

   function Read_Block (Drive  : Interfaces.C.int;
                        Lba    : Unsigned_64;
                        Count  : Unsigned_32;
                        Buffer : System.Address)
     return Interfaces.C.int
     with Export, Convention => C, External_Name => "ide_drive_read";

   function Atapi_Packet (Drive    : Interfaces.C.int;
                          Cdb      : System.Address;
                          Cdb_Len  : Unsigned_32;
                          Data     : System.Address;
                          Data_Len : Unsigned_32)
     return Interfaces.C.int
     with Export, Convention => C, External_Name => "ide_atapi_packet";
end Ide;
