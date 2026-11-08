#include <gdt.h>
#include <percpu.h>
#include <string.h>
#include <types.h>

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

struct tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

#define GDT_BASE_ENTRIES 5
#define GDT_ENTRY_COUNT  (GDT_BASE_ENTRIES + 2 * MAX_CPUS)

static u64 gdt[GDT_ENTRY_COUNT];
static struct tss tss_table[MAX_CPUS];
static u8 double_fault_stacks[MAX_CPUS][8192] __attribute__((aligned(16)));
static u8 nmi_stacks[MAX_CPUS][8192] __attribute__((aligned(16)));

extern void gdt_flush(const struct gdt_ptr *ptr);
extern void gdt_load_ap(const struct gdt_ptr *ptr, u16 tss_selector);
extern void tss_flush(void);

static u64 make_descriptor(u32 base, u32 limit, u8 access, u8 flags)
{
    u64 desc = 0;
    desc |= (u64)(limit & 0xFFFFu);
    desc |= (u64)(base & 0xFFFFu) << 16;
    desc |= (u64)((base >> 16) & 0xFFu) << 32;
    desc |= (u64)access << 40;
    desc |= (u64)(((limit >> 16) & 0xFu) | (flags & 0xF0u)) << 48;
    desc |= (u64)((base >> 24) & 0xFFu) << 56;
    return desc;
}

static u16 tss_selector(u32 cpu)
{
    return (u16)((GDT_BASE_ENTRIES + 2 * cpu) * 8);
}

static void build_tss_entry(u32 cpu)
{
    struct tss *state = &tss_table[cpu];
    memset(state, 0, sizeof(*state));
    state->ist[0] = (u64)double_fault_stacks[cpu] +
                    sizeof(double_fault_stacks[cpu]);
    state->ist[1] = (u64)nmi_stacks[cpu] + sizeof(nmi_stacks[cpu]);
    state->iomap_base = (u16)sizeof(*state);

    u64 base = (u64)state;
    u32 index = GDT_BASE_ENTRIES + 2 * cpu;
    gdt[index] = make_descriptor((u32)base, sizeof(*state) - 1, 0x89, 0x00);
    gdt[index + 1] = base >> 32;
}

static void build_gdt(void)
{
    memset(gdt, 0, sizeof(gdt));
    gdt[0] = 0;
    gdt[1] = make_descriptor(0, 0xFFFFF, 0x9A, 0xA0); /* kernel code, L=1 */
    gdt[2] = make_descriptor(0, 0xFFFFF, 0x92, 0xC0); /* kernel data */
    gdt[3] = make_descriptor(0, 0xFFFFF, 0xFA, 0xA0); /* user code, L=1 */
    gdt[4] = make_descriptor(0, 0xFFFFF, 0xF2, 0xC0); /* user data */

    for (u32 cpu = 0; cpu < MAX_CPUS; cpu++)
        build_tss_entry(cpu);
}

void tss_set_rsp0_cpu(u32 cpu, u64 rsp0)
{
    if (cpu >= MAX_CPUS)
        return;
    tss_table[cpu].rsp0 = rsp0;
}

void tss_set_rsp0(u64 rsp0)
{
    tss_set_rsp0_cpu(cpu_index(), rsp0);
}

void gdt_init(void)
{
    struct gdt_ptr ptr;

    build_gdt();

    ptr.limit = sizeof(gdt) - 1;
    ptr.base = (u64)gdt;
    gdt_flush(&ptr);
    tss_flush();
}

void gdt_ap_init(u32 cpu)
{
    struct gdt_ptr ptr;

    if (cpu >= MAX_CPUS)
        return;

    ptr.limit = sizeof(gdt) - 1;
    ptr.base = (u64)gdt;
    gdt_load_ap(&ptr, tss_selector(cpu));
}
