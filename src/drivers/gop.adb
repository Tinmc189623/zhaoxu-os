with Interfaces; use Interfaces;
with System; use System;
with System.Storage_Elements; use System.Storage_Elements;
with System.Address_To_Access_Conversions;

package body Gop is
   package U32_Conv is new System.Address_To_Access_Conversions (Unsigned_32);
   package U8_Conv is new System.Address_To_Access_Conversions (Unsigned_8);

   function To_Addr (Value : Unsigned_64) return Address is
     (To_Address (Integer_Address (Value)));

   function Addr_At (Base : Address; Offset : Unsigned_32) return Address is
     (To_Address (To_Integer (Base) + Integer_Address (Offset)));

   procedure Put_Pixel (Base : Address; Offset, Color : Unsigned_32) is
      P : U32_Conv.Object_Pointer := U32_Conv.To_Pointer (Addr_At (Base, Offset));
   begin
      P.all := Color;
   end Put_Pixel;

   procedure Fill_Rect (Fb : Unsigned_64;
                        Pitch : Unsigned_32;
                        X : Unsigned_32;
                        Y : Unsigned_32;
                        W : Unsigned_32;
                        H : Unsigned_32;
                        Color : Unsigned_32) is
      Base : constant Address := To_Addr (Fb);
   begin
      if W = 0 or H = 0 then
         return;
      end if;
      for Row in Unsigned_32 range 0 .. H - 1 loop
         for Col in Unsigned_32 range 0 .. W - 1 loop
            Put_Pixel (Base, (Y + Row) * Pitch + (X + Col) * 4, Color);
         end loop;
      end loop;
   end Fill_Rect;

   procedure Blit_Glyph (Fb : Unsigned_64;
                         Pitch : Unsigned_32;
                         X : Unsigned_32;
                         Y : Unsigned_32;
                         Glyph : Address;
                         Fg : Unsigned_32;
                         Bg : Unsigned_32) is
      Base : constant Address := To_Addr (Fb);
      Bits : Unsigned_8;
      Pixel : Unsigned_32;
      Offset : Unsigned_32;
   begin
      for Row in Unsigned_32 range 0 .. 7 loop
         Bits := U8_Conv.To_Pointer (Addr_At (Glyph, Row)).all;
         for Col in Unsigned_32 range 0 .. 7 loop
            if (Bits and Shift_Left (Unsigned_8 (1), Natural (Col))) /= 0 then
               Pixel := Fg;
            else
               Pixel := Bg;
            end if;
            Offset := (Y + Row * 2) * Pitch + (X + Col) * 4;
            Put_Pixel (Base, Offset, Pixel);
            Put_Pixel (Base, Offset + Pitch, Pixel);
         end loop;
      end loop;
   end Blit_Glyph;

   procedure Copy_Rows (Fb : Unsigned_64;
                        Pitch : Unsigned_32;
                        Src_Y : Unsigned_32;
                        Dst_Y : Unsigned_32;
                        Rows : Unsigned_32;
                        Width_Bytes : Unsigned_32) is
      Base : constant Address := To_Addr (Fb);
      Src_Addr : Address;
      Dst_Addr : Address;
      Byte : Unsigned_8;
      Src_Ptr : U8_Conv.Object_Pointer;
      Dst_Ptr : U8_Conv.Object_Pointer;
   begin
      for Row in Unsigned_32 range 0 .. Rows - 1 loop
         Src_Addr := Addr_At (Base, (Src_Y + Row) * Pitch);
         Dst_Addr := Addr_At (Base, (Dst_Y + Row) * Pitch);
         if Dst_Addr < Src_Addr then
            for I in Unsigned_32 range 0 .. Width_Bytes - 1 loop
               Src_Ptr := U8_Conv.To_Pointer (Addr_At (Src_Addr, I));
               Dst_Ptr := U8_Conv.To_Pointer (Addr_At (Dst_Addr, I));
               Byte := Src_Ptr.all;
               Dst_Ptr.all := Byte;
            end loop;
         else
            for I in reverse Unsigned_32 range 0 .. Width_Bytes - 1 loop
               Src_Ptr := U8_Conv.To_Pointer (Addr_At (Src_Addr, I));
               Dst_Ptr := U8_Conv.To_Pointer (Addr_At (Dst_Addr, I));
               Byte := Src_Ptr.all;
               Dst_Ptr.all := Byte;
            end loop;
         end if;
      end loop;
   end Copy_Rows;

   procedure Clear (Fb : Unsigned_64;
                    Pitch : Unsigned_32;
                    Width : Unsigned_32;
                    Height : Unsigned_32;
                    Color : Unsigned_32) is
   begin
      Fill_Rect (Fb, Pitch, 0, 0, Width, Height, Color);
   end Clear;
end Gop;
