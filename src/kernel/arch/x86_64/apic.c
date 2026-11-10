/*
 * Zhaoxu OS - ACPI MADT discovery and xAPIC driver.
 *
 * Only what the SMP bring-up needs: enumerate enabled CPUs, program the
 * local APIC, calibrate its timer against the PIT and send INIT/SIPI IPIs.
 * Legacy PIC interrupts stay on the BSP (LINT0 is configured as ExtINT).
 */
#include <apic.h>
#include <io.h>
#include <kprintf.h>
#include <percpu.h>
#include <spinlock.h>
#include <string.h>
#include <timer.h>
#include <types.h>

#define LAPIC_DEFAULT_BASE 0xFEE00000ULL

#define LAPIC_REG_ID        0x020
#define LAPIC_REG_TPR       0x080
#define LAPIC_REG_EOI       0x0B0
#define LAPIC_REG_SVR       0x0F0
#define LAPIC_REG_ICR_LOW   0x300
#define LAPIC_REG_ICR_HIGH  0x310
#define LAPIC_REG_LVT_TIMER 0x320
#define LAPIC_REG_LVT_LINT0 0x350
#define LAPIC_REG_LVT_LINT1 0x360
#define LAPIC_REG_LVT_ERROR 0x370
#define LAPIC_REG_TIMER_INIT  0x380
#define LAPIC_REG_TIMER_CUR   0x390
#define LAPIC_REG_TIMER_DIV   0x3E0

#define LVT_MASKED 0x10000u

struct madt_cpu {
    u32 apic_id;
    u16 acpi_id;
    u16 flags;
};

static volatile u32 *lapic;
static u64 lapic_physical = LAPIC_DEFAULT_BASE;
static u64 rsdp_physical;
static u32 lapic_timer_hz;
static u32 pm_timer_port;
static struct madt_cpu cpu_table[MAX_CPUS];
static u32 cpu_table_count;

static u32 lapic_read(u32 reg)
{
    return lapic[reg / 4];
}

static void lapic_write(u32 reg, u32 value)
{
    lapic[reg / 4] = value;
    __asm__ volatile("" ::: "memory");
}

static int acpi_checksum(const u8 *table, u32 length)
{
    u8 sum = 0;
    for (u32 i = 0; i < length; i++)
        sum = (u8)(sum + table[i]);
    return sum == 0;
}

/*
 * Some firmware (notably SeaBIOS on q35) keeps the RSDP outside the ranges
 * the legacy bootloader scans.  Walk the first MiB and return the first
 * signature with a valid checksum.
 */
u64 apic_locate_rsdp(u64 candidate)
{
    if (candidate) {
        const u8 *table = (const u8 *)(uintptr_t)candidate;
        if (memcmp(table, "RSD PTR ", 8) == 0 && acpi_checksum(table, 20))
            return candidate;
    }

    for (u64 address = 0; address + 36 <= 0x100000; address++) {
        const u8 *table = (const u8 *)(uintptr_t)address;
        if (table[0] != 'R' || memcmp(table, "RSD PTR ", 8) != 0)
            continue;
        if (acpi_checksum(table, 20))
            return address;
    }
    return 0;
}

static const u8 *acpi_find_table(const char signature[4], u32 *length_out)
{
    const u8 *rsdp;
    const u8 *root = NULL;
    u32 entry_size = 0;
    u32 root_length;

    if (!rsdp_physical)
        return NULL;
    rsdp = (const u8 *)(uintptr_t)rsdp_physical;
    if (memcmp(rsdp, "RSD PTR ", 8) != 0 || !acpi_checksum(rsdp, 20))
        return NULL;

    if (rsdp[15] >= 2) {
        u32 length = *(const u32 *)(rsdp + 20);
        u64 xsdt = *(const u64 *)(rsdp + 24);
        if (length >= 36 && xsdt && acpi_checksum(rsdp, length)) {
            root = (const u8 *)(uintptr_t)xsdt;
            if (memcmp(root, "XSDT", 4) == 0)
                entry_size = 8;
            else
                root = NULL;
        }
    }
    if (!root) {
        u32 rsdt = *(const u32 *)(rsdp + 16);
        if (!rsdt)
            return NULL;
        root = (const u8 *)(uintptr_t)rsdt;
        if (memcmp(root, "RSDT", 4) != 0)
            return NULL;
        entry_size = 4;
    }

    root_length = *(const u32 *)(root + 4);
    if (root_length < 36 || root_length > 0x100000)
        return NULL;
    if (!acpi_checksum(root, root_length))
        return NULL;

    for (u32 offset = 36; offset + entry_size <= root_length;
         offset += entry_size) {
        u64 address = entry_size == 8
                          ? *(const u64 *)(root + offset)
                          : (u64)*(const u32 *)(root + offset);
        const u8 *table = (const u8 *)(uintptr_t)address;
        if (!address || !table)
            continue;
        if (memcmp(table, signature, 4) != 0)
            continue;
        u32 length = *(const u32 *)(table + 4);
        if (length < 8 || length > 0x100000)
            continue;
        if (!acpi_checksum(table, length))
            continue;
        if (length_out)
            *length_out = length;
        return table;
    }
    return NULL;
}

