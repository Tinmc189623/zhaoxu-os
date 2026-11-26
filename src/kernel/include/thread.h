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

#define KERNEL_BOOT_STACK_TOP 0xffffffff87f00000ULL

struct address_space;
struct process;

struct thread {
    u64 rsp;                 /* saved kernel stack pointer */
    u64 wake_tick;
    u64 switches;
    u64 quantum;
    u64 stack_base;
    usize stack_size;
    u64 kernel_stack_top;    /* TSS rsp0 while this thread runs */
    u64 cr3;                 /* 0 = kernel address space */
    u64 fs_base;             /* Ring3 TLS base, written to MSR 0xC0000100 */
    u64 futex_key;           /* physical word while blocked in a futex wait */
    u8 futex_woken;          /* 1 when a waker (not the timeout) released it */
    u64 clear_child_tid;     /* user word zeroed when this thread exits */
    i32 last_error;          /* errno recorded for the next SYS_LAST_ERROR */
    struct address_space *space;
    struct process *proc;
    void (*entry)(void *);
    void *arg;
    struct thread *reap_next;
    u32 tid;
    i32 cpu;
    i32 affinity;            /* -1 = any CPU */
    u8 state;
    u8 is_idle;
    u8 on_cpu;               /* running or switching on some CPU */
    char name[20];
    /* FXSAVE area: x87/SSE state is per-thread.  Without it, a Ring3 program
     * using SSE can be preempted by another SSE user (or kernel font code)
     * and resume with clobbered XMM registers. */
    u8 fpu_state[512] __attribute__((aligned(16)));
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
struct thread *kthread_create_ctx(const char *name, void (*entry)(void *),
                                  void *arg, i32 affinity, u64 cr3,
                                  struct address_space *space,
                                  struct process *proc);
int sched_futex_wait(u64 key, u64 address, u32 expected, u32 bitset,
                     u64 deadline_tick);
int sched_futex_wake(u64 key, int count);
void sched_wake_process(struct process *process);
void thread_set_fs_base(struct thread *thread, u64 value);
u64 thread_get_fs_base(const struct thread *thread);
void thread_yield(void);
void thread_sleep_ms(u32 ms);
void yuanshuo_kthread_exit(void) __attribute__((noreturn));

u32 sched_thread_count(void);
const struct thread *sched_thread_info(u32 index);
u64 sched_cpu_ticks(u32 cpu);

#endif
