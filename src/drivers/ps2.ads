with Interfaces;

package Ps2 is
   procedure Init
     with Export, Convention => C, External_Name => "ps2_init";

   function Status return Interfaces.Unsigned_8
     with Export, Convention => C, External_Name => "ps2_status";

   function Read_Data return Interfaces.Unsigned_8
     with Export, Convention => C, External_Name => "ps2_read_data";
end Ps2;
