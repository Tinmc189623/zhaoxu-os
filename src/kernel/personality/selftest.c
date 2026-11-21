/*
 * Zhaoxu OS - M1 personality API self-test.
 *
 * Built only with PERSONALITY_SELFTEST=1.  Every check prints a boot marker
 * in the AGENTS.md smoke style; main halts after the last one.  Nothing here
 * links a personality image: M1 tests the core contract only.
 */
#if PERSONALITY_SELFTEST

#include <boot.h>
#include <kprintf.h>
#include <mem.h>
#include <personality.h>
#include <string.h>
#include <thread.h>
#include <types.h>

static volatile u32 g_thread_hits;
static volatile u32 g_trap_hits;
static volatile u32 g_timer_hits;

/* Task body: proves the thread actually ran on the core scheduler. */
static void test_thread(void *arg)
{
    (*(volatile u32 *)arg)++;
}

/* Trap hook: claims the vector and counts the delivery. */
static int test_trap(struct interrupt_frame *f, void *ctx)
{
    (void)f;
    (*(volatile u32 *)ctx)++;
    return 1;
}

/* Timer callback: runs in interrupt context, so it only bumps a counter. */
static void test_timer(void *ctx)
{
    (*(volatile u32 *)ctx)++;
}

int personality_selftest(void)
{
    u32 failures = 0;

    kprintf("\n[per ] M1 personality API self-test\n");
    kprintf("[per ] vm region %u pages at 0x%llx\n",
            personality_vm_pages(), personality_vm_base());

    /* VM: map, write, read, unmap, remap with NX, read again. */
    u32 vm_ok = 0;
    if (personality_vm_pages() >= 1 && personality_vm_base()) {
        volatile u64 *page = (volatile u64 *)(uintptr_t)personality_vm_base();
        u64 physical = 0;

        vm_ok = personality_vm_phys(0, &physical) == 0 &&
                personality_vm_map(0, VMM_WRITABLE) == 0;
        if (vm_ok) {
            page[0] = 0x5A4F5331CAFEBABEULL;
            vm_ok = page[0] == 0x5A4F5331CAFEBABEULL &&
                    personality_vm_unmap(0) == 0 &&
                    personality_vm_map(0, VMM_WRITABLE | VMM_EXECUTE_DISABLE) == 0 &&
                    page[0] == 0x5A4F5331CAFEBABEULL;
        }
    }
    if (!vm_ok)
        failures++;
    kprintf("[per ] vm map/write/unmap %s\n", vm_ok ? "PASS" : "FAIL");

    /* Task thread: create, run, join, slot released. */
    g_thread_hits = 0;
    struct personality_task *task =
        personality_task_create("per-test", test_thread, (void *)&g_thread_hits);
    u32 thread_ok = task && personality_task_join(task, 500) == 0 &&
                    g_thread_hits == 1;
    if (!thread_ok)
        failures++;
    kprintf("[per ] task thread %s\n", thread_ok ? "PASS" : "FAIL");

    /* Kernel IPC: one message across an endpoint pair, then close. */
    u32 ipc_ok = 0;
    u64 first = 0;
    u64 second = personality_ipc_create(&first);
    if (first && second) {
        const char payload[] = "kport";
        char buffer[8];
        u64 type = 0;

        memset(buffer, 0, sizeof(buffer));
        ipc_ok = personality_ipc_send(first, 7, payload, 5) == 5 &&
                 personality_ipc_recv(second, &type, buffer, sizeof(buffer), 100) == 5 &&
                 type == 7 &&
                 memcmp(buffer, payload, 5) == 0 &&
                 personality_ipc_close(first) == 0 &&
                 personality_ipc_close(second) == 0;
    }
    if (!ipc_ok)
        failures++;
    kprintf("[per ] kernel ipc %s\n", ipc_ok ? "PASS" : "FAIL");

    /* Trap forwarding: claim vector 3, raise int3, count the delivery. */
    g_trap_hits = 0;
    u32 trap_ok = personality_trap_register(3, test_trap, (void *)&g_trap_hits) == 0;
    if (trap_ok) {
        __asm__ volatile("int3" ::: "memory");
        trap_ok = g_trap_hits == 1 && personality_trap_unregister(3) == 0;
    }
    if (!trap_ok)
        failures++;
    kprintf("[per ] trap forwarding %s\n", trap_ok ? "PASS" : "FAIL");

    /* Timer service: 10-tick period, 50 ms of ticks must fire it. */
    g_timer_hits = 0;
    u32 timer_ok = personality_timer_register(10, test_timer,
                                              (void *)&g_timer_hits) == 0;
    if (timer_ok) {
        thread_sleep_ms(50);
        timer_ok = personality_timer_unregister(test_timer) == 0 &&
                   g_timer_hits > 0;
    }
    if (!timer_ok)
        failures++;
    kprintf("[per ] timer service %s (%u hits)\n",
            timer_ok ? "PASS" : "FAIL", (u32)g_timer_hits);

    /* Boot handoff and console service (M3 core side). */
    const struct boot_info *info = personality_boot_info();
    u32 boot_ok = info && info->magic == BOOT_MAGIC;
    if (!boot_ok)
        failures++;
    kprintf("[per ] boot handoff %s\n", boot_ok ? "PASS" : "FAIL");

    static const char console_marker[] = "[per ] console service PASS\n";
    personality_console_write(console_marker, sizeof(console_marker) - 1);

    kprintf("[per ] personality API %s\n", failures ? "FAIL" : "PASS");
    return failures ? -1 : 0;
}

#endif
