#ifndef ZHAOXU_PCI_H
#define ZHAOXU_PCI_H

#include <types.h>

u32 pci_config_read32(u8 bus, u8 slot, u8 function, u8 offset);
void pci_config_write32(u8 bus, u8 slot, u8 function, u8 offset, u32 value);
u32 pci_enumerate(void);

#endif
