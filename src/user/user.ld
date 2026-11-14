OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0;

    .text : ALIGN(4096)
    {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    } :rodata

    /* Newer lld rejects mixing SHT_DYNAMIC with PROGBITS inputs in one
     * output section, so the dynamic table gets its own header while still
     * living in the writable PT_LOAD segment. */
    .dynamic : ALIGN(4096)
    {
        *(.dynamic)
    } :data

    .data : ALIGN(4096)
    {
        *(.got .got.* .got.plt)
        *(.data .data.*)
    } :data

    .bss (NOLOAD) : ALIGN(4096)
    {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame)
    }
}
