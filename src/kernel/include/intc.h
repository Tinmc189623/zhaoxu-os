#ifndef ZHAOXU_INTC_H
#define ZHAOXU_INTC_H

#include <types.h>

enum intc_kind {
    INTC_NONE = 0,
    INTC_APIC,
    INTC_PIC,
    INTC_GIC
};

/* x86 local APIC / 8259 PIC front end. */
int intc_x86_init(void);
enum intc_kind intc_kind(void);
const char *intc_name(void);
u32 intc_cpu_count(void);
void intc_enable_irq(u32 irq);
void intc_disable_irq(u32 irq);
void intc_eoi(u32 irq);

/* ARM GICv3 back end (compiled by the arm64 port, not by the x86 build). */
int gic_init(u64 distributor_base, u64 redistributor_base);
void gic_enable_irq(u32 irq);
void gic_disable_irq(u32 irq);
void gic_eoi(u32 irq);
u32 gic_acknowledge(void);
void gic_send_ipi(u32 target_affinity, u32 irq);

#endif
