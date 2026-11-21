#ifndef ZHAOXU_PERSONALITY_H
#define ZHAOXU_PERSONALITY_H

#include <idt.h>
#include <types.h>

/*
 * Kernel-internal personality API (M1).
 *
 * This is the contract a future in-kernel personality (FreeBSD in OS_5P)
 * binds to.  It is not the Ring3 int 0x80 ABI: no user pointer is involved
 * and every call runs in kernel context.
 */

#define PERSONALITY_VM_PAGES   256   /* reserved region: 1 MiB */
#define PERSONALITY_MAX_TASKS  8
#define PERSONALITY_MAX_TRAPS  8
#define PERSONALITY_MAX_TIMERS 8

struct personality_task;
struct boot_info;

/* ---- boot handoff and console (M3 core side) ------------------------- */

/* Record the core boot info for the personality.  Returns 0 when the magic
 * matches; the personality reads it back through personality_boot_info(). */
int personality_start(const struct boot_info *bi);
const struct boot_info *personality_boot_info(void);

/* Console service the personality's cn(4) backend writes through. */
void personality_console_write(const char *text, u32 length);

/* ---- FreeBSD personality entry (M2 seam) ----------------------------- */

/* Provided by kernel/personality/freebsd/boot.c when FREEBSD_PERSONALITY=1
 * links the port.  Until freebsd_personality_main() is implemented, the weak
 * default reports that the personality is not linked. */
#if FREEBSD_PERSONALITY
int freebsd_personality_start(const struct boot_info *bi);
#endif

/* Trap handlers run in interrupt context; return 1 to claim the vector. */
typedef int (*personality_trap_handler)(struct interrupt_frame *f, void *ctx);

/* Timer callbacks run in interrupt context on the boot CPU. */
typedef void (*personality_timer_handler)(void *ctx);

/* Reserve the VM region and install the IDT forwarder.  Returns 0 on success. */
int personality_init(void);

/* ---- tasks: core kernel threads behind a personality task handle ---- */
struct personality_task *personality_task_create(const char *name,
                                                 void (*entry)(void *),
                                                 void *arg);

/* Wait for completion and release the task slot.  0 = done, -1 = timeout. */
int personality_task_join(struct personality_task *task, u32 timeout_ms);

u32 personality_task_count(void);

/* ---- VM region ------------------------------------------------------ */
u64 personality_vm_base(void);
u32 personality_vm_pages(void);
int personality_vm_phys(u32 index, u64 *physical);

/* Map / unmap / reprotect one page.  Flags are VMM_* bits; PRESENT is added. */
int personality_vm_map(u32 index, u64 flags);
int personality_vm_unmap(u32 index);

/* ---- trap forwarding ------------------------------------------------- */
int personality_trap_register(u8 vector, personality_trap_handler fn, void *ctx);
int personality_trap_unregister(u8 vector);

/* ---- timer service --------------------------------------------------- */
int personality_timer_register(u32 period_ticks,
                               personality_timer_handler fn, void *ctx);
int personality_timer_unregister(personality_timer_handler fn);

/* Driven by the core LAPIC tick; called from the interrupt path. */
void personality_timer_tick(void);

/* ---- kernel-space IPC endpoints -------------------------------------- */
u64 personality_ipc_create(u64 *peer);
int personality_ipc_send(u64 endpoint, u64 type, const void *data, u32 length);

/* Returns the message length, 0 on timeout, -1 on a closed or invalid endpoint. */
int personality_ipc_recv(u64 endpoint, u64 *type, void *data, u32 capacity,
                         u32 timeout_ms);

int personality_ipc_close(u64 endpoint);
u32 personality_ipc_live(void);

/* ---- self-test (M1 evidence) ----------------------------------------- */
#if PERSONALITY_SELFTEST
int personality_selftest(void);
#endif

#endif
