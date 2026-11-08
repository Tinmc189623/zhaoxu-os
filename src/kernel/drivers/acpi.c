#include <acpi.h>
#include <io.h>
#include <kprintf.h>
#include <string.h>
#include <types.h>

/* Physical addresses above the boot identity map are not reachable. */
#define ACPI_IDENTITY_LIMIT 0x100000000ULL

struct acpi_rsdp {
    char signature[8];
    u8   checksum;
    char oem_id[6];
    u8   revision;
    u32  rsdt_address;
    u32  length;
    u64  xsdt_address;
    u8   extended_checksum;
    u8   reserved[3];
} __attribute__((packed));

struct acpi_table_header {
    char signature[4];
    u32  length;
    u8   revision;
    u8   checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32  oem_revision;
    u32  creator_id;
    u32  creator_revision;
} __attribute__((packed));

/* FADT field offsets (ACPI 6.x). */
#define FADT_SMI_COMMAND     48
#define FADT_PM1A_CONTROL    64
#define FADT_PM1B_CONTROL    68
#define FADT_FLAGS           112
#define FADT_RESET_REGISTER  116
#define FADT_RESET_VALUE     128
#define FADT_X_PM1A_CONTROL  172
#define FADT_X_PM1B_CONTROL  184
#define FADT_FLAG_RESET_REG  (1u << 10)

static struct acpi_info g_acpi;

