/*
 * Zhaoxu OS - SDHCI (SD/MMC host controller) driver.
 *
 * Polled PIO implementation for PCI SD host controllers: card detection,
 * CMD0/CMD8/ACMD41/CMD2/CMD3/CMD9/CMD7/CMD16 initialization, CSD capacity
 * decoding for both byte-addressed and block-addressed cards, and single
 * block reads/writes through the buffer-data port.  It registers as one
 * removable block device.
 */
#include <dev.h>
#include <io.h>
#include <types.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);
extern void console_write(const char *text);

#define SDHCI_DMA 0x00
#define SDHCI_BLOCK_SIZE 0x04
#define SDHCI_BLOCK_COUNT 0x06
#define SDHCI_ARGUMENT 0x08
#define SDHCI_TRANSFER 0x0C
#define SDHCI_COMMAND 0x0E
#define SDHCI_RESPONSE 0x10
#define SDHCI_BUFFER 0x20
#define SDHCI_PRESENT 0x24
#define SDHCI_POWER 0x29
#define SDHCI_CLOCK 0x2C
#define SDHCI_TIMEOUT 0x2E
#define SDHCI_RESET 0x2F
#define SDHCI_NORMAL_INT 0x30
#define SDHCI_ERROR_INT 0x32
#define SDHCI_CAPABILITY 0x40

#define SDHCI_INT_COMMAND_COMPLETE 0x0001
#define SDHCI_INT_TRANSFER_COMPLETE 0x0002
#define SDHCI_INT_BUFFER_WRITE_READY 0x0010
#define SDHCI_INT_BUFFER_READ_READY 0x0020
#define SDHCI_INT_ERROR 0x8000

#define SDHCI_PRESENT_CMD_INHIBIT 0x00000001
#define SDHCI_PRESENT_DAT_INHIBIT 0x00000002
#define SDHCI_PRESENT_CARD_INSERTED 0x00010000

static volatile u8 *g_regs;
static u32 g_base_clock;
static u32 g_rca;
static u64 g_sectors;
static int g_block_addressed;
static int g_ready;

static u8 rd8(u32 offset) { return *(volatile u8 *)(g_regs + offset); }
static u16 rd16(u32 offset) { return *(volatile u16 *)(g_regs + offset); }
static u32 rd32(u32 offset) { return *(volatile u32 *)(g_regs + offset); }
static void wr8(u32 offset, u8 value)
{
    *(volatile u8 *)(g_regs + offset) = value;
}
static void wr16(u32 offset, u16 value)
{
    *(volatile u16 *)(g_regs + offset) = value;
}
static void wr32(u32 offset, u32 value)
{
    *(volatile u32 *)(g_regs + offset) = value;
}

/* Program the SD clock divider for the requested bus frequency. */
static void set_clock(u32 hz)
{
    if (!g_base_clock || !hz)
        hz = 400000;
    u32 divider = g_base_clock / hz;
    if (divider < 1)
        divider = 1;
    if (divider > 1023)
        divider = 1023;
    wr16(SDHCI_CLOCK, 0);
    u16 value = (u16)(((divider & 0xFF) << 8) |
                      (((divider >> 8) & 3) << 6));
    wr16(SDHCI_CLOCK, value);
    for (u32 spin = 0; spin < 1000000; spin++) {
        if (rd16(SDHCI_CLOCK) & 0x0002)
            break;
    }
    wr16(SDHCI_CLOCK, (u16)(value | 0x0004));
}

/* Wait for the command and data lines to become idle. */
static int wait_idle(int data)
{
    for (u32 spin = 0; spin < 5000000u; spin++) {
        u32 present = rd32(SDHCI_PRESENT);
        u32 mask = SDHCI_PRESENT_CMD_INHIBIT |
                   (data ? SDHCI_PRESENT_DAT_INHIBIT : 0);
        if ((present & mask) == 0)
            return 0;
    }
    return -1;
}

static void clear_status(void)
{
    wr16(SDHCI_NORMAL_INT, 0xFFFF);
    wr16(SDHCI_ERROR_INT, 0xFFFF);
}

/* Issue one command and return the four response words.  `response_type`
 * uses the SDHCI encoding: 0 none, 1 136-bit, 2 48-bit, 3 48-bit busy. */
