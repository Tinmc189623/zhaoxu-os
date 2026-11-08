#include <gdt.h>
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

static u64 gdt[7];
static struct tss tss;
static u8 double_fault_stack[8192] __attribute__((aligned(16)));
static u8 nmi_stack[8192] __attribute__((aligned(16)));

extern void gdt_flush(const struct gdt_ptr *ptr);
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

void tss_set_rsp0(u64 rsp0)
{
    tss.rsp0 = rsp0;
}

void gdt_init(void)
{
    struct gdt_ptr ptr;

    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = 0xffffffff80f00000ULL;
    tss.ist[0] = (u64)double_fault_stack + sizeof(double_fault_stack);
    tss.ist[1] = (u64)nmi_stack + sizeof(nmi_stack);
    tss.iomap_base = (u16)sizeof(tss);

    gdt[0] = 0;
    gdt[1] = make_descriptor(0, 0xFFFFF, 0x9A, 0xA0); /* kernel code, L=1 */
    gdt[2] = make_descriptor(0, 0xFFFFF, 0x92, 0xC0); /* kernel data */
    gdt[3] = make_descriptor(0, 0xFFFFF, 0xFA, 0xA0); /* user code, L=1 */
    gdt[4] = make_descriptor(0, 0xFFFFF, 0xF2, 0xC0); /* user data */

    u64 tss_base = (u64)&tss;
    gdt[5] = make_descriptor((u32)tss_base, sizeof(tss) - 1, 0x89, 0x00);
    gdt[6] = tss_base >> 32;

    ptr.limit = sizeof(gdt) - 1;
    ptr.base = (u64)gdt;
    gdt_flush(&ptr);
    tss_flush();
}
