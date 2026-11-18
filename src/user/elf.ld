OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

/* Fixed-address ELF image for the kernel's elf_load() path, which requires
 * PT_LOAD virtual addresses inside [USER_LOAD_BASE, USER_ADDRESS_LIMIT).
 * ZXE images use user.ld and are relocated to a random base instead. */
PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000010000000000;

    .text : ALIGN(4096)
    {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    } :rodata

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
