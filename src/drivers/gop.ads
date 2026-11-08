with Interfaces;
with System;

package Gop is
   procedure Fill_Rect (Fb : Interfaces.Unsigned_64;
                        Pitch : Interfaces.Unsigned_32;
                        X : Interfaces.Unsigned_32;
                        Y : Interfaces.Unsigned_32;
                        W : Interfaces.Unsigned_32;
                        H : Interfaces.Unsigned_32;
                        Color : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "gop_fill_rect";

   procedure Blit_Glyph (Fb : Interfaces.Unsigned_64;
                         Pitch : Interfaces.Unsigned_32;
                         X : Interfaces.Unsigned_32;
                         Y : Interfaces.Unsigned_32;
                         Glyph : System.Address;
                         Fg : Interfaces.Unsigned_32;
                         Bg : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "gop_blit_glyph";

   procedure Copy_Rows (Fb : Interfaces.Unsigned_64;
                        Pitch : Interfaces.Unsigned_32;
                        Src_Y : Interfaces.Unsigned_32;
                        Dst_Y : Interfaces.Unsigned_32;
                        Rows : Interfaces.Unsigned_32;
                        Width_Bytes : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "gop_copy_rows";

   procedure Clear (Fb : Interfaces.Unsigned_64;
                    Pitch : Interfaces.Unsigned_32;
                    Width : Interfaces.Unsigned_32;
                    Height : Interfaces.Unsigned_32;
                    Color : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "gop_clear";
end Gop;