static void madt_parse(void)
{
    u32 length = 0;
    const u8 *madt = acpi_find_table("APIC", &length);

    if (!madt || length < 44)
        return;

    u32 base = *(const u32 *)(madt + 36);
    if (base)
        lapic_physical = base;

    u32 offset = 44;
    while (offset + 2 <= length) {
        u8 type = madt[offset];
        u8 entry_length = madt[offset + 1];
        if (entry_length < 2 || offset + entry_length > length)
            break;

        if (type == 0 && entry_length >= 8) {
            u32 flags = *(const u32 *)(madt + offset + 4);
            if ((flags & 1u) && cpu_table_count < MAX_CPUS) {
                cpu_table[cpu_table_count].apic_id = madt[offset + 3];
                cpu_table[cpu_table_count].acpi_id = madt[offset + 2];
                cpu_table[cpu_table_count].flags = (u16)flags;
                cpu_table_count++;
            }
        } else if (type == 5 && entry_length >= 12) {
            u64 address = *(const u64 *)(madt + offset + 4);
            if (address)
                lapic_physical = address;
        }
        offset += entry_length;
    }
}

static void place_bsp_first(u32 bsp_apic_id)
{
    u32 position = cpu_table_count;
    for (u32 i = 0; i < cpu_table_count; i++) {
        if (cpu_table[i].apic_id == bsp_apic_id) {
            position = i;
            break;
        }
    }

    if (position == cpu_table_count) {
        if (cpu_table_count >= MAX_CPUS)
            return;
        cpu_table[cpu_table_count].apic_id = bsp_apic_id;
        cpu_table[cpu_table_count].acpi_id = 0;
        cpu_table[cpu_table_count].flags = 1;
        cpu_table_count++;
        position = cpu_table_count - 1;
    }

    struct madt_cpu saved = cpu_table[0];
    cpu_table[0] = cpu_table[position];
    cpu_table[position] = saved;
}

static void apic_pm_timer_locate(void)
{
    u32 length = 0;
    const u8 *fadt = acpi_find_table("FACP", &length);
    if (!fadt || length < 80)
        return;
    pm_timer_port = (u32)fadt[76] | ((u32)fadt[77] << 8) |
                    ((u32)fadt[78] << 16) | ((u32)fadt[79] << 24);
}

/* One-shot PIT channel-2 wait used only to calibrate the LAPIC timer.  The
 * PIT is not a kernel driver; it is core timing hardware used once at boot. */
static void pit_wait_10ms(void)
{
    u8 control = inb(0x61) & 0xFC;
    outb(0x61, (u8)(control | 1));
    outb(0x43, 0xB0);
    outb(0x42, 11932 & 0xFF);
    outb(0x42, (11932 >> 8) & 0xFF);
    for (u64 spin = 0; spin < 100000000ULL; spin++) {
        if (inb(0x61) & 0x20)
            break;
    }
    outb(0x61, (u8)(inb(0x61) & 0xFE));
}

void apic_delay_ms(u32 ms)
{
    if (!pm_timer_port) {
        for (volatile u64 spin = 0; spin < (u64)ms * 1000000ULL; spin++)
            cpu_relax();
        return;
    }
    u64 ticks = ((u64)ms * 3579545ULL) / 1000ULL;
    u32 start = inl(pm_timer_port) & 0xFFFFFFu;
    for (;;) {
        u32 now = inl(pm_timer_port) & 0xFFFFFFu;
        u32 elapsed = (now - start) & 0xFFFFFFu;
        if (elapsed >= ticks)
            return;
        cpu_relax();
    }
}

