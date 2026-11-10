/* Zhaoxu OS - ring 3 transition helpers */
.section .text
.code64

.global user_enter
.global user_resume
.global g_kernel_rsp

/*
 * void user_enter(u64 entry, u64 user_stack)
 *
 * Saves the kernel context (callee-saved registers and the stack pointer) so
 * that user_resume() can return to process_run(), then iretq's into ring 3.
 */
user_enter:
    pushq   %rbx
    pushq   %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    movq    %rsp, g_kernel_rsp(%rip)

    movw    $0x23, %ax
    movw    %ax, %ds
    movw    %ax, %es

    pushq   $0x23               /* SS  */
    pushq   %rsi                /* RSP */
    pushq   $0x3202             /* RFLAGS: IF=1, IOPL=3 (driver IO) */
    pushq   $0x1B               /* CS (user code, RPL 3) */
    pushq   %rdi                /* RIP */
    iretq

/*
 * void user_resume(void)
 *
 * Called from the syscall or exception path while running on the per-process
 * kernel stack.  Switches back to the saved kernel stack and returns from the
 * original user_enter() call.
 */
user_resume:
    movq    g_kernel_rsp(%rip), %rsp
    sti
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbp
    popq    %rbx
    ret

.section .bss
.align 8
g_kernel_rsp:
    .quad 0
