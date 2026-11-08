with Interfaces; use Interfaces;
with Interfaces.C;

package body Serial is
   --  C-side port I/O helpers (kernel/arch/x86_64/io.c).
   procedure Outb (Port : Unsigned_16; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "outb_port";

   function Inb (Port : Unsigned_16) return Unsigned_8
     with Import, Convention => C, External_Name => "inb_port";

   Base : constant Unsigned_16 := 16#3F8#;

   procedure Init (Baud : Unsigned_32) is
      Divisor : Unsigned_16;
   begin
      if Baud = 0 then
         Divisor := 1;
      else
         Divisor := Unsigned_16 (115200 / Baud);
         if Divisor = 0 then
            Divisor := 1;
         end if;
      end if;

      Outb (Base + 1, 0);          --  disable UART interrupts
      Outb (Base + 3, 16#80#);     --  DLAB on
      Outb (Base + 0, Unsigned_8 (Divisor and 16#FF#));
      Outb (Base + 1, Unsigned_8 (Shift_Right (Divisor, 8)));
      Outb (Base + 3, 3);          --  8 bits, no parity, one stop
      Outb (Base + 2, 16#C7#);     --  FIFO enable + clear
      Outb (Base + 4, 16#0B#);     --  DTR / RTS / OUT2
   end Init;

   procedure Putc (Value : Unsigned_8) is
   begin
      while (Inb (Base + 5) and 16#20#) = 0 loop
         null;
      end loop;
      Outb (Base + 0, Value);
   end Putc;

   function Getc return Interfaces.C.int is
   begin
      if (Inb (Base + 5) and 1) = 0 then
         return Interfaces.C.int (-1);
      end if;
      return Interfaces.C.int (Inb (Base + 0));
   end Getc;
end Serial;
