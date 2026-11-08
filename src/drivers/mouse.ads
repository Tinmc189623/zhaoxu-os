with Interfaces;

package Mouse is
   --  PS/2 aux-port mouse on the 8042 controller (IRQ 12).
   --  The driver tracks an absolute cursor position clamped to the screen
   --  bounds passed to Init; the desktop polls it once per frame.

   function Init (Width : Integer; Height : Integer) return Integer
     with Export, Convention => C, External_Name => "mouse_init";

   procedure Irq
     with Export, Convention => C, External_Name => "mouse_irq";

   function X return Integer
     with Export, Convention => C, External_Name => "mouse_x";

   function Y return Integer
     with Export, Convention => C, External_Name => "mouse_y";

   function Buttons return Integer
     with Export, Convention => C, External_Name => "mouse_buttons";

   function Ready return Integer
     with Export, Convention => C, External_Name => "mouse_ready";
end Mouse;
