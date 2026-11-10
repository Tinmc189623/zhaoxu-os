#include <apic.h>
#include <console.h>
#include <idt.h>
#include <io.h>
#include <kprintf.h>
#include <process.h>
#include <string.h>
#include <thread.h>
#include <timer.h>
#include <types.h>

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void isr32(void); extern void isr33(void); extern void isr34(void);
extern void isr35(void); extern void isr36(void); extern void isr37(void);
extern void isr38(void); extern void isr39(void); extern void isr40(void);
extern void isr41(void); extern void isr42(void); extern void isr43(void);
extern void isr44(void); extern void isr45(void); extern void isr46(void);
extern void isr47(void);
extern void isr64(void);
extern void isr65(void);
extern void isr66(void);
extern void isr128(void);
extern void isr255(void);

static struct idt_entry idt[256] __attribute__((aligned(16)));
static struct idt_ptr idt_pointer;

extern void idt_load(const struct idt_ptr *ptr);

static void (*const stubs[48])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
    isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
    isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
};

static const char *exception_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection", "Page Fault", "Reserved",
    "x87 Floating Point", "Alignment Check", "Machine Check", "SIMD Floating Point",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security", "Reserved"
};

static void set_gate(u8 vector, void (*handler)(void), u8 ist, u8 dpl)
{
    struct idt_entry *e = &idt[vector];
    u64 address = (u64)handler;
    e->offset_low = (u16)(address & 0xFFFF);
    e->selector = 0x08;
    e->ist = ist & 0x7;
    e->type_attr = (u8)(0x8E | ((dpl & 0x3) << 5));
    e->offset_mid = (u16)((address >> 16) & 0xFFFF);
    e->offset_high = (u32)(address >> 32);
    e->zero = 0;
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));
    for (u32 i = 0; i < 48; i++)
        set_gate((u8)i, stubs[i], 0, 0);

    /* Dedicated IST stacks for double fault and NMI. */
    set_gate(8, isr8, 1, 0);
    set_gate(2, isr2, 2, 0);

    /* System call gate, callable from ring 3. */
    set_gate(0x80, isr128, 0, 3);

    /* Local APIC vectors: timer, reschedule IPI, spurious. */
    set_gate(APIC_VECTOR_TIMER, isr64, 0, 0);
    set_gate(APIC_VECTOR_RESCHED, isr65, 0, 0);
    set_gate(APIC_VECTOR_SPURIOUS, isr66, 0, 0);
    set_gate(0xFF, isr255, 0, 0);

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (u64)idt;
    idt_load(&idt_pointer);
}

void idt_reload(void)
{
    idt_load(&idt_pointer);
}

static void exception_panic(struct interrupt_frame *f)
{
    u64 cr2 = 0;
    if (f->vector == 14)
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    kprintf("\n*** KERNEL EXCEPTION ***\n");
    kprintf("  vector : %u  %s\n", (u32)f->vector,
            f->vector < 32 ? exception_names[f->vector] : "Unknown");
    kprintf("  error  : 0x%llx\n", f->error);
    if (f->vector == 14)
        kprintf("  cr2    : 0x%llx\n", cr2);
    kprintf("  rip    : 0x%llx  cs:0x%llx  rflags:0x%llx\n",
            f->rip, f->cs, f->rflags);
    kprintf("  rsp    : 0x%llx  ss:0x%llx\n", f->rsp, f->ss);
    kprintf("  rax=%llx rbx=%llx rcx=%llx rdx=%llx\n",
            f->rax, f->rbx, f->rcx, f->rdx);
    kprintf("  rsi=%llx rdi=%llx rbp=%llx\n", f->rsi, f->rdi, f->rbp);
    console_reset_color();
    kprintf("System halted.\n");

    interrupts_disable();
    for (;;)
        hlt();
}

static void irq_dispatch(struct interrupt_frame *f)
{
    u32 irq = (u32)(f->vector - 32);

    (void)irq;

    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);

    sched_maybe_preempt();
}

void interrupt_dispatch(struct interrupt_frame *f)
{
    if (f->vector == 0x80) {
        syscall_dispatch(f);
        return;
    }
    if (f->vector == APIC_VECTOR_TIMER) {
        timer_irq();
        lapic_eoi();
        sched_maybe_preempt();
        return;
    }
    if (f->vector == APIC_VECTOR_RESCHED) {
        lapic_eoi();
        sched_maybe_preempt();
        return;
    }
    if (f->vector == APIC_VECTOR_SPURIOUS || f->vector == 0xFF)
        return;

    if (f->vector < 32) {
        if ((f->cs & 3) == 3 && process_active())
            process_handle_exception(f);
        exception_panic(f);
    } else if (f->vector < 48) {
        irq_dispatch(f);
    } else {
        exception_panic(f);
    }
}

void interrupts_enable(void)
{
    __asm__ volatile("sti");
}

void interrupts_disable(void)
{
    __asm__ volatile("cli");
}
