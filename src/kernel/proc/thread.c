/*
 * Zhaoxu OS - preemptive multithreaded scheduler.
 *
 * Every CPU pulls threads from one shared round-robin table.  The per-CPU
 * timer interrupt decrements the running thread's quantum and, on expiry,
 * switches to the next ready thread.  Kernel threads may pin themselves to
 * a CPU through their affinity field; the main (boot) thread is pinned to
 * the BSP because the ring 3 process still relies on BSP-local state.
 */
#include <gdt.h>
#include <io.h>
#include <kprintf.h>
#include <mem.h>
#include <percpu.h>
#include <process.h>
#include <smp.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

extern void thread_switch(u64 *save_rsp, u64 next_rsp, u64 flags);

static struct thread g_threads[MAX_THREADS];
static spinlock_t g_sched_lock = SPINLOCK_INIT;
static volatile u32 g_sched_ready;
static u32 g_next_tid = 1;

static void thread_start(void);
static void idle_entry(void *arg);
static void sched_finish_switch(void);

static struct thread *pick_next_locked(struct percpu *pc)
{
    struct thread *idle = pc->idle;
    u32 start = pc->rr_cursor;

    for (u32 i = 0; i < MAX_THREADS; i++) {
        struct thread *thread = &g_threads[(start + i) % MAX_THREADS];
        if (thread->state != THREAD_READY)
            continue;
        if (thread->on_cpu)
            continue;   /* still executing (or mid-switch) on another CPU */
        if (thread->affinity >= 0 && (u32)thread->affinity != pc->index)
            continue;
        if (thread->is_idle)
            continue;
        pc->rr_cursor = (start + i + 1) % MAX_THREADS;
        return thread;
    }

    if (idle && idle->state == THREAD_READY &&
        (idle->affinity < 0 || (u32)idle->affinity == pc->index))
        return idle;
    return NULL;
}

static void schedule(void)
{
    u64 flags;

    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");

    struct percpu *pc = percpu_this();
    struct thread *previous = pc->current;

    if (!__atomic_load_n(&g_sched_ready, __ATOMIC_RELAXED) || !previous) {
        irq_restore(flags);
        return;
    }

    spin_lock(&g_sched_lock);
    if (previous->state == THREAD_RUNNING)
        previous->state = THREAD_READY;

    struct thread *next = pick_next_locked(pc);
    if (!next)
        next = pc->idle;
    if (!next) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return;
    }

    next->state = THREAD_RUNNING;
    next->cpu = (i32)pc->index;
    next->quantum = SCHED_QUANTUM_TICKS;
    next->on_cpu = 1;
    pc->current = next;

    if (next == previous) {
        spin_unlock(&g_sched_lock);
        irq_restore(flags);
        return;
    }

    /* The old thread stays on_cpu until the new context runs; otherwise
     * another CPU could pick it up while it is still switching stacks. */
    pc->switch_from = previous;
    spin_unlock(&g_sched_lock);

    /* Every thread carries the address space it must run in; kernel threads
     * fall back to the boot page tables.  The per-CPU TSS rsp0 must point at
     * the next thread's kernel stack before it can be re-entered from ring 3. */
    u64 next_cr3 = next->cr3 ? next->cr3 : vmm_kernel_cr3();
    if (next_cr3 != vmm_current_cr3())
        vmm_activate_cr3(next_cr3);
    if (next->kernel_stack_top)
        tss_set_rsp0_cpu(pc->index, next->kernel_stack_top);

    previous->switches++;
    thread_switch(&previous->rsp, next->rsp, flags);

    /* Resumed on the next thread's stack: the context that switched to it
     * is now off-CPU and may become runnable again. */
    sched_finish_switch();
    sched_reap_local();
}

static void sched_finish_switch(void)
{
    struct percpu *pc = percpu_this();

    u64 flags = spin_lock_irqsave(&g_sched_lock);
    struct thread *previous = pc->switch_from;
    pc->switch_from = NULL;
    if (previous) {
        previous->on_cpu = 0;
        if (previous->state == THREAD_DEAD) {
            previous->reap_next = pc->reap_list;
            pc->reap_list = previous;
        }
    }
    spin_unlock_irqrestore(&g_sched_lock, flags);
}

