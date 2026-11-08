#include <io.h>
#include <kprintf.h>
#include <pci.h>
#include <types.h>

static const char *class_name(u8 class_code, u8 subclass)
{
    switch (class_code) {
    case 0x00: return "Unclassified";
    case 0x01: return "Mass storage";
    case 0x02: return "Network";
    case 0x03: return subclass == 0 ? "VGA" : "Display";
    case 0x04: return "Multimedia";
    case 0x05: return "Memory";
    case 0x06: return "Bridge";
    case 0x07: return "Communication";
    case 0x08: return "System peripheral";
    case 0x09: return "Input";
    case 0x0A: return "Docking";
    case 0x0B: return "Processor";
    case 0x0C: return "Serial bus";
    case 0x0D: return "Wireless";
    case 0x0E: return "Intelligent I/O";
    case 0x0F: return "Satellite";
    case 0x10: return "Encryption";
    case 0x11: return "Signal processing";
    default: return "Unknown";
    }
}

u32 pci_config_read32(u8 bus, u8 slot, u8 function, u8 offset)
{
    u32 address = 0x80000000u |
                  ((u32)bus << 16) |
                  ((u32)slot << 11) |
                  ((u32)function << 8) |
                  (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

u32 pci_enumerate(void)
{
    u32 found = 0;
    kprintf("Bus Dev Fun  Vendor Device Class\n");
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 slot = 0; slot < 32; slot++) {
            u32 id = pci_config_read32((u8)bus, (u8)slot, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF)
                continue;

            u8 header = (u8)(pci_config_read32((u8)bus, (u8)slot, 0, 0x0C) >> 16);
            u32 functions = (header & 0x80) ? 8 : 1;
            for (u32 function = 0; function < functions; function++) {
                id = pci_config_read32((u8)bus, (u8)slot, (u8)function, 0);
                if ((id & 0xFFFF) == 0xFFFF)
                    continue;
                u32 class_reg = pci_config_read32((u8)bus, (u8)slot, (u8)function, 0x08);
                u8 class_code = (u8)(class_reg >> 24);
                u8 subclass = (u8)(class_reg >> 16);
                kprintf("%03u %02u  %u   0x%04x 0x%04x %s\n",
                        bus, slot, function, id & 0xFFFF, id >> 16,
                        class_name(class_code, subclass));
                found++;
            }
        }
    }
    kprintf("%u PCI function(s) found.\n", found);
    return found;
}
