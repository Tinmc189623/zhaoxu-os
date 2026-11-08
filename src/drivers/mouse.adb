with Interfaces; use Interfaces;

package body Mouse is
   Status_Port : constant Unsigned_16 := 16#64#;
   Data_Port   : constant Unsigned_16 := 16#60#;

   Status_Output_Full : constant Unsigned_8 := 16#01#;
   Status_Input_Full  : constant Unsigned_8 := 16#02#;
   Status_Aux_Data    : constant Unsigned_8 := 16#20#;

   Ack : constant Unsigned_8 := 16#FA#;

   G_X : Integer := 0;
   G_Y : Integer := 0;
   G_Buttons : Integer := 0;
   G_Width : Integer := 1;
   G_Height : Integer := 1;
   G_Ready : Integer := 0;

   G_Packet : array (0 .. 2) of Unsigned_8 := (others => 0);
   G_Index : Integer := 0;

   pragma Atomic (G_X);
   pragma Atomic (G_Y);
   pragma Atomic (G_Buttons);
   pragma Atomic (G_Ready);

   function Inb (Port : Unsigned_16) return Unsigned_8
     with Import, Convention => C, External_Name => "inb_port";

   procedure Outb (Port : Unsigned_16; Value : Unsigned_8)
     with Import, Convention => C, External_Name => "outb_port";

   function Wait_Input_Clear return Boolean is
   begin
      for I in 1 .. 100_000 loop
         if (Inb (Status_Port) and Status_Input_Full) = 0 then
            return True;
         end if;
      end loop;
      return False;
   end Wait_Input_Clear;

   procedure Write_Command (Command : Unsigned_8) is
      Ok : Boolean;
      pragma Unreferenced (Ok);
   begin
      Ok := Wait_Input_Clear;
      Outb (Status_Port, Command);
   end Write_Command;

   procedure Write_Data (Value : Unsigned_8) is
      Ok : Boolean;
      pragma Unreferenced (Ok);
   begin
      Ok := Wait_Input_Clear;
      Outb (Data_Port, Value);
   end Write_Data;

   --  Read one byte from the controller, waiting for the output buffer.
   --  Returns -1 on timeout so callers can report a missing device.
   function Read_Byte return Integer is
   begin
      for I in 1 .. 100_000 loop
         if (Inb (Status_Port) and Status_Output_Full) /= 0 then
            return Integer (Inb (Data_Port));
         end if;
      end loop;
      return -1;
   end Read_Byte;

   procedure Flush_Output is
      Count : Integer := 0;
   begin
      while (Inb (Status_Port) and Status_Output_Full) /= 0
        and then Count < 64
      loop
         declare
            Dummy : Unsigned_8 := Inb (Data_Port);
            pragma Unreferenced (Dummy);
         begin
            null;
         end;
         Count := Count + 1;
      end loop;
   end Flush_Output;

   --  Send a byte to the aux device (mouse) rather than the keyboard and
   --  wait for its ACK.
   function Send_Aux (Value : Unsigned_8) return Boolean is
   begin
      Write_Command (16#D4#);
      Write_Data (Value);
      return Read_Byte = Integer (Ack);
   end Send_Aux;

   function Init (Width : Integer; Height : Integer) return Integer is
   begin
      G_Width := Integer'Max (Width, 1);
      G_Height := Integer'Max (Height, 1);
      G_X := G_Width / 2;
      G_Y := G_Height / 2;
      G_Buttons := 0;
      G_Index := 0;
      G_Ready := 0;

      Flush_Output;

      --  Enable the aux clock/IRQ and the keyboard IRQ in the config byte.
      Write_Command (16#20#);
      declare
         Raw : constant Integer := Read_Byte;
      begin
         if Raw < 0 then
            return 0;
         end if;
         declare
            Config : Unsigned_8 := Unsigned_8 (Raw);
         begin
            Config := Config or Unsigned_8'(16#03#);    --  IRQ1 + IRQ12
            Config := Config and not Unsigned_8'(16#30#);  --  clocks on
            Write_Command (16#60#);
            Write_Data (Config);
         end;
      end;

      Write_Command (16#A8#);                --  enable aux port

      if not Send_Aux (16#F6#) then          --  set defaults
         return 0;
      end if;
      if not Send_Aux (16#F4#) then          --  enable reporting
         return 0;
      end if;

      G_Ready := 1;
      return 1;
   end Init;

   procedure Irq is
      Status : Unsigned_8 := Inb (Status_Port);
      Value : Unsigned_8;
   begin
      --  Leave keyboard bytes for IRQ1.
      if (Status and Status_Output_Full) = 0
        or else (Status and Status_Aux_Data) = 0
      then
         return;
      end if;

      Value := Inb (Data_Port);

      if G_Index = 0 and then (Value and Unsigned_8'(16#08#)) = 0 then
         --  Lost sync: the first packet byte always has bit 3 set.
         return;
      end if;

      G_Packet (G_Index) := Value;
      G_Index := G_Index + 1;
      if G_Index < 3 then
         return;
      end if;
      G_Index := 0;

      declare
         Flags : Unsigned_8 := G_Packet (0);
         Dx : Integer := Integer (G_Packet (1));
         Dy : Integer := Integer (G_Packet (2));
         New_X : Integer;
         New_Y : Integer;
      begin
         G_Buttons := Integer (Flags and Unsigned_8'(16#07#));

         if Dx >= 128 then
            Dx := Dx - 256;
         end if;
         if Dy >= 128 then
            Dy := Dy - 256;
         end if;

         --  Y grows downward on screen but up on the mouse.
         New_X := G_X + Dx;
         New_Y := G_Y - Dy;

         if New_X < 0 then
            New_X := 0;
         elsif New_X >= G_Width then
            New_X := G_Width - 1;
         end if;
         if New_Y < 0 then
            New_Y := 0;
         elsif New_Y >= G_Height then
            New_Y := G_Height - 1;
         end if;

         G_X := New_X;
         G_Y := New_Y;
      end;
   end Irq;

   function X return Integer is (G_X);
   function Y return Integer is (G_Y);
   function Buttons return Integer is (G_Buttons);
   function Ready return Integer is (G_Ready);
end Mouse;
