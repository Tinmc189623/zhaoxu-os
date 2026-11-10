#include <drivers.h>
#include <io.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

static volatile u64 ticks;
static u32 frequency = 100;

void timer_init(u32 hz)
{
    if (hz == 0)
        hz = 100;
    frequency = hz;
    ticks = 0;
    /* The BSP tick comes from the LAPIC timer (started after SMP init). */
}

void timer_irq(void)
{
    ticks++;
    sched_tick();
    sched_wake_expired(ticks);
}

u64 timer_ticks(void)
{
    return ticks;
}

u32 timer_hz(void)
{
    return frequency;
}

void timer_sleep_ms(u32 ms)
{
    u64 target = ticks + ((u64)ms * frequency + 999) / 1000;
    while (ticks < target)
        hlt();
}

u64 timer_uptime_seconds(void)
{
    return ticks / frequency;
}
