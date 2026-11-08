with Interfaces; use Interfaces;
with Interfaces.C;
with System;

package Sata is
   function Init return Interfaces.C.int
     with Export, Convention => C, External_Name => "sata_init";

   function Abar return Unsigned_64
     with Export, Convention => C, External_Name => "sata_abar";

   function Port_Mask return Unsigned_32
     with Export, Convention => C, External_Name => "sata_port_mask";

   function Port_Count return Interfaces.C.int
     with Export, Convention => C, External_Name => "sata_port_count";

   function Port_Det (Port : Interfaces.C.int) return Unsigned_32
     with Export, Convention => C, External_Name => "sata_port_det";

   function Port_Gen (Port : Interfaces.C.int) return Unsigned_32
     with Export, Convention => C, External_Name => "sata_port_gen";
end Sata;
