#ifndef ZHAOXU_HPET_H
#define ZHAOXU_HPET_H

#include <types.h>

/* Initialize the ACPI HPET described by the RSDP captured at boot. */
int hpet_init(void);
int hpet_present(void);
u64 hpet_hz(void);
u64 hpet_counter(void);
void hpet_delay_us(u64 microseconds);

#endif
