/*
 * Zhaoxu OS - ACPI HPET driver.
 *
 * The HPET is a platform timer, so it stays in the kernel as part of the
 * timekeeping core rather than in the Ring3 device service.  The driver
 * parses XSDT/RSDT for the HPET description table, maps the MMIO block
 * through the existing identity map and exposes a monotonic 64-bit counter
 * plus a busy-wait delay.
 */
#include <boot.h>
#include <hpet.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define HPET_REG_CAP 0x000
#define HPET_REG_PERIOD 0x004
#define HPET_REG_CONFIG 0x010
#define HPET_REG_COUNTER 0x0F0
#define HPET_CONFIG_ENABLE 0x0001

static volatile u8 *g_hpet;
static u64 g_hz;

static u32 read_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u64 read_le64(const u8 *p)
{
    return (u64)read_le32(p) | ((u64)read_le32(p + 4) << 32);
}

static u64 hpet_read64(u32 offset)
{
    return *(volatile u64 *)(g_hpet + offset);
}

static void hpet_write64(u32 offset, u64 value)
{
    *(volatile u64 *)(g_hpet + offset) = value;
}

/* Scan a RSDT/XSDT for the HPET description table and return its base
 * address, or zero when the firmware does not describe a timer. */
static u64 hpet_find_base(void)
{
    const struct boot_info *boot = boot_get_info();
    if (!boot || !boot->rsdp)
        return 0;
    const u8 *rsdp = (const u8 *)(uintptr_t)boot->rsdp;
    if (memcmp(rsdp, "RSD PTR ", 8) != 0)
        return 0;

    u8 revision = rsdp[15];
    u64 table = 0;
    u32 entry_size = 4;
    if (revision >= 2) {
        table = read_le64(rsdp + 24);
        entry_size = 8;
    } else {
        table = read_le32(rsdp + 16);
    }
    if (!table || table >= 0x1000000000ull)
        return 0;

    const u8 *sdt = (const u8 *)(uintptr_t)table;
    if (memcmp(sdt, "XSDT", 4) != 0 && memcmp(sdt, "RSDT", 4) != 0)
        return 0;
    u32 length = read_le32(sdt + 4);
    if (length < 36 || length > 1024 * 1024)
        return 0;
    u32 entries = (length - 36) / entry_size;
    for (u32 i = 0; i < entries; i++) {
        u64 address;
        if (entry_size == 8)
            address = read_le64(sdt + 36 + i * 8);
        else
            address = read_le32(sdt + 36 + i * 4);
        if (!address || address >= 0x1000000000ull)
            continue;
        const u8 *table_header = (const u8 *)(uintptr_t)address;
        if (memcmp(table_header, "HPET", 4) != 0)
            continue;
        u32 table_length = read_le32(table_header + 4);
        if (table_length < 56)
            continue;
        /* Generic address structure at offset 40; the 64-bit address is at
         * offset 44. */
        u64 base = read_le64(table_header + 44);
        if (base)
            return base;
    }
    return 0;
}

int hpet_init(void)
{
    u64 base = hpet_find_base();
    if (!base) {
        kprintf("[hpet] not described by ACPI\n");
        return -1;
    }
    if (base >= 0x1000000000ull) {
        kprintf("[hpet] MMIO 0x%llx is outside the identity window\n", base);
        return -1;
    }
    g_hpet = (volatile u8 *)(uintptr_t)base;
    u64 capabilities = hpet_read64(HPET_REG_CAP);
    u32 period = (u32)hpet_read64(HPET_REG_PERIOD);
    if (!period) {
        g_hpet = NULL;
        return -1;
    }
    g_hz = 1000000000000000ull / period;

    u64 config = hpet_read64(HPET_REG_CONFIG);
    hpet_write64(HPET_REG_CONFIG, config | HPET_CONFIG_ENABLE);
    kprintf("[hpet] %u timer(s), %llu Hz, counter %s, at 0x%llx\n",
            (u32)((capabilities >> 8) & 0x1F) + 1, g_hz,
            (capabilities & (1ull << 13)) ? "64-bit" : "32-bit", base);
    return 0;
}

int hpet_present(void)
{
    return g_hpet != NULL;
}

u64 hpet_hz(void)
{
    return g_hz;
}

u64 hpet_counter(void)
{
    if (!g_hpet)
        return 0;
    return hpet_read64(HPET_REG_COUNTER);
}

void hpet_delay_us(u64 microseconds)
{
    if (!g_hpet || !g_hz) {
        return;
    }
    u64 ticks = (microseconds * g_hz) / 1000000ull;
    u64 start = hpet_counter();
    while (hpet_counter() - start < ticks)
        __asm__ volatile("pause");
}
