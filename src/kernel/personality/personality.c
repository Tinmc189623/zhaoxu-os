/*
 * Zhaoxu OS - personality API (M1).
 *
 * The core exports what an in-kernel personality needs: task threads backed
 * by core kthreads, a reserved VM region, trap forwarding out of the IDT
 * dispatch, a periodic timer service and kernel-space IPC endpoints.  No
 * personality code is linked in M1; this file only makes the contract real.
 */
#include <boot.h>
#include <console.h>
#include <idt.h>
#include <mem.h>
#include <percpu.h>
#include <personality.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

#define PERSONALITY_NAME_MAX 20

struct personality_task {
    struct thread *thread;
    void (*entry)(void *);
    void *arg;
    volatile u32 done;
    u8 used;
    char name[PERSONALITY_NAME_MAX];
};

struct personality_trap {
    personality_trap_handler fn;
    void *ctx;
    u8 used;
    u8 vector;
};

struct personality_timer {
    personality_timer_handler fn;
    void *ctx;
    u32 period;
    u32 countdown;
    u8 used;
};

static struct personality_task g_tasks[PERSONALITY_MAX_TASKS];
static struct personality_trap g_traps[PERSONALITY_MAX_TRAPS];
static struct personality_timer g_timers[PERSONALITY_MAX_TIMERS];
static spinlock_t g_personality_lock = SPINLOCK_INIT;

static const struct boot_info *g_boot_info;
static u64 g_vm_base;
static u64 g_vm_phys[PERSONALITY_VM_PAGES];
static u8 g_vm_mapped[PERSONALITY_VM_PAGES];
static u32 g_vm_pages;
static u32 g_task_count;
static u8 g_ready;

/* Claim a vector for a registered handler; 0 lets the core handle it. */
static int personality_forward(struct interrupt_frame *f)
{
    for (u32 i = 0; i < PERSONALITY_MAX_TRAPS; i++) {
        if (g_traps[i].used && g_traps[i].vector == f->vector) {
            g_traps[i].fn(f, g_traps[i].ctx);
            return 1;
        }
    }
    return 0;
}

/* Thread entry used by every personality task: run, then publish completion. */
static void task_trampoline(void *arg)
{
    struct personality_task *task = (struct personality_task *)arg;

    task->entry(task->arg);
    __atomic_store_n(&task->done, 1u, __ATOMIC_RELEASE);
}

int personality_init(void)
{
    if (g_ready)
        return 0;

    u64 base = vmm_alloc_pages(PERSONALITY_VM_PAGES);
    if (!base)
        return -1;

    g_vm_base = base;
    g_vm_pages = PERSONALITY_VM_PAGES;

    /* The region is granted unmapped: the personality maps what it needs. */
    for (u32 i = 0; i < PERSONALITY_VM_PAGES; i++) {
        u64 va = base + (u64)i * PAGE_SIZE;
        g_vm_phys[i] = vmm_get_physical(va);
        vmm_unmap_page(va);
        g_vm_mapped[i] = 0;
    }

    idt_set_forward(personality_forward);
    g_ready = 1;
    return 0;
}

struct personality_task *personality_task_create(const char *name,
                                                 void (*entry)(void *),
                                                 void *arg)
{
    if (!entry || !g_ready)
        return NULL;

    u64 flags = spin_lock_irqsave(&g_personality_lock);
    struct personality_task *task = NULL;

    for (u32 i = 0; i < PERSONALITY_MAX_TASKS; i++) {
        if (!g_tasks[i].used) {
            task = &g_tasks[i];
            break;
        }
    }

    if (task) {
        memset(task, 0, sizeof(*task));
        task->used = 1;
        task->entry = entry;
        task->arg = arg;
        if (name)
            strncpy(task->name, name, sizeof(task->name) - 1);
    }

    spin_unlock_irqrestore(&g_personality_lock, flags);

    if (!task)
        return NULL;

    struct thread *thread = kthread_create(task->name, task_trampoline, task, -1);
    if (!thread) {
        flags = spin_lock_irqsave(&g_personality_lock);
        task->used = 0;
        spin_unlock_irqrestore(&g_personality_lock, flags);
        return NULL;
    }

    task->thread = thread;

    flags = spin_lock_irqsave(&g_personality_lock);
    g_task_count++;
    spin_unlock_irqrestore(&g_personality_lock, flags);
    return task;
}

int personality_task_join(struct personality_task *task, u32 timeout_ms)
{
    if (!task || !task->used)
        return -1;

    u32 waited = 0;

    while (!__atomic_load_n(&task->done, __ATOMIC_ACQUIRE)) {
        if (waited >= timeout_ms)
            return -1;
        thread_sleep_ms(1);
        waited++;
    }

    u64 flags = spin_lock_irqsave(&g_personality_lock);
    task->used = 0;
    if (g_task_count)
        g_task_count--;
    spin_unlock_irqrestore(&g_personality_lock, flags);
    return 0;
}

