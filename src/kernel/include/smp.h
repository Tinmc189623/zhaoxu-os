#ifndef ZHAOXU_SMP_H
#define ZHAOXU_SMP_H

#include <types.h>

void smp_init(u64 rsdp);
void smp_reschedule_all(void);
u32 smp_cpu_count(void);

#endif
