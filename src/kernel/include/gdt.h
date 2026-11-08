#ifndef ZHAOXU_GDT_H
#define ZHAOXU_GDT_H

#include <types.h>

void gdt_init(void);
void tss_set_rsp0(u64 rsp0);

#endif
