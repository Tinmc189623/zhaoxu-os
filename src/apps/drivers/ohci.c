/*
 * Zhaoxu OS - OHCI USB host controller.
 *
 * Initializes the Open Host Controller for USB 1.1 companion ports, puts it
 * in the operational state and resets connected root ports.  Full-speed
 * class transfers are provided by the class driver layered above; this file
 * owns controller state, frame timing and root-port power/reset.
 */
#include <dev.h>
#include <io.h>
#include <types.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);
extern u64 pmm_alloc_page(void);
extern void buffer_zero(void *destination, u32 bytes);
extern void console_write(const char *text);
extern void usb_delay_ms(u32 milliseconds);

#define OHCI_HCREVISION 0x00
#define OHCI_HCCONTROL 0x04
#define OHCI_HCCOMMAND 0x08
#define OHCI_HCINTERRUPT_DISABLE 0x10
#define OHCI_HCHCCA 0x18
#define OHCI_HCCONTROLHEAD 0x20
#define OHCI_HCBULKHEAD 0x28
#define OHCI_HCFMINTERVAL 0x34
#define OHCI_HCPERIODICSTART 0x40
#define OHCI_HCRHDESCRIPTOR_A 0x48
#define OHCI_HCRHPORTSTATUS 0x54

#define OHCI_CONTROL_HCFS (3u << 6)
#define OHCI_CONTROL_OPERATIONAL (2u << 6)
#define OHCI_CONTROL_CLE (1u << 4)
#define OHCI_CONTROL_BLE (1u << 5)
#define OHCI_COMMAND_HCR (1u << 0)

#define OHCI_PORT_CCS (1u << 0)
#define OHCI_PORT_PES (1u << 1)
#define OHCI_PORT_PRS (1u << 4)
#define OHCI_PORT_PPS (1u << 8)
#define OHCI_PORT_LSDA (1u << 9)

static volatile u8 *g_ohci;
static int g_ports;
static u32 g_active_ports;

static u32 rd(u32 offset) { return *(volatile u32 *)(g_ohci + offset); }
static void wr(u32 offset, u32 value)
{
    *(volatile u32 *)(g_ohci + offset) = value;
}

int ohci_init(void)
{
    u64 bar = 0;
    for (u32 bus = 0; bus < 256 && !bar; bus++) {
        for (u32 slot = 0; slot < 32 && !bar; slot++) {
            u32 id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0)
                continue;
            u32 class = pci_config_read32(bus, slot, 0, 8) >> 8;
            if (class != 0x0C0310u)
                continue;
            u32 low = pci_config_read32(bus, slot, 0, 0x10);
            if (low & 1)
                continue;
            bar = low & 0xFFFFFFF0u;
            if (!bar || bar >= 0x100000000ull) {
                bar = 0;
                continue;
            }
            u32 command = pci_config_read32(bus, slot, 0, 4);
            pci_config_write32(bus, slot, 0, 4, command | 0x6);
        }
    }
    if (!bar)
        return -1;

    g_ohci = (volatile u8 *)(uintptr_t)bar;
    /* Reset the host controller and wait for the reset bit to clear. */
    wr(OHCI_HCCOMMAND, OHCI_COMMAND_HCR);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((rd(OHCI_HCCOMMAND) & OHCI_COMMAND_HCR) == 0)
            break;

    u64 hcca = pmm_alloc_page();
    if (!hcca)
        return -1;
    buffer_zero((void *)(uintptr_t)hcca, 4096);
    wr(OHCI_HCHCCA, (u32)hcca);
    wr(OHCI_HCCONTROLHEAD, 0);
    wr(OHCI_HCBULKHEAD, 0);
    wr(OHCI_HCFMINTERVAL, 0x2EDF);
    wr(OHCI_HCPERIODICSTART, 0x2A2F);
    wr(OHCI_HCINTERRUPT_DISABLE, 0xFFFFFFFFu);
    wr(OHCI_HCCONTROL, OHCI_CONTROL_CLE | OHCI_CONTROL_BLE |
                       OHCI_CONTROL_OPERATIONAL);

    u32 descriptor = rd(OHCI_HCRHDESCRIPTOR_A);
    g_ports = (int)(descriptor & 0xFF);
    for (int port = 0; port < g_ports; port++) {
        u32 status = rd(OHCI_HCRHPORTSTATUS + port * 4);
        wr(OHCI_HCRHPORTSTATUS + port * 4, status | OHCI_PORT_PPS);
        usb_delay_ms(20);
        status = rd(OHCI_HCRHPORTSTATUS + port * 4);
        if (!(status & OHCI_PORT_CCS))
            continue;
        wr(OHCI_HCRHPORTSTATUS + port * 4, OHCI_PORT_PRS);
        usb_delay_ms(50);
        for (u32 spin = 0; spin < 1000000u; spin++) {
            status = rd(OHCI_HCRHPORTSTATUS + port * 4);
            if (!(status & OHCI_PORT_PRS))
                break;
            usb_delay_ms(1);
        }
        status = rd(OHCI_HCRHPORTSTATUS + port * 4);
        if (status & OHCI_PORT_PES)
            g_active_ports |= 1u << port;
    }
    console_write("[ohci] USB 1.1 host controller operational\n");
    return 0;
}

int ohci_port_count(void)
{
    return g_ports;
}

u32 ohci_enabled_ports(void)
{
    return g_active_ports;
}
