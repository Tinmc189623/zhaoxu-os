/*
 * Zhaoxu OS - SMP bring-up.
 *
 * The BSP copies the real-mode trampoline to physical 0x7000, then walks the
 * MADT CPU list and starts each AP with INIT-SIPI-SIPI.  APs switch to the
 * kernel page tables inside the trampoline and enter ap_entry() on their own
 * idle stack.
 */
#include <apic.h>
#include <gdt.h>
#include <idt.h>
#include <io.h>
#include <kprintf.h>
#include <mem.h>
#include <percpu.h>
#include <smp.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

#define TRAMPOLINE_PHYSICAL 0x7000ULL
#define TRAMPOLINE_VECTOR   0x07u
#define TRAMPOLINE_PAGE     4096

extern u8 ap_trampoline_start[];
extern u8 ap_trampoline_end[];
extern u8 ap_trampoline_params[];

struct ap_params {
    u64 cr3;
    u64 stack;
    u64 entry;
    u64 cpu;
};

#if SCHED_STATS
static volatile u64 worker_hits[MAX_CPUS];
static volatile u32 churn_progress;

#define CHURN_THREADS 4
#define CHURN_ROUNDS  3
#endif

static u64 read_cr3(void)
{
    u64 value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

void ap_entry(u32 cpu);

#if SCHED_STATS
static void worker_entry(void *arg)
{
    u32 cpu = (u32)(uintptr_t)arg;
    for (;;) {
        worker_hits[cpu]++;
        __asm__ volatile("pause");
    }
}

static void churn_entry(void *arg)
{
    (void)arg;
    for (u32 round = 0; round < CHURN_ROUNDS; round++) {
        __atomic_add_fetch(&churn_progress, 1u, __ATOMIC_RELAXED);
        thread_sleep_ms(10);
    }
}

static void monitor_entry(void *arg)
{
    (void)arg;

    for (u32 round = 0; round < 3; round++) {
        thread_sleep_ms(1000);
        u64 heap_total, heap_used, heap_free;
        heap_get_stats(&heap_total, &heap_used, &heap_free);
        kprintf("[sched] ticks=%llu", timer_ticks());
        for (u32 cpu = 0; cpu < g_cpu_count; cpu++)
            kprintf(" c%u=%llu/%llu", cpu, sched_cpu_ticks(cpu),
                    worker_hits[cpu]);
        kprintf(" threads=%u churn=%u/%u heap=%llu\n",
                sched_thread_count(), churn_progress,
                CHURN_THREADS * CHURN_ROUNDS, heap_used);
    }
}
#endif

static int copy_trampoline(void)
{
    usize size = (usize)(ap_trampoline_end - ap_trampoline_start);
    if (size == 0 || size > TRAMPOLINE_PAGE)
        return -1;

    u8 *destination = (u8 *)(uintptr_t)TRAMPOLINE_PHYSICAL;
    memset(destination, 0, TRAMPOLINE_PAGE);
    memcpy(destination, ap_trampoline_start, size);

    for (usize offset = 0; offset < size; offset += 64)
        __asm__ volatile("clflush (%0)" :: "r"(destination + offset)
                         : "memory");
    __asm__ volatile("mfence" ::: "memory");
    return 0;
}

static void start_cpu(u32 cpu)
{
    struct ap_params *params = (struct ap_params *)(uintptr_t)
        (TRAMPOLINE_PHYSICAL +
         (usize)(ap_trampoline_params - ap_trampoline_start));

    params->cr3 = read_cr3();
    params->stack = sched_idle_stack(cpu);
    params->entry = (u64)(uintptr_t)ap_entry;
    params->cpu = cpu;

    if (!params->stack) {
        kprintf("[smp ] cpu%u has no idle stack, skipped\n", cpu);
        return;
    }

    apic_start_cpu(g_percpu[cpu].apic_id, TRAMPOLINE_VECTOR);

    for (u32 spin = 0; spin < 200; spin++) {
        if (__atomic_load_n(&g_percpu[cpu].online, __ATOMIC_ACQUIRE))
            return;
        apic_delay_ms(1);
    }

    kprintf("[smp ] cpu%u did not come online\n", cpu);
}

void ap_entry(u32 cpu)
{
    if (cpu >= MAX_CPUS) {
        for (;;)
            hlt();
    }

    struct percpu *pc = &g_percpu[cpu];

    gdt_ap_init(cpu);
    idt_reload();
    lapic_enable_ap();
    if (apic_lapic_hz())
        apic_timer_start(APIC_VECTOR_TIMER, timer_hz());

    pc->current = pc->idle;
    if (pc->idle) {
        pc->idle->state = THREAD_RUNNING;
        pc->idle->cpu = (i32)cpu;
    }

    __atomic_store_n(&pc->online, 1u, __ATOMIC_RELEASE);
    kprintf("[smp ] CPU%u online, apic %u, %u Hz\n", cpu, pc->apic_id,
            apic_lapic_hz());

    sti();
    for (;;) {
        sti();
        hlt();
    }
}

void smp_reschedule_all(void)
{
    u32 self = cpu_index();

    for (u32 cpu = 0; cpu < g_cpu_count; cpu++) {
        if (cpu == self)
            continue;
        if (!__atomic_load_n(&g_percpu[cpu].online, __ATOMIC_ACQUIRE))
            continue;
        apic_send_ipi(g_percpu[cpu].apic_id,
                      0x00004000u | APIC_VECTOR_RESCHED);
    }
}

static void start_demo_threads(u32 cpu_count)
{
#if !SCHED_STATS
    (void)cpu_count;
#else
    if (cpu_count > 1) {
        for (u32 cpu = 1; cpu < cpu_count; cpu++) {
            if (!kthread_create("worker", worker_entry,
                                (void *)(uintptr_t)cpu, (i32)cpu))
                kprintf("[sched] cannot create worker for cpu%u\n", cpu);
        }
        if (!kthread_create("monitor", monitor_entry, NULL, 1))
            kprintf("[sched] cannot create monitor thread\n");
    } else {
        /* A CPU-bound worker would steal half of the only core and make
         * boot interaction sluggish; the monitor still proves preemption. */
        if (!kthread_create("monitor", monitor_entry, NULL, 0))
            kprintf("[sched] cannot create monitor thread\n");
    }

    /* Short-lived threads exercise create/sleep/exit/reap on every CPU. */
    for (u32 i = 0; i < CHURN_THREADS; i++) {
        i32 cpu = cpu_count > 1 ? (i32)(2 + i % (cpu_count - 1)) : 0;
        cpu = cpu % (i32)cpu_count;
        if (!kthread_create("churn", churn_entry, NULL, cpu))
            kprintf("[sched] cannot create churn thread %u\n", i);
    }
#endif
}

void smp_init(u64 rsdp)
{
    apic_init(rsdp);

    u32 cpu_count = apic_cpu_count();
    if (cpu_count > MAX_CPUS)
        cpu_count = MAX_CPUS;

    sched_init(cpu_count);

    u32 hz = apic_calibrate_lapic();
    if (hz)
        kprintf("[smp ] LAPIC timer %u Hz, divider 16\n", hz);
    else
        kprintf("[smp ] LAPIC timer uncalibrated\n");

    if (cpu_count > 1) {
        if (copy_trampoline() == 0) {
            for (u32 cpu = 1; cpu < cpu_count; cpu++)
                start_cpu(cpu);
        } else {
            kprintf("[smp ] trampoline does not fit one page\n");
        }
    } else {
        kprintf("[smp ] single processor, LAPIC preemption\n");
    }

    sched_start();
    start_demo_threads(cpu_count);

    u32 online = 1;
    for (u32 cpu = 1; cpu < cpu_count; cpu++) {
        if (__atomic_load_n(&g_percpu[cpu].online, __ATOMIC_ACQUIRE))
            online++;
    }
    kprintf("[smp ] %u/%u CPU(s) online, scheduler preemptive at %u Hz\n",
            online, cpu_count, timer_hz());
}

u32 smp_cpu_count(void)
{
    return g_cpu_count;
}
