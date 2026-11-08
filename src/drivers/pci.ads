with Interfaces;

package Pci is
   function Config_Read (Bus   : Interfaces.Unsigned_32;
                         Slot  : Interfaces.Unsigned_32;
                         Func  : Interfaces.Unsigned_32;
                         Off   : Interfaces.Unsigned_32)
     return Interfaces.Unsigned_32
     with Export, Convention => C, External_Name => "pci_config_read32";

   procedure Config_Write (Bus   : Interfaces.Unsigned_32;
                           Slot  : Interfaces.Unsigned_32;
                           Func  : Interfaces.Unsigned_32;
                           Off   : Interfaces.Unsigned_32;
                           Value : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "pci_config_write32";

   function Enumerate return Interfaces.Unsigned_32
     with Export, Convention => C, External_Name => "pci_enumerate";
end Pci;
