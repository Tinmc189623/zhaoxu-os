/*
 * Zhaoxu OS - logging glue for the Ada USB stack.
 *
 * The Ada packages cannot call variadic kprintf directly, so every message
 * that carries numbers or a model string goes through these helpers.
 */
#include <kprintf.h>
#include <types.h>

void xhci_log_no_controller(void)
{
    kprintf("[usb ] no xHCI controller found\n");
}

void xhci_log_controller(u32 bus, u32 slot, u32 func, u64 bar, u32 ports,
                         u32 version)
{
    kprintf("[usb ] xHCI %02x:%02x.%u, MMIO 0x%llx, %u ports, v%x\n",
            bus, slot, func, bar, ports, version);
}

void xhci_log_error(u32 stage, u32 code)
{
    const char *what = "unknown failure";
    switch (stage) {
    case 1: what = "controller initialisation"; break;
    case 2: what = "DMA ring allocation"; break;
    case 3: what = "root port reset"; break;
    case 4: what = "enable slot"; break;
    case 5: what = "address device"; break;
    case 6: what = "device descriptor"; break;
    case 7: what = "configuration descriptor"; break;
    case 8: what = "command completion"; break;
    case 9: what = "transfer completion"; break;
    case 10: what = "configure endpoint"; break;
    case 11: what = "set configuration"; break;
    default: break;
    }
    kprintf("[usb ] xHCI %s failed (code %u)\n", what, code);
}

void xhci_log_device(u32 port, u32 speed, u32 vendor, u32 product,
                     u32 cls, u32 subclass, u32 protocol)
{
    kprintf("[usb ] port %u: %04x:%04x class %02x/%02x/%02x, speed %u\n",
            port, vendor, product, cls, subclass, protocol, speed);
}

void xhci_log_summary(u32 count)
{
    kprintf("[usb ] %u device(s) addressed\n", count);
}

void usb_msc_log_disk(u32 index, const char *model, u64 sectors)
{
    kprintf("[usb ] disk %u: %s, %llu sectors (%llu MiB)\n",
            index, model && model[0] ? model : "USB mass storage",
            sectors, (sectors * 512) >> 20);
}

void usb_msc_log_summary(u32 count)
{
    kprintf("[usb ] %u mass-storage disk(s) online\n", count);
}

void usb_msc_log_error(u32 stage, i32 status)
{
    const char *what = "unknown failure";
    switch (stage) {
    case 1: what = "USB storage initialisation"; break;
    case 2: what = "TEST UNIT READY"; break;
    case 3: what = "READ CAPACITY(10)"; break;
    case 4: what = "READ(10)"; break;
    case 5: what = "WRITE(10)"; break;
    case 6: what = "SYNCHRONIZE CACHE(10)"; break;
    case 7: what = "BOT reset recovery"; break;
    default: break;
    }
    kprintf("[usb ] %s failed (%d)\n", what, status);
}
