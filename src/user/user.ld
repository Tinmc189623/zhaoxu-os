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

    .data : ALIGN(4096)
    {
        *(.dynamic)
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