static int send_command(u32 index, u32 argument, u32 response_type,
                        u32 response[4])
{
    if (wait_idle(0) != 0)
        return -1;
    clear_status();
    wr32(SDHCI_ARGUMENT, argument);
    u16 command = (u16)((index << 8) | response_type);
    if (response_type)
        command |= 0x0018; /* CRC check and index check */
    wr16(SDHCI_COMMAND, command);

    for (u32 spin = 0; spin < 5000000u; spin++) {
        u16 normal = rd16(SDHCI_NORMAL_INT);
        u16 error = rd16(SDHCI_ERROR_INT);
        if (error)
            return -1;
        if (normal & SDHCI_INT_COMMAND_COMPLETE) {
            if (response) {
                response[0] = rd32(SDHCI_RESPONSE + 0);
                response[1] = rd32(SDHCI_RESPONSE + 4);
                response[2] = rd32(SDHCI_RESPONSE + 8);
                response[3] = rd32(SDHCI_RESPONSE + 12);
            }
            clear_status();
            return 0;
        }
        if (normal & SDHCI_INT_ERROR)
            return -1;
    }
    return -1;
}

/* Single-block transfer through the PIO buffer port. */
static int transfer_block(int write, u64 lba, void *buffer)
{
    if (!g_ready || !g_sectors || lba >= g_sectors)
        return -1;
    if (wait_idle(1) != 0)
        return -1;
    clear_status();

    u32 address = g_block_addressed ? (u32)lba : (u32)(lba * 512);
    wr16(SDHCI_BLOCK_SIZE, 512);
    wr16(SDHCI_BLOCK_COUNT, 1);
    wr16(SDHCI_TRANSFER, (u16)(write ? 0x0002 : 0x0012));

    u32 response[4];
    if (send_command(write ? 24 : 17, address, 2, response) != 0)
        return -1;

    u32 *words = buffer;
    if (write) {
        for (u32 spin = 0; spin < 5000000u; spin++) {
            if (rd16(SDHCI_NORMAL_INT) & SDHCI_INT_BUFFER_WRITE_READY)
                break;
            if (rd16(SDHCI_ERROR_INT))
                return -1;
        }
        for (int i = 0; i < 128; i++)
            wr32(SDHCI_BUFFER, words[i]);
        for (u32 spin = 0; spin < 5000000u; spin++) {
            if (rd16(SDHCI_NORMAL_INT) & SDHCI_INT_TRANSFER_COMPLETE)
                break;
            if (rd16(SDHCI_ERROR_INT))
                return -1;
        }
    } else {
        for (u32 spin = 0; spin < 5000000u; spin++) {
            if (rd16(SDHCI_NORMAL_INT) & SDHCI_INT_BUFFER_READ_READY)
                break;
            if (rd16(SDHCI_ERROR_INT))
                return -1;
        }
        for (int i = 0; i < 128; i++)
            words[i] = rd32(SDHCI_BUFFER);
        for (u32 spin = 0; spin < 5000000u; spin++) {
            if (rd16(SDHCI_NORMAL_INT) & SDHCI_INT_TRANSFER_COMPLETE)
                break;
            if (rd16(SDHCI_ERROR_INT))
                return -1;
        }
    }
    clear_status();
    return 0;
}

/* Decode CSD capacity and addressing mode. */
static void parse_csd(const u32 response[4])
{
    u32 r0 = response[3];
    u32 r1 = response[2];
    u32 r2 = response[1];
    u32 r3 = response[0];
    (void)r3;
    u32 structure = (r0 >> 30) & 3;
    u64 bytes = 0;
    if (structure == 1) {
        u32 c_size = ((r1 & 0x3F) << 16) | ((r2 >> 8) & 0xFFFF);
        bytes = ((u64)c_size + 1) * 512 * 1024;
        g_block_addressed = 1;
    } else {
        u32 read_bl_len = (r1 >> 16) & 0x0F;
        u32 c_size = ((r1 & 0x3FF) << 2) | ((r2 >> 30) & 3);
        u32 c_size_mult = (r2 >> 15) & 7;
        bytes = ((u64)c_size + 1) << (c_size_mult + 2 + read_bl_len);
        g_block_addressed = 0;
    }
    g_sectors = bytes / 512;
}

