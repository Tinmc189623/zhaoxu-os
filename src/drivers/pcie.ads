with Interfaces;

package Pcie is
   procedure Init (Rsdp : Interfaces.Unsigned_64)
     with Export, Convention => C, External_Name => "pcie_init";

   function Active return Interfaces.Unsigned_32
     with Export, Convention => C, External_Name => "pcie_active";

   function Ecam_Base return Interfaces.Unsigned_64
     with Export, Convention => C, External_Name => "pcie_ecam_base";

   function Config_Read (Bus  : Interfaces.Unsigned_32;
                         Dev  : Interfaces.Unsigned_32;
                         Func : Interfaces.Unsigned_32;
                         Off  : Interfaces.Unsigned_32)
     return Interfaces.Unsigned_32
     with Export, Convention => C, External_Name => "pcie_config_read32";

   procedure Config_Write (Bus   : Interfaces.Unsigned_32;
                           Dev   : Interfaces.Unsigned_32;
                           Func  : Interfaces.Unsigned_32;
                           Off   : Interfaces.Unsigned_32;
                           Value : Interfaces.Unsigned_32)
     with Export, Convention => C, External_Name => "pcie_config_write32";
end Pcie;
