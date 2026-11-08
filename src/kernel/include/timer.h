#ifndef ZHAOXU_TIMER_H
#define ZHAOXU_TIMER_H

#include <types.h>

void timer_init(u32 hz);
u64 timer_ticks(void);
u32 timer_hz(void);
void timer_irq(void);
void timer_sleep_ms(u32 ms);
u64 timer_uptime_seconds(void);

#endif
