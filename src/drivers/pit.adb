with Interfaces; use Interfaces;

package body Pit is
   procedure Outb (Port : Unsigned_16; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "outb_port";

   Pit_Base : constant Unsigned_32 := 1_193_182;

   procedure Init (Hz : Unsigned_32) is
      Divisor : Unsigned_32;
   begin
      if Hz = 0 then
         return;
      end if;

      Divisor := Pit_Base / Hz;
      if Divisor = 0 then
         Divisor := 1;
      elsif Divisor > 65_535 then
         Divisor := 65_535;
      end if;

      Outb (16#43#, 16#36#);       --  channel 0, lo/hi byte, mode 3
      Outb (16#40#, Unsigned_8 (Divisor and 16#FF#));
      Outb (16#40#, Unsigned_8 (Shift_Right (Divisor, 8) and 16#FF#));
   end Init;
end Pit;