void sched_reap_local(void)
{
    struct percpu *pc = percpu_this();

    for (;;) {
        u64 flags = spin_lock_irqsave(&g_sched_lock);
        struct thread *thread = pc->reap_list;
        if (thread)
            pc->reap_list = thread->reap_next;
        spin_unlock_irqrestore(&g_sched_lock, flags);

        if (!thread)
            break;

        void *stack = (void *)(uintptr_t)thread->stack_base;

        /* A dead process keeps its address space until its thread is reaped:
         * the space cannot be torn down while the thread still runs on it. */
        if (thread->proc || thread->space)
            process_thread_reaped(thread);

        thread->stack_base = 0;
        thread->stack_size = 0;
        thread->kernel_stack_top = 0;
        thread->cr3 = 0;
        thread->space = NULL;
        thread->proc = NULL;
        thread->entry = NULL;
        thread->arg = NULL;
        thread->reap_next = NULL;
        thread->state = THREAD_UNUSED;
        thread->on_cpu = 0;
        thread->tid = 0;
        thread->name[0] = 0;
        if (stack)
            kfree(stack);
    }
}

static void thread_start(void)
{
    struct thread *thread = sched_current();

    sched_finish_switch();
    sched_reap_local();

    /* The handoff is complete; only now may this CPU take timer interrupts
     * again, otherwise a second switch could overwrite pc->switch_from. */
    sti();

    if (!thread || !thread->entry)
        kthread_exit();

    thread->entry(thread->arg);
    kthread_exit();
}

static void idle_entry(void *arg)
{
    (void)arg;
    for (;;) {
        sti();
        hlt();
    }
}

void sched_init(u32 cpu_count)
{
    memset(g_threads, 0, sizeof(g_threads));
    g_next_tid = 1;
    __atomic_store_n(&g_sched_ready, 0u, __ATOMIC_RELEASE);

    if (cpu_count == 0)
        cpu_count = 1;
    if (cpu_count > MAX_CPUS)
        cpu_count = MAX_CPUS;

    for (u32 cpu = 0; cpu < MAX_CPUS; cpu++) {
        g_percpu[cpu].current = NULL;
        g_percpu[cpu].idle = NULL;
        g_percpu[cpu].reap_list = NULL;
        g_percpu[cpu].rr_cursor = 0;
        g_percpu[cpu].need_resched = 0;
        g_percpu[cpu].preempt_count = 0;
    }

    struct thread *main_thread = &g_threads[0];
    main_thread->tid = g_next_tid++;
    main_thread->state = THREAD_RUNNING;
    main_thread->cpu = 0;
    main_thread->affinity = 0;
    main_thread->quantum = SCHED_QUANTUM_TICKS;
    main_thread->kernel_stack_top = KERNEL_BOOT_STACK_TOP;
    main_thread->on_cpu = 1;
    strncpy(main_thread->name, "main", sizeof(main_thread->name) - 1);
    g_percpu[0].current = main_thread;

    struct thread *idle0 = kthread_create("idle0", idle_entry, NULL, 0);
    if (idle0) {
        idle0->is_idle = 1;
        g_percpu[0].idle = idle0;
    }

    for (u32 cpu = 1; cpu < cpu_count; cpu++) {
        struct thread *idle = kthread_create("idle", idle_entry, NULL, (i32)cpu);
        if (idle) {
            idle->is_idle = 1;
            g_percpu[cpu].idle = idle;
            g_percpu[cpu].current = idle;
        }
    }

    kprintf("[sched] %u slots, %u CPUs, quantum %u ticks\n",
            MAX_THREADS, cpu_count, SCHED_QUANTUM_TICKS);
}

void sched_start(void)
{
    sched_reap_local();
    __atomic_store_n(&g_sched_ready, 1u, __ATOMIC_RELEASE);
}

void sched_tick(void)
{
    struct percpu *pc = percpu_this();

    pc->ticks++;

    if (!__atomic_load_n(&g_sched_ready, __ATOMIC_RELAXED))
        return;

    struct thread *thread = pc->current;
    if (!thread)
        return;

    if (thread->is_idle || thread->state != THREAD_RUNNING) {
        pc->need_resched = 1;
        return;
    }

    if (thread->quantum > 0)
        thread->quantum--;
    if (thread->quantum == 0)
        pc->need_resched = 1;
}

void sched_wake_expired(u64 now)
{
    u64 flags = spin_lock_irqsave(&g_sched_lock);

    for (u32 i = 0; i < MAX_THREADS; i++) {
        struct thread *thread = &g_threads[i];
        if (thread->state == THREAD_BLOCKED && thread->wake_tick <= now) {
            thread->wake_tick = 0;
            thread->quantum = SCHED_QUANTUM_TICKS;
            thread->state = THREAD_READY;
        }
    }

    spin_unlock_irqrestore(&g_sched_lock, flags);
}

void sched_maybe_preempt(void)
{
    struct percpu *pc = percpu_this();

    if (!__atomic_load_n(&g_sched_ready, __ATOMIC_RELAXED))
        return;
    if (pc->preempt_count)
        return;
    if (!pc->need_resched)
        return;

    pc->need_resched = 0;
    schedule();
}

