#ifndef ZHAOXU_THREAD_H
#define ZHAOXU_THREAD_H

#include <types.h>

#define MAX_THREADS 32
#define KERNEL_THREAD_STACK (16 * 1024)
#define SCHED_QUANTUM_TICKS 10

enum thread_state {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
};

struct thread {
    u64 rsp;                 /* saved kernel stack pointer */
    u64 wake_tick;
    u64 switches;
    u64 quantum;
    u64 stack_base;
    usize stack_size;
    void (*entry)(void *);
    void *arg;
    struct thread *reap_next;
    u32 tid;
    i32 cpu;
    i32 affinity;            /* -1 = any CPU */
    u8 state;
    u8 is_idle;
    char name[20];
};

void sched_init(u32 cpu_count);
void sched_start(void);
void sched_tick(void);
void sched_wake_expired(u64 now);
void sched_maybe_preempt(void);
struct thread *sched_current(void);
u64 sched_idle_stack(u32 cpu);
void sched_reap_local(void);

struct thread *kthread_create(const char *name, void (*entry)(void *),
                              void *arg, i32 affinity);
void thread_yield(void);
void thread_sleep_ms(u32 ms);
void kthread_exit(void) __attribute__((noreturn));

u32 sched_thread_count(void);
const struct thread *sched_thread_info(u32 index);
u64 sched_cpu_ticks(u32 cpu);

#endif
