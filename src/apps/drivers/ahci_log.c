/*
 * Zhaoxu OS - logging glue for the SPARK AHCI driver.
 *
 * Ada cannot call variadic kprintf, so the driver passes numbers and the
 * model string through these small helpers.
 */
#include <kprintf.h>
#include <types.h>

void ahci_log_no_controller(void)
{
    kprintf("[ahci] no AHCI controller found\n");
}

void ahci_log_abar(u64 abar)
{
    kprintf("[ahci] unusable ABAR 0x%llx\n", abar);
}

void ahci_log_controller(u32 bus, u32 slot, u32 func, u64 abar,
                         u32 ports, u32 major, u32 minor)
{
    kprintf("[ahci] controller %02x:%02x.%u, ABAR 0x%llx, %u ports, v%u.%u\n",
            bus, slot, func, abar, ports, major, minor);
}

void ahci_log_port_code(u32 port, u32 code)
{
    const char *what = "unknown failure";
    switch (code) {
    case 1: what = "DMA page allocation failed"; break;
    case 2: what = "engine did not stop"; break;
    case 3: what = "engine did not start"; break;
    case 4: what = "IDENTIFY failed"; break;
    case 5: what = "drive reports zero sectors"; break;
    case 6: what = "not an ATA disk (ATAPI skipped)"; break;
    default: break;
    }
    kprintf("[ahci] port %u: %s\n", port, what);
}

void ahci_log_disk(u32 port, const char *model, u64 sectors)
{
    kprintf("[ahci] port %u: %s, %llu sectors (%llu MiB)\n",
            port, model, sectors, (sectors * 512) >> 20);
}

void ahci_log_summary(u32 count)
{
    kprintf("[ahci] %u block device(s) online\n", count);
}

void ahci_log_cmd_fail(u32 port, u32 cmd, u64 lba, u32 count,
                       u32 ci, u32 isr, u32 tfd, u32 serr)
{
    kprintf("[ahci] port %u cmd %02x lba %llu x%u failed: "
            "CI 0x%x IS 0x%x TFD 0x%x SERR 0x%x\n",
            port, cmd, lba, count, ci, isr, tfd, serr);
}
