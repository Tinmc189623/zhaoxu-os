/*
 * Zhaoxu OS - Intel 8254x/82574L Ethernet driver.
 *
 * Polled RX/TX descriptor rings with a MAC address read from the receive
 * address registers (falling back to the EEPROM).  Ring buffers are
 * page-aligned physical pages from dev_alloc_page(), because the driver
 * process passes their addresses to the NIC as DMA without translation.
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

#define E1000_CTRL 0x0000
#define E1000_STATUS 0x0008
#define E1000_EERD 0x0014
#define E1000_RCTL 0x0100
#define E1000_TCTL 0x0400
#define E1000_TIPG 0x0410
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH 0x2810
#define E1000_RDT 0x2818
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH 0x3810
#define E1000_TDT 0x3818
#define E1000_RAL 0x5400
#define E1000_RAH 0x5404
#define E1000_MTA 0x5200

#define E1000_CTRL_RST (1u << 26)
#define E1000_CTRL_SLU (1u << 6)
#define E1000_STATUS_LU (1u << 1)

#define E1000_TCTL_EN (1u << 1)
#define E1000_TCTL_PSP (1u << 3)
#define E1000_TCTL_CT (0x10u << 4)
#define E1000_TCTL_COLD (0x40u << 12)

#define E1000_RCTL_EN (1u << 1)
#define E1000_RCTL_SBP (1u << 2)
#define E1000_RCTL_BAM (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)

#define E1000_TXD_CMD_EOP 0x01
#define E1000_TXD_CMD_IFCS 0x02
#define E1000_TXD_CMD_RS 0x08
#define E1000_TXD_STAT_DD 0x01
#define E1000_RXD_STAT_DD 0x01
#define E1000_RXD_STAT_EOP 0x02

#define RING_COUNT 32
#define BUFFER_SIZE 2048

struct e1000_tx_desc {
    u64 address;
    u16 length;
    u8 checksum_offset;
    u8 command;
    u8 status;
    u8 checksum_start;
    u16 special;
} __attribute__((packed));

struct e1000_rx_desc {
    u64 address;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} __attribute__((packed));

struct e1000 {
    volatile u8 *regs;
    u8 mac[6];
    u32 rx_tail;
    u32 tx_tail;
    u64 rx_desc_phys;
    u64 tx_desc_phys;
    u64 rx_buffers[RING_COUNT];
    u64 tx_buffers[RING_COUNT];
    int link;
};

static struct e1000 g_nic;

static u32 reg_read(u32 offset)
{
    return *(volatile u32 *)(g_nic.regs + offset);
}

static void reg_write(u32 offset, u32 value)
{
    *(volatile u32 *)(g_nic.regs + offset) = value;
}

/* Read one 16-bit word from the 93C46-compatible serial EEPROM. */
static int eeprom_read(u8 address, u16 *value)
{
    reg_write(E1000_EERD, ((u32)address << 8) | 1);
    for (u32 spin = 0; spin < 1000000u; spin++) {
        u32 data = reg_read(E1000_EERD);
        if (data & (1u << 4)) {
            *value = (u16)(data >> 16);
            return 0;
        }
    }
    return -1;
}

static void read_mac(void)
{
    u32 low = reg_read(E1000_RAL);
    u32 high = reg_read(E1000_RAH);
    if (high & 0x80000000u) {
        g_nic.mac[0] = (u8)low;
        g_nic.mac[1] = (u8)(low >> 8);
        g_nic.mac[2] = (u8)(low >> 16);
        g_nic.mac[3] = (u8)(low >> 24);
        g_nic.mac[4] = (u8)high;
        g_nic.mac[5] = (u8)(high >> 8);
        return;
    }
    u16 words[3];
    for (u8 i = 0; i < 3; i++)
        if (eeprom_read(i, &words[i]) != 0)
            words[i] = 0;
    g_nic.mac[0] = (u8)words[0];
    g_nic.mac[1] = (u8)(words[0] >> 8);
    g_nic.mac[2] = (u8)words[1];
    g_nic.mac[3] = (u8)(words[1] >> 8);
    g_nic.mac[4] = (u8)words[2];
    g_nic.mac[5] = (u8)(words[2] >> 8);
}

