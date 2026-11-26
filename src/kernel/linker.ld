/* Zhaoxu OS - Yuanshuo kernel linker script */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA = 0xffffffff80000000;
/* The FreeBSD personality links into this image: text 5.5 MiB, rodata 0.8 MiB,
 * data 0.7 MiB, bss 84 MiB.  The boot stack window sits above it; limine_boot
 * maps the 2 MiB page that holds the top at a BSS array. */
KERNEL_STACK_TOP = 0xffffffff87f00000;

SECTIONS
{
    . = KERNEL_VMA;

    .text : ALIGN(4096)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }
    /* FreeBSD objects reference the end of text and the dynamic section. */
    PROVIDE(_etext = .);
    PROVIDE(etext = .);
    PROVIDE(_DYNAMIC = 0);

    /* The FreeBSD set_* orphan sections land right after .text; start them on
     * a fresh page so no segment shares a page with a different permission
     * (Limine refuses that: "different permissions sharing the same page"). */
    . = ALIGN(4096);

    /* lld places the dynamic relocation table at the exact end of .text, and
     * as a read-only orphan it drags the R segment back into the last text
     * page (0x...556bc0 instead of 0x...557000).  Pin it to a fresh page. */
    .rela.dyn : ALIGN(4096)
    {
        *(.rela.dyn)
        *(.rela.plt)
        *(.rela.*)
    }

    /* clang emits .note.gnu.property into the FreeBSD objects; it would start
     * a read-only segment inside the last text page.  Nothing reads it. */
    /DISCARD/ : { *(.note.gnu.property) *(.note.*) }

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
    PROVIDE(edata = .);

    .bss : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* FreeBSD personality sets (M2) are left as orphan sections on purpose:
     * their input names (set_sysinit, set_pcpu, ...) are C identifiers, so the
     * linker synthesizes __start_set_<name>/__stop_set_<name> for them.  An
     * explicit ".set_*" output section would suppress that. */

    __kernel_end = .;
    _end = .;
    PROVIDE(end = .);
    /* FreeBSD's ldscript provides these; pmap.c and the build-id prologue
     * reference them. */
    PROVIDE(brwsection = .);
    PROVIDE(__build_id_start = .);
    PROVIDE(__build_id_end = .);

    /* Keep the image clear of the 2 MiB boot-stack mapping below the top. */
    ASSERT(__kernel_end < KERNEL_STACK_TOP - 0x200000,
           "kernel image exceeds the reserved kernel window")
}
