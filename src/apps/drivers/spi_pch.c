/*
 * Zhaoxu OS - Intel PCH SPI flash controller.
 *
 * The PCH exposes its SPI flash controller as function 5 of the LPC bridge.
 * Only hardware sequencing is used: the driver programs the opcode through
 * OPTYPE/OPMENU and moves up to four bytes per hardware cycle through the
 * FDATA registers.  This is enough for JEDEC identification and for reading
 * the descriptor/BIOS regions.
 */
#include <dev.h>
#include <io.h>
#include <types.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);
extern void console_write(const char *text);

#define SPIBAR_HSFSTS 0x04
#define SPIBAR_FADDR 0x08
#define SPIBAR_FDATA0 0x10
#define SPIBAR_FRAP 0x50
#define SPIBAR_FREG0 0x54
#define SPIBAR_SSFC 0x90
#define SPIBAR_PREOP 0x94
#define SPIBAR_OPTYPE 0x96
#define SPIBAR_OPMENU 0x98

#define HSFSTS_FDONE (1u << 0)
#define HSFSTS_FCERR (1u << 1)
#define HSFSTS_SCIP (1u << 5)

static volatile u8 *g_spi;
static u8 g_flash_id[3];

static u32 rd(u32 offset) { return *(volatile u32 *)(g_spi + offset); }
static void wr(u32 offset, u32 value)
{
    *(volatile u32 *)(g_spi + offset) = value;
}

/* Wait for the hardware sequencer to leave the busy state. */
static int wait_idle(void)
{
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((rd(SPIBAR_HSFSTS) & HSFSTS_SCIP) == 0)
            return 0;
    return -1;
}

/* Issue one hardware-sequenced read of `length` bytes (1..4) at `address`. */
static int flash_read(u32 address, u8 *out, u32 length)
{
    if (!g_spi || !out || !length || length > 4)
        return -1;
    if (wait_idle() != 0)
        return -1;
    wr(SPIBAR_FADDR, address);
    wr(SPIBAR_FDATA0, 0);
    u32 control = (1u << 16) |          /* FGO */
                  (0u << 17) |          /* read cycle */
                  ((length - 1u) << 19) |
                  (1u << 24);           /* FDIN */
    wr(SPIBAR_HSFSTS, control);
    for (u32 spin = 0; spin < 1000000u; spin++) {
        u32 status = rd(SPIBAR_HSFSTS);
        if (status & HSFSTS_FCERR)
            return -1;
        if (status & HSFSTS_FDONE)
            break;
    }
    u32 value = rd(SPIBAR_FDATA0);
    for (u32 i = 0; i < length; i++)
        out[i] = (u8)(value >> (i * 8));
    return 0;
}

/* Read the JEDEC identification (opcode 0x9F) through the opcode menu. */
int spi_pch_flash_id(u8 id[3])
{
    if (!g_spi)
        return -1;
    *(volatile u8 *)(g_spi + SPIBAR_PREOP) = 0;
    *(volatile u8 *)(g_spi + SPIBAR_OPTYPE) = 0;
    *(volatile u8 *)(g_spi + SPIBAR_OPMENU + 0) = 0x9F;
    for (u32 spin = 0; spin < 1000000u; spin++) {
        u32 value = rd(SPIBAR_HSFSTS);
        if (value & HSFSTS_FCERR)
            return -1;
        if (value & HSFSTS_FDONE)
            break;
    }
    if (flash_read(0, id, 3) != 0)
        return -1;
    for (int i = 0; i < 3; i++)
        g_flash_id[i] = id[i];
    return 0;
}

int spi_pch_init(void)
{
    /* SPI is function 5 of device 31 on the LPC bridge. */
    u32 id = pci_config_read32(0, 31, 5, 0);
    if (id == 0xFFFFFFFFu || id == 0 || (id & 0xFFFF) != 0x8086)
        return -1;
    u32 low = pci_config_read32(0, 31, 5, 0x10);
    if (low & 1)
        return -1;
    u64 bar = low & 0xFFFFFFF0u;
    if (!bar || bar >= 0x100000000ull)
        return -1;
    u32 command = pci_config_read32(0, 31, 5, 4);
    pci_config_write32(0, 31, 5, 4, command | 0x2);
    g_spi = (volatile u8 *)(uintptr_t)bar;

    /* Keep the descriptor region visible and keep the default SCK rate;
     * the region registers remain as firmware left them. */
    u16 ssfc = *(volatile u16 *)(g_spi + SPIBAR_SSFC);
    ssfc &= ~(0x7u << 1);
    ssfc |= (0x2u << 1);
    *(volatile u16 *)(g_spi + SPIBAR_SSFC) = ssfc;

    u8 chip[3];
    if (spi_pch_flash_id(chip) == 0) {
        console_write("[spi ] PCH SPI flash detected\n");
    } else {
        console_write("[spi ] PCH SPI present, flash identification failed\n");
    }
    return 0;
}

const u8 *spi_pch_id(void)
{
    return g_flash_id;
}