struct thread *kthread_create_ctx(const char *name, void (*entry)(void *),
                                  void *arg, i32 affinity, u64 cr3,
                                  struct address_space *space,
                                  struct process *proc)
{
    if (!entry)
        return NULL;

    void *stack = kmalloc(KERNEL_THREAD_STACK);
    if (!stack)
        return NULL;

    u64 top = ((u64)(uintptr_t)stack + KERNEL_THREAD_STACK) & ~0xFULL;
    u64 *sp = (u64 *)(uintptr_t)top;
    *--sp = (u64)(uintptr_t)thread_start;   /* return address */
    *--sp = 0x2;                            /* RFLAGS: IF=0 until the
                                             * switch handoff completes */
    *--sp = 0;                              /* rbx */
    *--sp = 0;                              /* rbp */
    *--sp = 0;                              /* r12 */
    *--sp = 0;                              /* r13 */
    *--sp = 0;                              /* r14 */
    *--sp = 0;                              /* r15 */

    struct thread *thread = NULL;
    u64 flags = spin_lock_irqsave(&g_sched_lock);

    for (u32 i = 1; i < MAX_THREADS; i++) {
        if (g_threads[i].state == THREAD_UNUSED) {
            thread = &g_threads[i];
            break;
        }
    }

    if (thread) {
        memset(thread, 0, sizeof(*thread));
        thread->rsp = (u64)(uintptr_t)sp;
        thread->stack_base = (u64)(uintptr_t)stack;
        thread->stack_size = KERNEL_THREAD_STACK;
        thread->kernel_stack_top = top;
        thread->cr3 = cr3;
        thread->space = space;
        thread->proc = proc;
        thread->entry = entry;
        thread->arg = arg;
        thread->tid = g_next_tid++;
        thread->state = THREAD_READY;
        thread->affinity = affinity;
        thread->cpu = affinity >= 0 ? affinity : -1;
        thread->quantum = SCHED_QUANTUM_TICKS;
        if (name)
            strncpy(thread->name, name, sizeof(thread->name) - 1);
    }

    spin_unlock_irqrestore(&g_sched_lock, flags);

    if (!thread) {
        kfree(stack);
        return NULL;
    }

    smp_reschedule_all();
    return thread;
}

struct thread *kthread_create(const char *name, void (*entry)(void *),
                              void *arg, i32 affinity)
{
    return kthread_create_ctx(name, entry, arg, affinity, 0, NULL, NULL);
}

void thread_yield(void)
{
    struct thread *thread = sched_current();

    if (!thread || thread->is_idle) {
        __asm__ volatile("pause");
        return;
    }

    u64 flags = irq_save();
    if (thread->state == THREAD_RUNNING)
        thread->state = THREAD_READY;
    irq_restore(flags);

    schedule();
}

void thread_sleep_ms(u32 ms)
{
    struct thread *thread = sched_current();

    if (!thread || thread->is_idle ||
        !__atomic_load_n(&g_sched_ready, __ATOMIC_RELAXED)) {
        timer_sleep_ms(ms);
        return;
    }

    u64 delta = ((u64)ms * timer_hz() + 999) / 1000;
    if (delta == 0)
        delta = 1;

    u64 flags = irq_save();
    thread->wake_tick = timer_ticks() + delta;
    thread->state = THREAD_BLOCKED;
    irq_restore(flags);

    if (thread->state == THREAD_BLOCKED)
        schedule();
}

void kthread_exit(void)
{
    struct thread *thread = sched_current();
    u64 flags = irq_save();

    if (thread && !thread->is_idle)
        thread->state = THREAD_DEAD;

    schedule();
    irq_restore(flags);

    for (;;)
        hlt();
}

struct thread *sched_current(void)
{
    return percpu_this()->current;
}

u64 sched_idle_stack(u32 cpu)
{
    if (cpu >= MAX_CPUS)
        return 0;

    struct thread *idle = g_percpu[cpu].idle;
    if (!idle || !idle->stack_base)
        return 0;

    return (idle->stack_base + idle->stack_size) & ~0xFULL;
}

u32 sched_thread_count(void)
{
    u32 count = 0;
    for (u32 i = 0; i < MAX_THREADS; i++) {
        if (g_threads[i].state != THREAD_UNUSED)
            count++;
    }
    return count;
}

const struct thread *sched_thread_info(u32 index)
{
    return index < MAX_THREADS ? &g_threads[index] : NULL;
}

u64 sched_cpu_ticks(u32 cpu)
{
    return cpu < MAX_CPUS ? g_percpu[cpu].ticks : 0;
}
