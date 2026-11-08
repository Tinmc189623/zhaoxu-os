with Interfaces;
with Interfaces.C;

package Serial is
   procedure Init (Baud : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "serial_init";

   procedure Putc (Value : Interfaces.Unsigned_8)
     with Export, Convention => C, External_Name => "serial_putc";

   function Getc return Interfaces.C.int
     with Export, Convention => C, External_Name => "serial_getc";
end Serial;
