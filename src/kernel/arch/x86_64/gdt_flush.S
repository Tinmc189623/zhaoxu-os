.section .text
.code64

.global gdt_flush
gdt_flush:
    lgdt    (%rdi)
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss
    pushq   $0x08
    leaq    1f(%rip), %rax
    pushq   %rax
    lretq
1:
    ret

/* Load the shared GDT on an application processor and give it its own TSS. */
.global gdt_load_ap
gdt_load_ap:
    lgdt    (%rdi)
    mov     $0x10, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss
    pushq   $0x08
    leaq    1f(%rip), %rax
    pushq   %rax
    lretq
1:
    mov     %si, %ax
    ltr     %ax
    ret

.global tss_flush
tss_flush:
    mov     $0x28, %ax
    ltr     %ax
    ret

.global idt_load
idt_load:
    lidt    (%rdi)
    ret
