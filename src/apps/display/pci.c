/*
 * Zhaoxu OS - legacy PCI configuration access for the display service.
 *
 * The display service has no PCIe ECAM window: it only needs the virtio-gpu
 * device on bus 0, which mechanism #1 reaches through 0xCF8/0xCFC.  The
 * driver service uses the Ada PCI/PCIe packages instead.
 */
#include <io.h>
#include <types.h>

/* Read one aligned 32-bit configuration dword. */
u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset)
{
    u32 address = 0x80000000u | ((bus & 0xFF) << 16) |
                  ((slot & 0x1F) << 11) | ((function & 7) << 8) |
                  (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC + (offset & 3));
}

/* Write one aligned 32-bit configuration dword. */
void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                        u32 value)
{
    u32 address = 0x80000000u | ((bus & 0xFF) << 16) |
                  ((slot & 0x1F) << 11) | ((function & 7) << 8) |
                  (offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC + (offset & 3), value);
}