int sdhci_init(void)
{
    u64 bar = 0;
    for (u32 bus = 0; bus < 256 && !bar; bus++) {
        for (u32 slot = 0; slot < 32 && !bar; slot++) {
            for (u32 function = 0; function < 8 && !bar; function++) {
                u32 id = pci_config_read32(bus, slot, function, 0);
                if (id == 0xFFFFFFFFu || id == 0)
                    continue;
                u32 class = pci_config_read32(bus, slot, function, 8) >> 8;
                if (class != 0x080501u && class != 0x080500u)
                    continue;
                u32 low = pci_config_read32(bus, slot, function, 0x10);
                if (!(low & 0x1)) /* memory BAR is required */
                    continue;
                bar = low & 0xFFFFFFF0u;
                if (bar >= 0x100000000ull) {
                    bar = 0;
                    continue;
                }
                u32 command = pci_config_read32(bus, slot, function, 4);
                pci_config_write32(bus, slot, function, 4, command | 0x6);
            }
        }
    }
    if (!bar)
        return -1;

    g_regs = (volatile u8 *)(uintptr_t)bar;
    wr8(SDHCI_RESET, 1);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((rd8(SDHCI_RESET) & 1) == 0)
            break;
    wr8(SDHCI_TIMEOUT, 0x0E);
    wr8(SDHCI_POWER, 0x0F);

    u32 capability = rd32(SDHCI_CAPABILITY);
    g_base_clock = (capability >> 8) & 0xFF;
    if (!g_base_clock)
        g_base_clock = 50;
    g_base_clock *= 1000000u;
    set_clock(400000);

    if ((rd32(SDHCI_PRESENT) & SDHCI_PRESENT_CARD_INSERTED) == 0) {
        console_write("[sdhci] no card inserted\n");
        return -1;
    }

    u32 response[4];
    if (send_command(0, 0, 0, NULL) != 0)
        return -1;
    int high_capacity = 0;
    if (send_command(8, 0x1AA, 2, response) == 0 &&
        (response[0] & 0xFFF) == 0x1AA)
        high_capacity = 1;

    int ready = 0;
    for (u32 attempt = 0; attempt < 100 && !ready; attempt++) {
        if (send_command(55, g_rca << 16, 2, response) != 0)
            return -1;
        u32 argument = 0x40FF8000u | (high_capacity ? 0x40000000u : 0);
        if (send_command(41, argument, 2, response) == 0 &&
            (response[0] & 0x80000000u))
            ready = 1;
    }
    if (!ready)
        return -1;

    if (send_command(2, 0, 1, response) != 0)
        return -1;
    if (send_command(3, 0, 2, response) != 0)
        return -1;
    g_rca = response[0] >> 16;
    if (send_command(9, g_rca << 16, 1, response) != 0)
        return -1;
    parse_csd(response);
    if (!g_sectors)
        return -1;
    if (send_command(7, g_rca << 16, 3, response) != 0)
        return -1;
    if (send_command(16, 512, 2, response) != 0)
        return -1;
    set_clock(25000000);
    g_ready = 1;
    console_write("[sdhci] SD card ready\n");
    return 0;
}

int sdhci_disk_count(void)
{
    return g_ready ? 1 : 0;
}

u64 sdhci_disk_sectors(void)
{
    return g_ready ? g_sectors : 0;
}

const char *sdhci_disk_model(void)
{
    return "SD/MMC card";
}

int sdhci_disk_read(u64 lba, u32 count, void *buffer)
{
    u8 *dst = buffer;
    for (u32 i = 0; i < count; i++)
        if (transfer_block(0, lba + i, dst + i * 512) != 0)
            return -1;
    return 0;
}

int sdhci_disk_write(u64 lba, u32 count, const void *buffer)
{
    const u8 *src = buffer;
    for (u32 i = 0; i < count; i++)
        if (transfer_block(1, lba + i, (void *)(src + i * 512)) != 0)
            return -1;
    return 0;
}

int sdhci_disk_flush(void)
{
    return g_ready ? 0 : -1;
}
