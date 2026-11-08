#ifndef ZHAOXU_GDT_H
#define ZHAOXU_GDT_H

#include <types.h>

void gdt_init(void);
void tss_set_rsp0(u64 rsp0);
void tss_set_rsp0_cpu(u32 cpu, u64 rsp0);
void gdt_ap_init(u32 cpu);

#endif
