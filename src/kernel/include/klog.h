#ifndef ZHAOXU_KLOG_H
#define ZHAOXU_KLOG_H

#include <types.h>

/* Kernel log ring: the last KLOG_BYTES console bytes, readable through
 * syscall 81 (klogctl).  Separate from the Ring3 driver log page because
 * that page is consumed by the driver, not by user programs. */
#define KLOG_BYTES 65536u

void klog_putc(char c);
u32 klog_size(void);
u32 klog_read(char *buffer, u32 length, int clear);
void klog_clear(void);

#endif
