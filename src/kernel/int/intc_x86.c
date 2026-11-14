/*
 * Zhaoxu OS - x86 interrupt controller front end.
 *
 * Wraps the two controllers this architecture can expose: the local APIC
 * (used whenever the MADT parser found one) and the legacy 8259 pair as the
 * fallback.  The GIC implementation lives in the arm64 port and shares the
 * kind enumeration.
 */
#include <apic.h>
#include <idt.h>
#include <intc.h>
#include <kprintf.h>
#include <smp.h>
#include <types.h>

static enum intc_kind g_kind;

int intc_x86_init(void)
{
    g_kind = apic_base() ? INTC_APIC : INTC_PIC;
    kprintf("[intc] %s interrupt controller\n", intc_name());
    return 0;
}

enum intc_kind intc_kind(void)
{
    return g_kind;
}

const char *intc_name(void)
{
    switch (g_kind) {
    case INTC_APIC:
        return "APIC";
    case INTC_PIC:
        return "PIC";
    case INTC_GIC:
        return "GIC";
    default:
        return "none";
    }
}

u32 intc_cpu_count(void)
{
    if (g_kind == INTC_APIC)
        return smp_cpu_count();
    return 1;
}

void intc_enable_irq(u32 irq)
{
    if (g_kind == INTC_PIC && irq < 16)
        pic_unmask((u8)irq);
}

void intc_disable_irq(u32 irq)
{
    if (g_kind == INTC_PIC && irq < 16)
        pic_mask((u8)irq);
}

void intc_eoi(u32 irq)
{
    if (g_kind == INTC_APIC)
        lapic_eoi();
    else if (irq < 16)
        pic_eoi((u8)irq);
}
