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

    .biosblob : ALIGN(4096)
    {
        *(.biosblob)
    }

    .data : ALIGN(4096)
    {
        *(.data .data.*)
        KEEP(*(.limine_requests))
    }

    .bss : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* FreeBSD personality sets (M2).  Each set keeps its own output section
     * so the linker synthesizes __start_set_<name>/__stop_set_<name>; the
     * sections stay empty until the FreeBSD objects are linked. */
    .set_sysinit : ALIGN(8) { KEEP(*(set_sysinit)) }
    .set_sysuninit : ALIGN(8) { KEEP(*(set_sysuninit)) }
    .set_sysctl : ALIGN(8) { KEEP(*(set_sysctl)) }
    .set_pcpu : ALIGN(8) { KEEP(*(set_pcpu)) }
    .set_sdt_tracepoint : ALIGN(8) { KEEP(*(set_sdt_tracepoint)) }

    __kernel_end = .;
    _end = .;
    /* FreeBSD's ldscript PROVIDEs this; pmap.o references it. */
    PROVIDE(brsection = .);

    ASSERT(__kernel_end < KERNEL_STACK_TOP - 0x100000,
           "kernel image exceeds the reserved 15 MiB window")
}