int e1000_init(void)
{
    u64 bar = 0;
    for (u32 bus = 0; bus < 256 && !bar; bus++) {
        for (u32 slot = 0; slot < 32 && !bar; slot++) {
            u32 id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0)
                continue;
            u32 class = pci_config_read32(bus, slot, 0, 8) >> 8;
            u32 vendor = id & 0xFFFF;
            if (class != 0x020000u && vendor != 0x8086)
                continue;
            if (vendor != 0x8086)
                continue;
            u32 low = pci_config_read32(bus, slot, 0, 0x10);
            if (low & 1) /* I/O BAR, not the MMIO register block */
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

    g_nic.regs = (volatile u8 *)(uintptr_t)bar;
    reg_write(E1000_CTRL, reg_read(E1000_CTRL) | E1000_CTRL_RST);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((reg_read(E1000_CTRL) & E1000_CTRL_RST) == 0)
            break;
    reg_write(E1000_CTRL, reg_read(E1000_CTRL) | E1000_CTRL_SLU);
    for (u32 i = 0; i < 128; i++)
        reg_write(E1000_MTA + i * 4, 0);

    read_mac();
    if (!g_nic.mac[0] && !g_nic.mac[1] && !g_nic.mac[2])
        return -1;

    g_nic.rx_desc_phys = pmm_alloc_page();
    g_nic.tx_desc_phys = pmm_alloc_page();
    if (!g_nic.rx_desc_phys || !g_nic.tx_desc_phys)
        return -1;
    buffer_zero((void *)(uintptr_t)g_nic.rx_desc_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_nic.tx_desc_phys, 4096);

    struct e1000_rx_desc *rx =
        (struct e1000_rx_desc *)(uintptr_t)g_nic.rx_desc_phys;
    struct e1000_tx_desc *tx =
        (struct e1000_tx_desc *)(uintptr_t)g_nic.tx_desc_phys;
    for (u32 i = 0; i < RING_COUNT; i++) {
        g_nic.rx_buffers[i] = pmm_alloc_page();
        g_nic.tx_buffers[i] = pmm_alloc_page();
        if (!g_nic.rx_buffers[i] || !g_nic.tx_buffers[i])
            return -1;
        rx[i].address = g_nic.rx_buffers[i];
        rx[i].status = 0;
        tx[i].address = g_nic.tx_buffers[i];
        tx[i].status = E1000_TXD_STAT_DD;
    }

    reg_write(E1000_RDBAL, (u32)g_nic.rx_desc_phys);
    reg_write(E1000_RDBAH, (u32)(g_nic.rx_desc_phys >> 32));
    reg_write(E1000_RDLEN, RING_COUNT * 16);
    reg_write(E1000_RDH, 0);
    reg_write(E1000_RDT, RING_COUNT - 1);

    reg_write(E1000_TDBAL, (u32)g_nic.tx_desc_phys);
    reg_write(E1000_TDBAH, (u32)(g_nic.tx_desc_phys >> 32));
    reg_write(E1000_TDLEN, RING_COUNT * 16);
    reg_write(E1000_TDH, 0);
    reg_write(E1000_TDT, 0);

    reg_write(E1000_TIPG, 0x0060200A);
    reg_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                           E1000_TCTL_CT | E1000_TCTL_COLD);
    reg_write(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_SBP |
                           E1000_RCTL_BAM | E1000_RCTL_SECRC);
    g_nic.link = (reg_read(E1000_STATUS) & E1000_STATUS_LU) != 0;
    console_write("[e1000] link ready\n");
    return 0;
}

void e1000_get_mac(u8 mac[6])
{
    for (int i = 0; i < 6; i++)
        mac[i] = g_nic.mac[i];
}

int e1000_link_up(void)
{
    return (reg_read(E1000_STATUS) & E1000_STATUS_LU) != 0;
}

int e1000_send(const void *data, u32 length)
{
    if (!g_nic.regs || !data || !length || length > BUFFER_SIZE)
        return -1;
    struct e1000_tx_desc *tx =
        (struct e1000_tx_desc *)(uintptr_t)g_nic.tx_desc_phys;
    u32 index = g_nic.tx_tail;
    if (!(tx[index].status & E1000_TXD_STAT_DD))
        return -1;
    u8 *buffer = (u8 *)(uintptr_t)g_nic.tx_buffers[index];
    for (u32 i = 0; i < length; i++)
        buffer[i] = ((const u8 *)data)[i];
    tx[index].length = (u16)length;
    tx[index].command = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
                        E1000_TXD_CMD_RS;
    tx[index].status = 0;
    g_nic.tx_tail = (index + 1) % RING_COUNT;
    reg_write(E1000_TDT, g_nic.tx_tail);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if (tx[index].status & E1000_TXD_STAT_DD)
            return 0;
    return -1;
}

/* Copy one received frame out of the ring.  Returns the frame length, zero
 * when the ring is empty and -1 on a descriptor error. */
int e1000_receive(void *buffer, u32 capacity)
{
    if (!g_nic.regs || !buffer)
        return -1;
    struct e1000_rx_desc *rx =
        (struct e1000_rx_desc *)(uintptr_t)g_nic.rx_desc_phys;
    u32 head = reg_read(E1000_RDH);
    u32 index = g_nic.rx_tail;
    if (index == head)
        return 0;
    if (!(rx[index].status & E1000_RXD_STAT_DD))
        return 0;
    if (rx[index].errors)
        goto done;
    u32 length = rx[index].length;
    if (length > capacity)
        length = capacity;
    const u8 *source = (const u8 *)(uintptr_t)g_nic.rx_buffers[index];
    for (u32 i = 0; i < length; i++)
        ((u8 *)buffer)[i] = source[i];
    rx[index].status = 0;
    g_nic.rx_tail = (index + 1) % RING_COUNT;
    reg_write(E1000_RDT, (g_nic.rx_tail + RING_COUNT - 1) % RING_COUNT);
    return (int)length;

done:
    rx[index].status = 0;
    g_nic.rx_tail = (index + 1) % RING_COUNT;
    reg_write(E1000_RDT, (g_nic.rx_tail + RING_COUNT - 1) % RING_COUNT);
    return -1;
}
