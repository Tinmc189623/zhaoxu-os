/*
 * Zhaoxu OS - board temperature sensor (LM75/TMP102 class).
 *
 * Probes the common 0x48..0x4F addresses on the DesignWare I2C bus and
 * publishes milli-degrees Celsius in the shared device header, where sysfs
 * exposes it as thermal_zone0/temp.
 */
#include <dev.h>
#include <types.h>

extern int i2c_dw_present(void);
extern int i2c_dw_write(u8 address, const u8 *data, u32 length);
extern int i2c_dw_read(u8 address, u8 *data, u32 length);

static int g_address = -1;
static int g_sensor_kind; /* 0 none, 1 LM75 (9-bit), 2 TMP102 (12-bit) */
static u32 g_last_poll;

static int read_register(int address, u8 reg, u8 *data, u32 length)
{
    if (i2c_dw_write((u8)address, &reg, 1) != 0)
        return -1;
    return i2c_dw_read((u8)address, data, length);
}

/* Decode a two-byte sample according to the detected sensor layout. */
static int decode_temperature(const u8 raw[2], int kind)
{
    if (kind == 1) {
        i16 value = (i16)(((u16)raw[0] << 8) | raw[1]);
        return (int)(value >> 7) * 500;
    }
    i16 value = (i16)(((u16)raw[0] << 8) | raw[1]);
    return (int)(value >> 4) * 625 / 10;
}

int thermal_init(void)
{
    if (!i2c_dw_present())
        return -1;
    for (int address = 0x48; address <= 0x4F; address++) {
        u8 data[2];
        if (read_register(address, 0x00, data, 2) != 0)
            continue;
        int lm75 = decode_temperature(data, 1);
        int tmp102 = decode_temperature(data, 2);
        int chosen = 0;
        if (lm75 > -55000 && lm75 < 150000)
            chosen = 1;
        else if (tmp102 > -55000 && tmp102 < 150000)
            chosen = 2;
        if (!chosen)
            continue;
        g_address = address;
        g_sensor_kind = chosen;
        return 0;
    }
    return -1;
}

/* Sample the sensor and publish the result into the shared header. */
void thermal_poll(struct dev_header *header)
{
    if (!header || g_address < 0)
        return;
    if (++g_last_poll % 512 != 0)
        return;
    u8 data[2];
    if (read_register(g_address, 0x00, data, 2) != 0)
        return;
    int milli = decode_temperature(data, g_sensor_kind);
    if (milli < -55000 || milli > 150000)
        return;
    header->sensor_temp_milli = (u32)(i32)milli;
    header->sensor_ready = 1;
}
