/*
 * Zhaoxu OS - C glue for the SPARK/Ada ACPI driver.
 *
 * Provides logging, CPU control and the triple-fault path; all ACPI table
 * parsing and power state selection lives in drivers/acpi.adb.
 */
#include <io.h>
#include <kprintf.h>
#include <types.h>

void acpi_cpu_cli(void) { cli(); }
void acpi_cpu_halt(void) { hlt(); }

void acpi_spin(u32 iterations)
{
    for (volatile u32 i = 0; i < iterations; i++)
        __asm__ volatile("pause");
}

void acpi_triple_fault(void)
{
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) null_idt = { 0, 0 };

    __asm__ volatile("lidt %0" :: "m"(null_idt));
    __asm__ volatile("int $0x03");
    for (;;)
        hlt();
}

void acpi_log_fail(const char *reason, u64 value)
{
    kprintf("[acpi] %s (0x%llx)\n", reason, value);
}

void acpi_log_tables(u32 revision, const char *oem, u32 tables, u32 use_xsdt)
{
    kprintf("[acpi] RSDP v%u, OEM %s, %s, %u tables", revision, oem,
            use_xsdt ? "XSDT" : "RSDT", tables);
}

void acpi_log_fadt(u32 revision, u32 pm1a, u32 pm1b, u32 smi,
                   u32 slp_valid, u32 slp)
{
    kprintf(", FADT rev %u\n", revision);
    kprintf("[acpi] PM1a_CNT 0x%x, PM1b_CNT 0x%x, SMI 0x%x, S5 type %s%u\n",
            pm1a, pm1b, smi, slp_valid ? "" : "?", slp);
}

void acpi_log_no_fadt(void)
{
    kprintf(", no FADT\n");
}

void acpi_log_reset(u32 space, u64 address, u32 value)
{
    static const char *const names[] = {
        "memory", "I/O", "PCI config", "SMBus", "fixed"
    };
    const char *name = space < 5 ? names[space] : "unknown";
    kprintf("[acpi] reset via %s at 0x%llx, value 0x%x\n", name, address,
            value);
}

void acpi_log_fallback(void)
{
    kprintf("[acpi] no FADT reset register, using legacy fallback\n");
}

void acpi_log_halted(void)
{
    kprintf("ACPI power off was not accepted; halted.\n");
}

void acpi_log_ext_checksum(void)
{
    kprintf("[acpi] bad RSDP extended checksum, XSDT checksum still governs\n");
}
