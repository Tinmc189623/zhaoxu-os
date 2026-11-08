#ifndef ZHAOXU_PCI_H
#define ZHAOXU_PCI_H

#include <types.h>

/* Implemented in Ada (src/drivers/pci.adb, pcie.adb). */
void pcie_init(u64 rsdp);
u32 pcie_active(void);
u64 pcie_ecam_base(void);
u32 pcie_config_read32(u32 bus, u32 dev, u32 func, u32 off);
void pcie_config_write32(u32 bus, u32 dev, u32 func, u32 off, u32 value);

u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset, u32 value);
u32 pci_enumerate(void);

#endif
