#ifndef ZHAOXU_IDT_H
#define ZHAOXU_IDT_H

#include <types.h>

struct interrupt_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rbx, rdx, rcx, rax;
    u64 vector, error;
    u64 rip, cs, rflags, rsp, ss;
};

void idt_init(void);
void idt_reload(void);
void pic_init(void);
void pic_unmask(u8 irq);
void pic_mask(u8 irq);
void pic_eoi(u8 irq);
void interrupts_enable(void);
void interrupts_disable(void);

#endif
