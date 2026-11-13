#ifndef ZHAOXU_ZXE_FORMAT_H
#define ZHAOXU_ZXE_FORMAT_H

#include <types.h>

#define ZXE_MAGIC "ZXE\0"
#define ZXE_VERSION 1
#define ZXE_MACHINE_X86_64 0x003E
#define ZXE_HEADER_SIZE 128
#define ZXE_DIRECTORY_ENTRY_SIZE 24
#define ZXE_MAX_FILE_SIZE (256ULL * 1024 * 1024)
#define ZXE_MAX_SEGMENTS 32
#define ZXE_MAX_DEPENDENCIES 64
#define ZXE_MAX_IMPORTS 4096
#define ZXE_MAX_EXPORTS 4096
#define ZXE_MAX_RELOCATIONS 65536
#define ZXE_MAX_STRINGS (1024U * 1024U)
#define ZXE_IMAGE_WINDOW (1024ULL * 1024 * 1024)
#define ZXE_MAX_TABLES 16

#define ZXE_FLAG_SHARED (1U << 0)
#define ZXE_DIR_REQUIRED (1U << 0)

#define ZXE_TABLE_SEGMENTS 1
#define ZXE_TABLE_DEPENDENCIES 2
#define ZXE_TABLE_IMPORTS 3
#define ZXE_TABLE_EXPORTS 4
#define ZXE_TABLE_RELOCATIONS 5
#define ZXE_TABLE_STRINGS 6

#define ZXE_SEG_CODE 1
#define ZXE_SEG_RODATA 2
#define ZXE_SEG_DATA 3
#define ZXE_SEG_ZERO 4
#define ZXE_PERM_R (1U << 0)
#define ZXE_PERM_W (1U << 1)
#define ZXE_PERM_X (1U << 2)

#define ZXE_IMPORT_WEAK (1U << 0)
#define ZXE_EXPORT_GLOBAL (1U << 0)

#define ZXE_RELOC_RELATIVE 1
#define ZXE_RELOC_64 2
#define ZXE_RELOC_GLOB_DAT 3
#define ZXE_RELOC_JUMP_SLOT 4

struct zxe_header {
    u8 magic[4];
    u16 version;
    u16 machine;
    u16 header_size;
    u16 flags;
    u16 directory_entry_size;
    u16 directory_count;
    u64 file_size;
    u64 entry_rva;
    u64 image_size;
    u64 directory_offset;
    u8 build_id[16];
    u32 api_abi_major;
    u32 reserved0;
    u8 reserved1[56];
} __attribute__((packed));

struct zxe_directory_entry {
    u32 kind;
    u32 flags;
    u64 offset;
    u32 count;
    u32 entry_size;
} __attribute__((packed));

struct zxe_segment {
    u32 type;
    u32 flags;
    u64 file_offset;
    u64 file_size;
    u64 mem_size;
    u64 vaddr_rva;
    u64 alignment;
} __attribute__((packed));

struct zxe_dependency {
    u32 name_offset;
    u32 abi_major;
    u32 flags;
    u32 reserved;
} __attribute__((packed));

struct zxe_import {
    u32 dependency_index;
    u32 name_offset;
    u32 abi_major;
    u32 flags;
} __attribute__((packed));

struct zxe_export {
    u32 name_offset;
    u32 abi_major;
    u64 value_rva;
    u32 size;
    u32 flags;
} __attribute__((packed));

struct zxe_relocation {
    u64 target_rva;
    u32 type;
    u32 symbol_index;
    i64 addend;
} __attribute__((packed));

#endif
