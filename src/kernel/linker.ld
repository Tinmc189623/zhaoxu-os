/* Zhaoxu OS - Yuanshuo kernel linker script */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA = 0xffffffff80000000;
KERNEL_STACK_TOP = 0xffffffff80f00000;

SECTIONS
{
    . = KERNEL_VMA;

    .text : ALIGN(4096)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    }

    .initramfs : ALIGN(4096)
    {
        *(.initramfs)
    }

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;

    ASSERT(__kernel_end < KERNEL_STACK_TOP - 0x100000,
           "kernel image exceeds the reserved 15 MiB window")
}