void apic_init(u64 rsdp)
{
    apic_pm_timer_locate();
    rsdp_physical = apic_locate_rsdp(rsdp);
    lapic = (volatile u32 *)(uintptr_t)LAPIC_DEFAULT_BASE;

    u32 bsp_apic_id = lapic_read(LAPIC_REG_ID) >> 24;

    madt_parse();
    place_bsp_first(bsp_apic_id);
    if (cpu_table_count == 0) {
        cpu_table[0].apic_id = bsp_apic_id;
        cpu_table[0].acpi_id = 0;
        cpu_table[0].flags = 1;
        cpu_table_count = 1;
    }

    lapic = (volatile u32 *)(uintptr_t)lapic_physical;

    percpu_init_bsp(cpu_table[0].apic_id);
    for (u32 i = 1; i < cpu_table_count; i++)
        percpu_register(i, cpu_table[i].apic_id);
    g_cpu_count = cpu_table_count;

    lapic_enable_bsp();

    kprintf("[smp ] MADT: %u CPU(s), LAPIC 0x%llx, bsp apic %u, rsdp 0x%llx\n",
            cpu_table_count, lapic_physical, bsp_apic_id, rsdp_physical);
    for (u32 i = 0; i < cpu_table_count; i++)
        kprintf("[smp ] cpu%u apic %u%s\n", i, cpu_table[i].apic_id,
                i == 0 ? " (BSP)" : "");
}

u32 apic_cpu_count(void)
{
    return cpu_table_count;
}

u32 apic_cpu_apic_id(u32 index)
{
    return index < cpu_table_count ? cpu_table[index].apic_id : 0;
}

u16 apic_cpu_acpi_id(u32 index)
{
    return index < cpu_table_count ? cpu_table[index].acpi_id : 0;
}

u64 apic_base(void)
{
    return lapic_physical;
}

void lapic_enable_bsp(void)
{
    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_SVR, 0x1FF);        /* enable, spurious 0xFF */
    lapic_write(LAPIC_REG_LVT_LINT0, 0x700);  /* ExtINT: legacy PIC */
    lapic_write(LAPIC_REG_LVT_LINT1, 0x400);  /* NMI */
    lapic_write(LAPIC_REG_LVT_ERROR, LVT_MASKED | APIC_VECTOR_SPURIOUS);
    lapic_write(LAPIC_REG_LVT_TIMER, LVT_MASKED);
}

void lapic_enable_ap(void)
{
    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_SVR, 0x1FF);
    lapic_write(LAPIC_REG_LVT_LINT0, LVT_MASKED); /* PIC stays on the BSP */
    lapic_write(LAPIC_REG_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_REG_LVT_ERROR, LVT_MASKED | APIC_VECTOR_SPURIOUS);
}

void lapic_eoi(void)
{
    lapic_write(LAPIC_REG_EOI, 0);
}

static void lapic_wait_delivery(void)
{
    for (u32 spin = 0; spin < 1000000; spin++) {
        if (!(lapic_read(LAPIC_REG_ICR_LOW) & (1u << 12)))
            return;
        cpu_relax();
    }
}

void apic_send_ipi(u32 apic_id, u32 icr_low)
{
    lapic_wait_delivery();
    lapic_write(LAPIC_REG_ICR_HIGH, (apic_id & 0xFF) << 24);
    lapic_write(LAPIC_REG_ICR_LOW, icr_low);
    lapic_wait_delivery();
}

void apic_start_cpu(u32 apic_id, u32 vector)
{
    /* INIT assert, deassert, then two STARTUP IPIs with the trampoline page. */
    apic_send_ipi(apic_id, 0x00004500u);
    apic_delay_ms(10);
    apic_send_ipi(apic_id, 0x00008500u);
    apic_delay_ms(1);
    apic_send_ipi(apic_id, 0x00004600u | (vector & 0xFFu));
    apic_delay_ms(1);
    apic_send_ipi(apic_id, 0x00004600u | (vector & 0xFFu));
}

u32 apic_calibrate_lapic(void)
{
    lapic_write(LAPIC_REG_TIMER_DIV, 0x03);   /* divide by 16 */
    lapic_write(LAPIC_REG_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);

    pit_wait_10ms();

    u32 remaining = lapic_read(LAPIC_REG_TIMER_CUR);
    lapic_write(LAPIC_REG_LVT_TIMER, LVT_MASKED);

    u32 elapsed = 0xFFFFFFFFu - remaining;
    lapic_timer_hz = elapsed * 100u;
    if (lapic_timer_hz < 10000u)
        lapic_timer_hz = 0;
    return lapic_timer_hz;
}

u32 apic_lapic_hz(void)
{
    return lapic_timer_hz;
}

void apic_timer_start(u32 vector, u32 hz)
{
    u32 count = 0;

    if (!lapic_timer_hz || !hz)
        return;
    count = lapic_timer_hz / hz;
    if (count < 16)
        count = 16;

    lapic_write(LAPIC_REG_TIMER_DIV, 0x03);
    lapic_write(LAPIC_REG_LVT_TIMER, 0x20000u | (vector & 0xFFu));
    lapic_write(LAPIC_REG_TIMER_INIT, count);
}

void apic_timer_stop(void)
{
    lapic_write(LAPIC_REG_LVT_TIMER, LVT_MASKED);
}
