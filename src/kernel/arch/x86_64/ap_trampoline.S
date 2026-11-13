/*
 * Zhaoxu OS - application processor startup trampoline.
 *
 * The blob is linked with the kernel but copied verbatim to physical 0x7000
 * before the BSP sends STARTUP IPIs (vector 0x07).  It starts in 16-bit real
 * mode, builds a flat protected-mode environment, enables long mode with the
 * kernel's page tables and finally jumps to ap_entry() on the per-CPU stack.
 *
 * All addresses that are not RIP-relative are expressed as offsets from
 * ap_trampoline_start plus TRAMP_BASE, so the code works at its new home.
 */
.section .text
.code16

.global ap_trampoline_start
.global ap_trampoline_end
.global ap_trampoline_params

.set TRAMP_BASE, 0x7000

ap_trampoline_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs

    .byte   0x0F, 0x01, 0x16                   /* lgdt 16-bit pseudo-desc */
    .word   gdt_descriptor - ap_trampoline_start + TRAMP_BASE

    movl    %cr0, %eax
    orl     $0x00000001, %eax                  /* PE */
    movl    %eax, %cr0

    .byte   0x66, 0xEA                          /* ljmpl $0x08, protected32 */
    .long   protected32 - ap_trampoline_start + TRAMP_BASE
    .word   0x08

.code32
protected32:
    movw    $0x18, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs

    movl    %cr4, %eax
    orl     $0x00000620, %eax                  /* PAE | OSFXSR | OSXMMEXCPT */
    movl    %eax, %cr4

    .byte   0xA1                                /* movl moffs32, %eax */
    .long   ap_trampoline_params - ap_trampoline_start + TRAMP_BASE
    movl    %eax, %cr3

    movl    $0xC0000080, %ecx                  /* EFER.LME | EFER.NXE */
    rdmsr
    orl     $0x00000900, %eax
    wrmsr

    movl    %cr0, %eax
    andl    $0xFFFFFFF3, %eax                  /* clear EM and TS */
    orl     $0x80000001, %eax                  /* PG | PE */
    movl    %eax, %cr0

    .byte   0xEA                                /* ljmp $0x10, protected64 */
    .long   protected64 - ap_trampoline_start + TRAMP_BASE
    .word   0x10

.code64
protected64:
    movw    $0x18, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs

    leaq    ap_trampoline_params(%rip), %rbx
    movq    8(%rbx), %rsp
    movq    16(%rbx), %rax
    movq    24(%rbx), %rdi
    jmp     *%rax

.align 8
gdt_start:
    .quad   0
    .quad   0x00CF9A000000FFFF                 /* 0x08: 32-bit code */
    .quad   0x00AF9A000000FFFF                 /* 0x10: 64-bit code */
    .quad   0x00CF92000000FFFF                 /* 0x18: data */
gdt_end:
gdt_descriptor:
    .word   gdt_end - gdt_start - 1
    .long   gdt_start - ap_trampoline_start + TRAMP_BASE

.align 8
ap_trampoline_params:
    .quad   0                                   /* cr3 */
    .quad   0                                   /* stack */
    .quad   0                                   /* entry */
    .quad   0                                   /* cpu index */
ap_trampoline_end:
