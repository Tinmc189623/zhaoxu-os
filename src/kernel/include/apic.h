#ifndef ZHAOXU_APIC_H
#define ZHAOXU_APIC_H

#include <types.h>

#define APIC_VECTOR_TIMER     0x40
#define APIC_VECTOR_RESCHED   0x41
#define APIC_VECTOR_SPURIOUS  0x42

void apic_init(u64 rsdp);
u64 apic_locate_rsdp(u64 candidate);
u32 apic_cpu_count(void);
u32 apic_cpu_apic_id(u32 index);
u16 apic_cpu_acpi_id(u32 index);
u64 apic_base(void);

void lapic_enable_bsp(void);
void lapic_enable_ap(void);
void lapic_eoi(void);
void apic_send_ipi(u32 apic_id, u32 icr_low);
void apic_start_cpu(u32 apic_id, u32 vector);

u32 apic_calibrate_lapic(void);
u32 apic_lapic_hz(void);
void apic_timer_start(u32 vector, u32 hz);
void apic_timer_stop(void);

#endif
