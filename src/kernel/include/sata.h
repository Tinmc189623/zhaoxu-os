#ifndef ZHAOXU_SATA_H
#define ZHAOXU_SATA_H

#include <types.h>

/* Implemented in Ada (src/drivers/sata.adb). */
int sata_init(void);
u64 sata_abar(void);
u32 sata_port_mask(void);
int sata_port_count(void);
u32 sata_port_det(int port);
u32 sata_port_gen(int port);

#endif
