/* Zhaoxu OS - early kernel entry (64-bit, higher half) */
.section .text.entry
.global _start
.extern kernel_main

_start:
    cli
    movabs $0xffffffff80f00000, %rsp
    xor %rbp, %rbp
    /* RDI already holds the boot_info pointer from the loader. */
    call kernel_main

1:
    cli
    hlt
    jmp 1b