static u16 read16(const u8 *base, u32 offset)
{
    u16 value;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static u32 read32(const u8 *base, u32 offset)
{
    u32 value;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static u64 read64(const u8 *base, u32 offset)
{
    u64 value;
    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static int checksum_ok(const void *base, u32 length)
{
    const u8 *bytes = (const u8 *)base;
    u8 sum = 0;
    for (u32 i = 0; i < length; i++)
        sum = (u8)(sum + bytes[i]);
    return sum == 0;
}

static const char *address_space_name(u8 space)
{
    switch (space) {
    case 0: return "memory";
    case 1: return "I/O";
    case 2: return "PCI config";
    case 3: return "SMBus";
    case 4: return "fixed";
    default: return "unknown";
    }
}

static u32 aml_package_length(const u8 *p, u32 *consumed)
{
    u8 lead = p[0];
    u32 extra = (u32)(lead >> 6);
    if (extra == 0) {
        *consumed = 1;
        return (u32)(lead & 0x3F);
    }
    u32 value = (u32)(lead & 0x0F);
    for (u32 i = 0; i < extra; i++)
        value |= (u32)p[1 + i] << (4 + 8 * i);
    *consumed = 1 + extra;
    return value;
}

static int aml_integer(const u8 *p, u32 *out, u32 *consumed)
{
    switch (p[0]) {
    case 0x00: *out = 0; *consumed = 1; return 1;         /* ZeroOp */
    case 0x01: *out = 1; *consumed = 1; return 1;         /* OneOp */
    case 0xFF: *out = 0xFFFFFFFFu; *consumed = 1; return 1; /* OnesOp */
    case 0x0A: *out = p[1]; *consumed = 2; return 1;      /* BytePrefix */
    case 0x0B: *out = (u32)p[1] | ((u32)p[2] << 8); *consumed = 3; return 1;
    case 0x0C:
        *out = (u32)p[1] | ((u32)p[2] << 8) |
               ((u32)p[3] << 16) | ((u32)p[4] << 24);
        *consumed = 5;
        return 1;
    default:
        return 0;
    }
}

/* Locate Name(_S5, Package(2){ SLP_TYPa, SLP_TYPb }) inside the DSDT. */
static int parse_s5(const u8 *dsdt, u32 length, u8 *slp_typa)
{
    static const u8 prefix[5] = { 0x08, 0x5F, 0x53, 0x35, 0x5F };
    if (!dsdt || length < 16)
        return 0;

    for (u32 i = 0; i + 16 < length; i++) {
        if (memcmp(dsdt + i, prefix, sizeof(prefix)) != 0)
            continue;

        const u8 *p = dsdt + i + sizeof(prefix);
        if (p[0] != 0x12)                       /* PackageOp */
            continue;

        u32 consumed;
        u32 package_length = aml_package_length(p + 1, &consumed);
        if (package_length < 2 || package_length > 1024)
            continue;
        if (i + sizeof(prefix) + 1 + consumed + package_length > length)
            continue;

        const u8 *elements = p + 1 + consumed;
        u32 count = elements[0];
        if (count < 1)
            continue;

        u32 value;
        u32 value_length;
        if (!aml_integer(elements + 1, &value, &value_length))
            continue;
        if (value > 7)
            continue;

        *slp_typa = (u8)value;
        return 1;
    }
    return 0;
}

static const struct acpi_table_header *scan_root_table(u64 root, int is_xsdt)
{
    const struct acpi_table_header *header =
        (const struct acpi_table_header *)(uintptr_t)root;
    const char *expected = is_xsdt ? "XSDT" : "RSDT";

    if (memcmp(header->signature, expected, 4) != 0)
        return NULL;
    if (header->length < sizeof(*header) || header->length > 0x100000)
        return NULL;
    if (!checksum_ok(header, header->length))
        return NULL;

    u32 entry_size = is_xsdt ? 8 : 4;
    u32 count = (header->length - sizeof(*header)) / entry_size;
    const u8 *entries = (const u8 *)header + sizeof(*header);
    const struct acpi_table_header *fadt = NULL;
    u32 table_count = 0;

    for (u32 i = 0; i < count; i++) {
        u64 address = is_xsdt ? read64(entries, i * 8)
                              : (u64)read32(entries, i * 4);
        if (!address || address >= ACPI_IDENTITY_LIMIT)
            continue;

        const struct acpi_table_header *table =
            (const struct acpi_table_header *)(uintptr_t)address;
        if (table->length < sizeof(*table) || table->length > 0x100000)
            continue;
        if (!checksum_ok(table, table->length))
            continue;

        table_count++;
        if (!fadt && memcmp(table->signature, "FACP", 4) == 0)
            fadt = table;
    }

    g_acpi.table_count = table_count;
    return fadt;
}

static void parse_fadt(const struct acpi_table_header *header)
{
    const u8 *fadt = (const u8 *)header;

    g_acpi.fadt_revision = header->revision;
    if (header->length < 116)
        return;

    g_acpi.pm1a_control = read16(fadt, FADT_PM1A_CONTROL);
    g_acpi.pm1b_control = read16(fadt, FADT_PM1B_CONTROL);
    g_acpi.smi_command = read32(fadt, FADT_SMI_COMMAND);

    u32 flags = read32(fadt, FADT_FLAGS);
    if ((flags & FADT_FLAG_RESET_REG) && header->length >= 129) {
        u8 space = fadt[FADT_RESET_REGISTER];
        u64 address = read64(fadt, FADT_RESET_REGISTER + 4);
        if (address) {
            g_acpi.reset_supported = 1;
            g_acpi.reset_address_space = space;
            g_acpi.reset_address = address;
            g_acpi.reset_value = fadt[FADT_RESET_VALUE];
        }
    }

    if (header->length >= 184 && g_acpi.pm1a_control == 0 &&
        fadt[FADT_X_PM1A_CONTROL] == 1) {
        u64 address = read64(fadt, FADT_X_PM1A_CONTROL + 4);
        if (address && address <= 0xFFFF)
            g_acpi.pm1a_control = (u16)address;
    }
    if (header->length >= 196 && g_acpi.pm1b_control == 0 &&
        fadt[FADT_X_PM1B_CONTROL] == 1) {
        u64 address = read64(fadt, FADT_X_PM1B_CONTROL + 4);
        if (address && address <= 0xFFFF)
            g_acpi.pm1b_control = (u16)address;
    }

    /* Locate the DSDT (_S5 sleep type) so SLP_TYPa is correct. */
    if (header->length >= 148) {
        u64 dsdt_address = read64(fadt, 140);   /* X_DSDT */
        if (!dsdt_address || dsdt_address >= ACPI_IDENTITY_LIMIT)
            dsdt_address = read32(fadt, 40);
        if (dsdt_address && dsdt_address < ACPI_IDENTITY_LIMIT) {
            const struct acpi_table_header *dsdt =
                (const struct acpi_table_header *)(uintptr_t)dsdt_address;
            if (memcmp(dsdt->signature, "DSDT", 4) == 0 &&
                dsdt->length >= sizeof(*dsdt) && dsdt->length <= 0x100000) {
                g_acpi.slp_typa_valid = parse_s5((const u8 *)dsdt,
                                                 dsdt->length,
                                                 &g_acpi.slp_typa);
            }
        }
    }
}

void acpi_init(u64 rsdp_physical)
{
    memset(&g_acpi, 0, sizeof(g_acpi));

    if (!rsdp_physical || rsdp_physical >= ACPI_IDENTITY_LIMIT) {
        kprintf("[acpi] RSDP unavailable (0x%llx)\n", rsdp_physical);
        return;
    }

    const struct acpi_rsdp *rsdp =
        (const struct acpi_rsdp *)(uintptr_t)rsdp_physical;

    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
        kprintf("[acpi] bad RSDP signature\n");
        return;
    }
    if (!checksum_ok(rsdp, 20)) {
        kprintf("[acpi] bad RSDP checksum\n");
        return;
    }

    g_acpi.rsdp_revision = rsdp->revision;
    memcpy(g_acpi.oem_id, rsdp->oem_id, 6);
    g_acpi.oem_id[6] = 0;

    u64 root = rsdp->rsdt_address;
    int use_xsdt = 0;

    if (rsdp->revision >= 2) {
        if (rsdp->length >= 36 && rsdp->length <= 4096 &&
            checksum_ok(rsdp, rsdp->length)) {
            if (rsdp->xsdt_address) {
                root = rsdp->xsdt_address;
                use_xsdt = 1;
            }
        } else {
            kprintf("[acpi] bad RSDP extended checksum, falling back to RSDT\n");
        }
    }

    if (!root || root >= ACPI_IDENTITY_LIMIT) {
        kprintf("[acpi] no usable root table\n");
        return;
    }

    const struct acpi_table_header *fadt = scan_root_table(root, use_xsdt);
    g_acpi.present = 1;

    if (!fadt) {
        kprintf("[acpi] RSDP v%u, OEM %s, %s, %u tables, no FADT\n",
                g_acpi.rsdp_revision, g_acpi.oem_id,
                use_xsdt ? "XSDT" : "RSDT", g_acpi.table_count);
        return;
    }

    parse_fadt(fadt);

    kprintf("[acpi] RSDP v%u, OEM %s, %s, %u tables, FADT rev %u\n",
            g_acpi.rsdp_revision, g_acpi.oem_id,
            use_xsdt ? "XSDT" : "RSDT", g_acpi.table_count,
            g_acpi.fadt_revision);
    kprintf("[acpi] PM1a_CNT 0x%x, PM1b_CNT 0x%x, SMI 0x%x, S5 type %s%u\n",
            g_acpi.pm1a_control, g_acpi.pm1b_control, g_acpi.smi_command,
            g_acpi.slp_typa_valid ? "" : "?",
            g_acpi.slp_typa);
    if (g_acpi.reset_supported) {
        kprintf("[acpi] reset via %s at 0x%llx, value 0x%x\n",
                address_space_name(g_acpi.reset_address_space),
                g_acpi.reset_address, g_acpi.reset_value);
    } else {
        kprintf("[acpi] no FADT reset register, using legacy fallback\n");
    }
}

const struct acpi_info *acpi_get_info(void)
{
    return &g_acpi;
}

static void spin_delay(u32 iterations)
{
    for (volatile u32 i = 0; i < iterations; i++)
        __asm__ volatile("pause");
}

static void write_sleep_state(u8 type)
{
    u16 value = (u16)(((u16)(type & 7) << 10) | (1u << 13));
    if (g_acpi.pm1a_control) {
        outw(g_acpi.pm1a_control, value);
        spin_delay(50000);
    }
    if (g_acpi.pm1b_control) {
        outw(g_acpi.pm1b_control, value);
        spin_delay(50000);
    }
}

void acpi_poweroff(void)
{
    cli();

    u8 attempted[4];
    u32 count = 0;

    if (g_acpi.slp_typa_valid) {
        attempted[count++] = g_acpi.slp_typa;
    } else {
        attempted[count++] = 5;
    }
    /* Secondary candidates: 5 covers most firmware, 0 covers QEMU/Boards. */
    static const u8 fallback[] = { 5, 0 };
    for (u32 i = 0; i < sizeof(fallback); i++) {
        int seen = 0;
        for (u32 j = 0; j < count; j++)
            if (attempted[j] == fallback[i])
                seen = 1;
        if (!seen && count < sizeof(attempted))
            attempted[count++] = fallback[i];
    }

    for (u32 i = 0; i < count; i++)
        write_sleep_state(attempted[i]);

    /* QEMU/Bochs fixed ACPI ports (q35 PM base, i440fx PM base). */
    outw(0x604, 0x2000);
    spin_delay(50000);
    outw(0xB004, 0x2000);
    spin_delay(50000);

    kprintf("ACPI power off was not accepted; halted.\n");
    for (;;)
        hlt();
}

void acpi_reboot(void)
{
    cli();

    if (g_acpi.reset_supported) {
        if (g_acpi.reset_address_space == 1 && g_acpi.reset_address <= 0xFFFF) {
            outb((u16)g_acpi.reset_address, g_acpi.reset_value);
            spin_delay(100000);
        } else if (g_acpi.reset_address_space == 0) {
            *(volatile u8 *)(uintptr_t)g_acpi.reset_address = g_acpi.reset_value;
            spin_delay(100000);
        }
    }

    /* PCI reset control register. */
    outb(0xCF9, 0x02);
    io_wait();
    spin_delay(10000);
    outb(0xCF9, 0x06);
    spin_delay(100000);

    /* Keyboard controller pulse reset line. */
    for (u32 i = 0; i < 1000 && (inb(0x64) & 0x02); i++)
        io_wait();
    outb(0x64, 0xFE);
    spin_delay(100000);

    /* Last resort: triple fault. */
    struct {
        u16 limit;
        u64 base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile("lidt %0" :: "m"(null_idt));
    __asm__ volatile("int $0x03");

    for (;;)
        hlt();
}
