#ifndef ZHAOXU_ELF_LOADER_H
#define ZHAOXU_ELF_LOADER_H

#include <types.h>

#define USER_LOAD_BASE      0x0000010000000000ULL   /* 1 TiB */
#define USER_STACK_TOP      0x0000010100000000ULL   /* 1 TiB + 4 GiB */
#define USER_STACK_PAGES    32
#define USER_ADDRESS_LIMIT  0x0000018000000000ULL

struct elf64_header {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 program_header_offset;
    u64 section_header_offset;
    u32 flags;
    u16 header_size;
    u16 program_header_size;
    u16 program_header_count;
    u16 section_header_size;
    u16 section_header_count;
    u16 string_table_index;
} __attribute__((packed));

struct elf64_program_header {
    u32 type;
    u32 flags;
    u64 offset;
    u64 virtual_address;
    u64 physical_address;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
} __attribute__((packed));

#define ELF_PT_LOAD 1

struct process;

/* Load a ZXE or ELF image, map the user stack and build argc/argv/envp/auxv.
 * Returns the entry point and initial stack pointer; brk is set for ELF. */
int image_load(struct process *process, const u8 *image, u64 image_size,
               const char *path, int argc, const char **argv,
               u64 *entry_out, u64 *stack_out);

#endif
