/* Zhaoxu OS - early kernel entry (64-bit, higher half) */
.section .text.entry
.global _start
.extern kernel_main
.extern limine_boot_prepare

_start:
    cli
    /* Limine enters with its own stack and page tables.  Build the identity
     * map and boot_info first, then switch to the kernel's reserved stack. */
    call limine_boot_prepare
    test %rax, %rax
    jz 2f
    mov %rax, %rdi
    movabs $0xffffffff80f00000, %rsp
    xor %rbp, %rbp
    call kernel_main

2:
    cli
    hlt
    jmp 2b

1:
    cli
    hlt
    jmp 1b
