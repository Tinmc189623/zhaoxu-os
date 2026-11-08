/*
 * Zhaoxu OS - presentation glue for the SPARK PCI/PCIe drivers.
 *
 * Ada cannot call variadic kprintf, so the enumeration is done in
 * drivers/pci.adb and each line is emitted from here.
 */
#include <kprintf.h>
#include <types.h>

static const char *class_name(u32 class_code, u32 subclass)
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

void pci_report_header(void)
{
    kprintf("Bus Dev Fun  Vendor Device Class\n");
}

void pci_report_device(u32 bus, u32 slot, u32 func, u32 id, u32 class_reg)
{
    u32 class_code = class_reg >> 24;
    u32 subclass = (class_reg >> 16) & 0xFF;
    kprintf("%03u %02u  %u   0x%04x 0x%04x %s\n",
            bus, slot, func, id & 0xFFFF, id >> 16,
            class_name(class_code, subclass));
}

void pci_report_summary(u32 found, u32 ecam)
{
    kprintf("%u PCI function(s) found.\n", found);
    if (ecam)
        kprintf("[pci ] config space: PCIe ECAM\n");
    else
        kprintf("[pci ] config space: legacy 0xCF8/0xCFC\n");
}
