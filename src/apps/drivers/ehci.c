/*
 * Zhaoxu OS - EHCI USB host controller.
 *
 * Brings up the high-speed host controller, installs an empty asynchronous
 * schedule and resets every connected root port.  Transfer rings are left to
 * the USB class drivers; xHCI handles the storage class on this platform,
 * while EHCI covers machines that expose only USB 2.0.
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

#define EHCI_USBCMD 0x00
#define EHCI_USBSTS 0x04
#define EHCI_USBINTR 0x08
#define EHCI_CTRLDSSEG 0x10
#define EHCI_ASYNCLIST 0x18
#define EHCI_CONFIGFLAG 0x30
#define EHCI_PORTSC 0x34

#define EHCI_CMD_RUN 0x00000001u
#define EHCI_CMD_RESET 0x00000002u
#define EHCI_CMD_ASYNC 0x00000020u

#define PORT_CONNECT 0x00000001u
#define PORT_ENABLE 0x00000002u
#define PORT_RESET 0x00000100u
#define PORT_POWER 0x00001000u

struct ehci_qh {
    u32 link;
    u32 characteristics;
    u32 capabilities;
    u32 current;
    u32 next;
    u32 alternate;
    u32 token;
    u32 buffers[5];
} __attribute__((packed));

static volatile u8 *g_op;
static u64 g_qh_phys;
static int g_ports;
static u32 g_speed_mask;

/* Read one operational register. */
static u32 op_read(u32 offset)
{
    return *(volatile u32 *)(g_op + offset);
}

static void op_write(u32 offset, u32 value)
{
    *(volatile u32 *)(g_op + offset) = value;
}

int ehci_init(void)
{
    u64 bar = 0;
    for (u32 bus = 0; bus < 256 && !bar; bus++) {
        for (u32 slot = 0; slot < 32 && !bar; slot++) {
            u32 id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0)
                continue;
            u32 class = pci_config_read32(bus, slot, 0, 8) >> 8;
            if (class != 0x0C0320u)
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

    volatile u8 *regs = (volatile u8 *)(uintptr_t)bar;
    u8 cap_length = regs[0];
    u32 hcs_params = *(volatile u32 *)(regs + 4);
    g_ports = (int)(hcs_params & 0x0F);
    g_op = regs + cap_length;

    op_write(EHCI_USBCMD, EHCI_CMD_RESET);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((op_read(EHCI_USBCMD) & EHCI_CMD_RESET) == 0)
            break;
    op_write(EHCI_USBINTR, 0);
    op_write(EHCI_CTRLDSSEG, 0);
    op_write(EHCI_CONFIGFLAG, 1);

    /* One self-linked reclamation head is a valid empty async schedule. */
    g_qh_phys = pmm_alloc_page();
    if (!g_qh_phys)
        return -1;
    buffer_zero((void *)(uintptr_t)g_qh_phys, 4096);
    struct ehci_qh *qh = (struct ehci_qh *)(uintptr_t)g_qh_phys;
    qh->link = (u32)g_qh_phys | 0x2;
    qh->characteristics = 1u << 15; /* head of reclamation list */
    op_write(EHCI_ASYNCLIST, (u32)g_qh_phys);
    op_write(EHCI_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASYNC);

    int found = 0;
    for (int port = 0; port < g_ports; port++) {
        u32 status = op_read(EHCI_PORTSC + port * 4);
        if (!(status & PORT_CONNECT))
            continue;
        found++;
        op_write(EHCI_PORTSC + port * 4,
                 (status & ~PORT_RESET) | PORT_POWER);
        usb_delay_ms(20);
        status = op_read(EHCI_PORTSC + port * 4);
        op_write(EHCI_PORTSC + port * 4, status | PORT_RESET);
        usb_delay_ms(50);
        status = op_read(EHCI_PORTSC + port * 4);
        op_write(EHCI_PORTSC + port * 4, status & ~PORT_RESET);
        usb_delay_ms(20);
        status = op_read(EHCI_PORTSC + port * 4);
        if (status & PORT_ENABLE)
            g_speed_mask |= 1u << port;
    }
    console_write("[ehci] USB 2.0 host controller ");
    console_write(found ? "with attached device(s)\n" : "ready, no devices\n");
    return 0;
}

int ehci_port_count(void)
{
    return g_ports;
}

u32 ehci_enabled_ports(void)
{
    return g_speed_mask;
}
