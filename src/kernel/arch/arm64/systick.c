/*
 * Zhaoxu OS - ARM SysTick timer.
 *
 * Cortex-M class cores provide the 24-bit SysTick down-counter; it is the
 * arm64/ARMv7-M counterpart of the x86 LAPIC timer.  The reload value is
 * derived from the core clock supplied by the platform.
 */
#include <types.h>

#define SYST_CSR 0xE000E010u
#define SYST_RVR 0xE000E014u
#define SYST_CVR 0xE000E018u

static volatile u64 g_ticks;
static u32 g_hz;

static void write32(u32 address, u32 value)
{
    *(volatile u32 *)(uintptr_t)address = value;
}

static u32 read32(u32 address)
{
    return *(volatile u32 *)(uintptr_t)address;
}

int systick_init(u32 core_clock, u32 hz)
{
    if (!core_clock || !hz)
        return -1;
    u32 reload = core_clock / hz;
    if (!reload || reload > 0x00FFFFFFu)
        return -1;
    g_hz = hz;
    g_ticks = 0;
    write32(SYST_RVR, reload - 1);
    write32(SYST_CVR, 0);
    /* Processor clock, interrupt enabled, counter enabled. */
    write32(SYST_CSR, 0x7);
    return 0;
}

void systick_irq(void)
{
    g_ticks++;
}

u64 systick_ticks(void)
{
    return g_ticks;
}

u32 systick_hz(void)
{
    return g_hz;
}

u32 systick_pending(void)
{
    return read32(SYST_CSR) & (1u << 16);
}
