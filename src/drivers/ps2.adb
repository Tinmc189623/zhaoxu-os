with Interfaces; use Interfaces;

package body Ps2 is
   function Inb (Port : Unsigned_16) return Unsigned_8
     with Import, Convention => C, External_Name => "inb_port";

   procedure Init is
   begin
      --  Drain any bytes left in the controller output buffer.
      for I in 1 .. 32 loop
         if (Inb (16#64#) and 1) = 0 then
            return;
         end if;
         declare
            Dummy : Unsigned_8 := Inb (16#60#);
         begin
            pragma Unreferenced (Dummy);
         end;
      end loop;
   end Init;

   function Status return Unsigned_8 is
     (Inb (16#64#));

   function Read_Data return Unsigned_8 is
     (Inb (16#60#));
end Ps2;
