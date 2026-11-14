/*
 * Zhaoxu OS - Synopsys DesignWare I2C controller.
 *
 * The Intel LPSS I2C blocks use this register layout.  The driver polls the
 * FIFOs, which is sufficient for the small transactions used by the thermal
 * sensor and by platform management, and exposes blocking write/read calls
 * for other Ring3 drivers.
 */
#include <dev.h>
#include <io.h>
#include <types.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);
extern void console_write(const char *text);

#define IC_CON 0x00
#define IC_TAR 0x04
#define IC_DATA_CMD 0x10
#define IC_SS_SCL_HCNT 0x14
#define IC_SS_SCL_LCNT 0x18
#define IC_FS_SCL_HCNT 0x1C
#define IC_FS_SCL_LCNT 0x20
#define IC_INTR_MASK 0x30
#define IC_RAW_INTR_STAT 0x34
#define IC_RX_TL 0x38
#define IC_TX_TL 0x3C
#define IC_ENABLE 0x6C
#define IC_STATUS 0x70
#define IC_TXFLR 0x74
#define IC_RXFLR 0x78
#define IC_TX_ABRT_SOURCE 0x80
#define IC_ENABLE_STATUS 0x9C

#define IC_STATUS_TFE 0x04
#define IC_STATUS_RFNE 0x08
#define IC_STATUS_MST_ACTIVITY 0x20
#define IC_DATA_CMD_READ 0x0100
#define IC_DATA_CMD_STOP 0x0200
#define IC_ABRT_ALL 0xFFFF

static volatile u8 *g_i2c;

static u32 rd(u32 offset) { return *(volatile u32 *)(g_i2c + offset); }
static void wr(u32 offset, u32 value)
{
    *(volatile u32 *)(g_i2c + offset) = value;
}

int i2c_dw_present(void)
{
    return g_i2c != NULL;
}

int i2c_dw_init(void)
{
    u64 bar = 0;
    for (u32 bus = 0; bus < 256 && !bar; bus++) {
        for (u32 slot = 0; slot < 32 && !bar; slot++) {
            for (u32 function = 0; function < 8 && !bar; function++) {
                u32 id = pci_config_read32(bus, slot, function, 0);
                if (id == 0xFFFFFFFFu || id == 0)
                    continue;
                u32 class = pci_config_read32(bus, slot, function, 8) >> 8;
                if (class != 0x0C8000u)
                    continue;
                u32 low = pci_config_read32(bus, slot, function, 0x10);
                if (low & 1)
                    continue;
                bar = low & 0xFFFFFFF0u;
                if (!bar || bar >= 0x100000000ull) {
                    bar = 0;
                    continue;
                }
                u32 command = pci_config_read32(bus, slot, function, 4);
                pci_config_write32(bus, slot, function, 4,
                                   command | 0x6);
            }
        }
    }
    if (!bar)
        return -1;

    g_i2c = (volatile u8 *)(uintptr_t)bar;
    wr(IC_ENABLE, 0);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((rd(IC_ENABLE_STATUS) & 1) == 0)
            break;
    /* Master, fast mode, restart enabled. */
    wr(IC_CON, 0x65);
    wr(IC_SS_SCL_HCNT, 400);
    wr(IC_SS_SCL_LCNT, 470);
    wr(IC_FS_SCL_HCNT, 60);
    wr(IC_FS_SCL_LCNT, 130);
    wr(IC_RX_TL, 0);
    wr(IC_TX_TL, 0);
    wr(IC_INTR_MASK, 0);
    wr(IC_ENABLE, 1);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if (rd(IC_ENABLE_STATUS) & 1)
            break;
    console_write("[i2c ] DesignWare controller ready\n");
    return 0;
}

/* Write a byte sequence to a 7-bit address. */
int i2c_dw_write(u8 address, const u8 *data, u32 length)
{
    if (!g_i2c || !data || !length)
        return -1;
    wr(IC_TX_ABRT_SOURCE, IC_ABRT_ALL);
    wr(IC_TAR, address & 0x7F);
    for (u32 i = 0; i < length; i++) {
        u32 command = data[i];
        if (i + 1 == length)
            command |= IC_DATA_CMD_STOP;
        wr(IC_DATA_CMD, command);
        for (u32 spin = 0; spin < 1000000u; spin++) {
            if (rd(IC_RAW_INTR_STAT) & 0x01) /* TX_ABRT */
                return -1;
            if (rd(IC_TXFLR) == 0)
                break;
        }
    }
    for (u32 spin = 0; spin < 1000000u; spin++) {
        if (rd(IC_RAW_INTR_STAT) & 0x01)
            return -1;
        if ((rd(IC_STATUS) & (IC_STATUS_TFE | IC_STATUS_MST_ACTIVITY)) ==
            IC_STATUS_TFE)
            return 0;
    }
    return -1;
}

/* Read a byte sequence from a 7-bit address. */
int i2c_dw_read(u8 address, u8 *data, u32 length)
{
    if (!g_i2c || !data || !length)
        return -1;
    wr(IC_TX_ABRT_SOURCE, IC_ABRT_ALL);
    wr(IC_TAR, address & 0x7F);
    for (u32 i = 0; i < length; i++) {
        u32 command = IC_DATA_CMD_READ;
        if (i + 1 == length)
            command |= IC_DATA_CMD_STOP;
        wr(IC_DATA_CMD, command);
    }
    for (u32 i = 0; i < length; i++) {
        u32 spin;
        for (spin = 0; spin < 1000000u; spin++) {
            if (rd(IC_RAW_INTR_STAT) & 0x01)
                return -1;
            if (rd(IC_STATUS) & IC_STATUS_RFNE)
                break;
        }
        if (spin >= 1000000u)
            return -1;
        data[i] = (u8)rd(IC_DATA_CMD);
    }
    return 0;
}
