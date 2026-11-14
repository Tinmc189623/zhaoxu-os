/*
 * Zhaoxu OS - ARM GICv3 interrupt controller driver.
 *
 * Part of the arm64 port of the kernel; the x86 build uses APIC/PIC through
 * kernel/int/intc_x86.c.  The distributor enables group 1 non-secure
 * interrupts with affinity routing, each redistributor wakes its own core,
 * and the CPU interface runs with system-register access and priority
 * masking suitable for a single secure-world EL1 kernel.
 */
#include <intc.h>
#include <types.h>

#define GICD_CTLR 0x0000
#define GICD_ISENABLER 0x0100
#define GICD_ICENABLER 0x0180
#define GICD_IPRIORITYR 0x0400

#define GICR_WAKER 0x0014
#define GICR_SGI_BASE 0x10000
#define GICR_IGROUPR0 (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_BASE + 0x0100)
#define GICR_ICENABLER0 (GICR_SGI_BASE + 0x0180)
#define GICR_IPRIORITYR (GICR_SGI_BASE + 0x0400)

#define GICD_CTLR_ENABLE_GRP1 (1u << 1)
#define GICD_CTLR_ARE_NS (1u << 4)
#define GICD_CTLR_RWP (1u << 31)

static volatile u32 *g_distributor;
static volatile u8 *g_redistributor;

static u32 mmio_read32(volatile void *address)
{
    return *(volatile u32 *)address;
}

static void mmio_write32(volatile void *address, u32 value)
{
    *(volatile u32 *)address = value;
}

static void wait_rwp(void)
{
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((mmio_read32(g_distributor + GICD_CTLR / 4) &
             GICD_CTLR_RWP) == 0)
            return;
}

static void write_sre(u64 value)
{
    __asm__ volatile("msr ICC_SRE_EL1, %0" :: "r"(value));
    __asm__ volatile("isb");
}

static void write_pmr(u64 value)
{
    __asm__ volatile("msr ICC_PMR_EL1, %0" :: "r"(value));
}

static void write_grpen1(u64 value)
{
    __asm__ volatile("msr ICC_IGRPEN1_EL1, %0" :: "r"(value));
}

int gic_init(u64 distributor_base, u64 redistributor_base)
{
    if (!distributor_base || !redistributor_base)
        return -1;
    g_distributor = (volatile u32 *)(uintptr_t)distributor_base;
    g_redistributor = (volatile u8 *)(uintptr_t)redistributor_base;

    /* CPU interface: system register access, priority drop and deactivate. */
    write_sre(0x7);
    write_pmr(0xF0);
    __asm__ volatile("msr ICC_CTLR_EL1, %0" :: "r"(1ull));
    write_grpen1(1);

    mmio_write32(g_distributor + GICD_CTLR / 4, 0);
    wait_rwp();
    mmio_write32(g_distributor + GICD_CTLR / 4,
                 GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_GRP1);
    wait_rwp();

    /* Wake this redistributor and enable all SGIs/PPIs in group 1. */
    u32 waker = mmio_read32(g_redistributor + GICR_WAKER);
    waker &= ~(1u << 1);
    mmio_write32(g_redistributor + GICR_WAKER, waker);
    for (u32 spin = 0; spin < 1000000u; spin++)
        if ((mmio_read32(g_redistributor + GICR_WAKER) & (1u << 2)) == 0)
            break;
    mmio_write32(g_redistributor + GICR_IGROUPR0, 0xFFFFFFFFu);
    for (u32 i = 0; i < 32; i++)
        g_redistributor[GICR_IPRIORITYR + i] = 0xA0;
    mmio_write32(g_redistributor + GICR_ISENABLER0, 0xFFFFFFFFu);

    /* Every shared peripheral interrupt starts at group 1 and medium
     * priority; the platform can refine the routes later. */
    for (u32 irq = 32; irq < 256; irq += 4)
        mmio_write32(g_distributor + GICD_IPRIORITYR + irq, 0xA0A0A0A0u);
    return 0;
}

void gic_enable_irq(u32 irq)
{
    if (!g_distributor)
        return;
    if (irq < 32)
        mmio_write32(g_redistributor + GICR_ISENABLER0, 1u << irq);
    else
        mmio_write32(g_distributor + GICD_ISENABLER + (irq / 32) * 4,
                     1u << (irq % 32));
}

void gic_disable_irq(u32 irq)
{
    if (!g_distributor)
        return;
    if (irq < 32)
        mmio_write32(g_redistributor + GICR_ICENABLER0, 1u << irq);
    else
        mmio_write32(g_distributor + GICD_ICENABLER + (irq / 32) * 4,
                     1u << (irq % 32));
}

u32 gic_acknowledge(void)
{
    u64 value;
    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(value));
    return (u32)value;
}

void gic_eoi(u32 irq)
{
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"((u64)irq));
    /* With EOImode=1, deactivate the interrupt after the handler body. */
    __asm__ volatile("msr ICC_DIR_EL1, %0" :: "r"((u64)irq));
}

void gic_send_ipi(u32 target_affinity, u32 irq)
{
    u64 value = (u64)irq | ((u64)(target_affinity & 0xFFFFFF) << 16);
    __asm__ volatile("msr ICC_SGI1R_EL1, %0" :: "r"(value));
    __asm__ volatile("isb");
}
