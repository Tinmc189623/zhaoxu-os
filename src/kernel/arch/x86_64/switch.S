/* Zhaoxu OS - kernel thread context switch */
.section .text
.code64

/*
 * void thread_switch(u64 *save_rsp, u64 next_rsp, u64 flags)
 *
 * Pushes the callee-saved registers plus the caller's RFLAGS, stores the
 * resulting stack pointer, then restores the next thread's context.  The
 * saved flags make each thread remember whether interrupts were enabled at
 * the point it was switched out.
 */
.global thread_switch
thread_switch:
    pushq   %rdx
    pushq   %rbx
    pushq   %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movq    %rsp, (%rdi)
    movq    %rsi, %rsp

    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbp
    popq    %rbx
    popfq
    ret
