#ifndef ZHAOXU_PERCPU_H
#define ZHAOXU_PERCPU_H

#include <types.h>

#define MAX_CPUS 16

struct thread;

struct percpu {
    u32 index;
    u32 apic_id;
    volatile u32 online;
    u32 rr_cursor;
    struct thread *current;
    struct thread *idle;
    volatile u64 ticks;
    volatile u32 need_resched;
    u32 preempt_count;
    u32 lapic_hz;
    struct thread *reap_list;
};

extern struct percpu g_percpu[MAX_CPUS];
extern u32 g_cpu_count;

void percpu_init_bsp(u32 apic_id);
void percpu_register(u32 index, u32 apic_id);
struct percpu *percpu_this(void);
u32 cpu_index(void);

#endif