/* Hand the core boot record to the personality after the core is up. */
int personality_start(const struct boot_info *bi)
{
    if (!bi || bi->magic != BOOT_MAGIC)
        return -1;
    g_boot_info = bi;
    return 0;
}

const struct boot_info *personality_boot_info(void)
{
    return g_boot_info;
}

/* cn(4) output goes straight to the core console (VGA + log ring). */
void personality_console_write(const char *text, u32 length)
{
    if (text && length)
        console_write_n(text, length);
}

u32 personality_task_count(void)
{
    return __atomic_load_n(&g_task_count, __ATOMIC_RELAXED);
}

u64 personality_vm_base(void)
{
    return g_vm_base;
}

u32 personality_vm_pages(void)
{
    return g_vm_pages;
}

int personality_vm_phys(u32 index, u64 *physical)
{
    if (!physical || index >= g_vm_pages || !g_vm_phys[index])
        return -1;
    *physical = g_vm_phys[index];
    return 0;
}

int personality_vm_map(u32 index, u64 flags)
{
    if (!g_ready || index >= g_vm_pages || !g_vm_phys[index])
        return -1;

    u64 va = g_vm_base + (u64)index * PAGE_SIZE;
    int rc = vmm_map_page(va, g_vm_phys[index], flags | VMM_PRESENT);

    if (rc == 0)
        g_vm_mapped[index] = 1;
    return rc;
}

int personality_vm_unmap(u32 index)
{
    if (index >= g_vm_pages)
        return -1;

    u64 va = g_vm_base + (u64)index * PAGE_SIZE;

    if (g_vm_mapped[index]) {
        vmm_unmap_page(va);
        g_vm_mapped[index] = 0;
    }
    return 0;
}

int personality_trap_register(u8 vector, personality_trap_handler fn, void *ctx)
{
    if (!fn)
        return -1;

    u64 flags = spin_lock_irqsave(&g_personality_lock);

    for (u32 i = 0; i < PERSONALITY_MAX_TRAPS; i++) {
        if (g_traps[i].used && g_traps[i].vector == vector) {
            g_traps[i].fn = fn;
            g_traps[i].ctx = ctx;
            spin_unlock_irqrestore(&g_personality_lock, flags);
            return 0;
        }
    }

    for (u32 i = 0; i < PERSONALITY_MAX_TRAPS; i++) {
        if (!g_traps[i].used) {
            g_traps[i].used = 1;
            g_traps[i].vector = vector;
            g_traps[i].fn = fn;
            g_traps[i].ctx = ctx;
            spin_unlock_irqrestore(&g_personality_lock, flags);
            return 0;
        }
    }

    spin_unlock_irqrestore(&g_personality_lock, flags);
    return -1;
}

int personality_trap_unregister(u8 vector)
{
    u64 flags = spin_lock_irqsave(&g_personality_lock);

    for (u32 i = 0; i < PERSONALITY_MAX_TRAPS; i++) {
        if (g_traps[i].used && g_traps[i].vector == vector) {
            memset(&g_traps[i], 0, sizeof(g_traps[i]));
            spin_unlock_irqrestore(&g_personality_lock, flags);
            return 0;
        }
    }

    spin_unlock_irqrestore(&g_personality_lock, flags);
    return -1;
}

int personality_timer_register(u32 period_ticks, personality_timer_handler fn,
                               void *ctx)
{
    if (!fn || period_ticks == 0)
        return -1;

    u64 flags = spin_lock_irqsave(&g_personality_lock);

    for (u32 i = 0; i < PERSONALITY_MAX_TIMERS; i++) {
        if (!g_timers[i].used) {
            g_timers[i].used = 1;
            g_timers[i].fn = fn;
            g_timers[i].ctx = ctx;
            g_timers[i].period = period_ticks;
            g_timers[i].countdown = period_ticks;
            spin_unlock_irqrestore(&g_personality_lock, flags);
            return 0;
        }
    }

    spin_unlock_irqrestore(&g_personality_lock, flags);
    return -1;
}

int personality_timer_unregister(personality_timer_handler fn)
{
    u64 flags = spin_lock_irqsave(&g_personality_lock);

    for (u32 i = 0; i < PERSONALITY_MAX_TIMERS; i++) {
        if (g_timers[i].used && g_timers[i].fn == fn) {
            memset(&g_timers[i], 0, sizeof(g_timers[i]));
            spin_unlock_irqrestore(&g_personality_lock, flags);
            return 0;
        }
    }

    spin_unlock_irqrestore(&g_personality_lock, flags);
    return -1;
}

/* Core tick fan-out: boot CPU only, interrupt context, handlers must not sleep. */
void personality_timer_tick(void)
{
    if (cpu_index() != 0)
        return;

    for (u32 i = 0; i < PERSONALITY_MAX_TIMERS; i++) {
        if (!g_timers[i].used)
            continue;
        if (--g_timers[i].countdown == 0) {
            g_timers[i].countdown = g_timers[i].period;
            g_timers[i].fn(g_timers[i].ctx);
        }
    }
}
