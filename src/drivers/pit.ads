with Interfaces;

package Pit is
   procedure Init (Hz : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "pit_init";
end Pit;
