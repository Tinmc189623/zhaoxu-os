# ZXE Native Executable Format Design

Date: 2026-11-13  
Status: Format v1 field decisions confirmed; toolchain and some loading policy still to be added

## 1. Purpose and Scope

This document records the ZXE target direction settled in the current discussion: ZXE is the native executable format used by Ring3 user programs, adopts a native segment table, and supports dynamic linking at the format layer. ZXE files do not use ELF as the runtime payload container; ELF may serve as a compiler or linker intermediate, but the final ZXE should be produced by the packaging tool as native segment tables and payloads.

Storage is primarily ext4. FAT32 remains for the ESP file system and as a compatibility medium, and the kernel is not written into the boot sector or the ESP. ZXE applications and shared libraries live on the ext4 file system. This format does not cover the boot loader or the kernel image.

This document records the confirmed format v1 field decisions and the rules still to be filled in during the toolchain and loading-policy phase. It does not mean the loader, linker or dynamic linking features are already implemented.

## 2. Design Goals

- For x86-64 Ring3 user programs, consistent with the microkernel isolation model.
- Describe native code segments, read-only data segments and read-write data segments and their memory image without relying on runtime ELF parsing.
- Support applications depending on shared libraries, import/export symbol resolution and relocation.
- Use a versioned file layout with verifiable boundaries and room to extend.
- Allow Zhaoxu Aqua Framework (ZAF), Aqua Desktop Environment and ordinary applications to be organised as user-mode software through library dependencies, without merging GUI framework or application code into the kernel.
- The file format does not prescribe the Aqua theme. ZAF owns GUI rendering, widgets, window management, composition, animation and theming; it and its libraries are user-mode components.

## 3. Overall File Layout

A ZXE file consists of a fixed header, segment table, dynamic linking metadata, string data and segment payloads. Optional extension data goes into explicitly marked extension areas; the loader should be able to skip extensions it does not recognise that are marked ignorable, and must refuse to execute when it meets an unknown required feature.

```text
+---------------------------+
| Fixed header              |
+---------------------------+
| Native segment table      |
+---------------------------+
| Dynamic link metadata     |
+---------------------------+
| Relocation tables         |
+---------------------------+
| String table              |
+---------------------------+
| Optional extension area   |
+---------------------------+
| Segment payloads          |
+---------------------------+
```

### 3.1 Fixed header and table directory

All integers are fixed-width little-endian fields. ZXE v1 uses a fixed 128-byte header:

| Offset | Width | Field | Rule |
|---:|---:|---|---|
| 0 | 4 | `magic` | fixed `ZXE\0` |
| 4 | 2 | `format_version` | fixed 1 |
| 6 | 2 | `machine` | `0x003E`, x86-64 |
| 8 | 2 | `header_size` | fixed 128 |
| 10 | 2 | `flags` | bit 0=SHARED; all other bits must be 0. A SHARED file is a library and `entry_rva=0`; a non-SHARED file is an executable program and its entry point is valid |
| 12 | 2 | `directory_entry_size` | fixed 24 |
| 14 | 2 | `directory_count` | no more than 16 |
| 16 | 8 | `file_size` | must equal the actual file length, at most 256 MiB |
| 24 | 8 | `entry_rva` | entry point offset relative to the image base |
| 32 | 8 | `image_size` | minimum coverage length from RVA 0 to the end of the highest segment, at most 1 GiB |
| 40 | 8 | `directory_offset` | file offset of the table directory, 8-byte aligned |
| 48 | 16 | `build_id` | 128-bit build identifier; used to pair with debug files |
| 64 | 4 | `api_abi_major` | fixed at 1; execution is refused on mismatch |
| 68 | 4 | `reserved0` | must be 0 |
| 72 | 56 | `reserved1` | must all be 0 |

ZXE v1 files use a fixed physical order: the 128-byte header, the table directory, the metadata tables, then the segment payloads. Every region is 8-byte aligned, gap bytes must be zero, and optional unknown tables may be inserted into the metadata table area. The loader must still take the offsets and lengths from the directory and validate the ranges.

Table entry limits: 64 dependencies, 4096 imports, 4096 exports, 65536 relocations; the string table is at most 1 MiB. `entry_size` must equal the fixed structure size of the corresponding table. An empty table may have a directory entry with `count=0`.

Relocations are always 8-byte writes; `target_rva` must be 8-byte aligned and fall entirely inside a writable DATA or ZERO segment. Relocating into code, into read-only segments or outside segments is not allowed. The shared library dependency graph must be acyclic; loading is refused when a cycle is detected.

### 3.2 Native segment table

The segment table holds at most 32 entries, each a fixed 48 bytes, with the field order: `type:u32`, `flags:u32`, `file_offset:u64`, `file_size:u64`, `mem_size:u64`, `vaddr_rva:u64`, `alignment:u64`.

Segment type numbers: CODE=1, RODATA=2, DATA=3, ZERO=4. Permission bits are bit 0=R, bit 1=W, bit 2=X, and all other bits must be zero. CODE is fixed at R|X, RODATA is fixed at R, DATA and ZERO are fixed at R|W; every W|X combination is illegal. A ZERO segment has a file length of 0 and its memory is zeroed by the loader. All RVAs, memory sizes and alignment values are fixed at 4096 bytes; `alignment` can only be 4096. Segment RVA ranges must not overlap, and the entry RVA must fall inside a CODE segment.

PIE main programs and shared libraries use RVAs relative to their respective image base. Within a process, the main program and all dependent libraries together use the `[USER_LOAD_BASE, USER_LOAD_BASE + 1 GiB)` image window, and the combined image range must not exceed 1 GiB; each image's `image_size` must also not exceed 1 GiB. The kernel entropy source tries at most 64 randomly chosen 64 KiB-aligned bases and excludes the user stack, shared memory and other occupied regions; when no address is available it refuses to start and does not fall back to a fixed base. All offset, length and address additions must perform overflow and boundary checks.

### 3.3 Dynamic linking metadata

| Number | Type | Computed result |
|---:|---|---|
| 1 | RELATIVE | `load_base + addend`; `symbol_index` must be 0 |
| 2 | 64 | `symbol_address + addend` |
| 3 | GLOB_DAT | `symbol_address` |
| 4 | JUMP_SLOT | `symbol_address` |

Unassigned relocation numbers are refused at load. A relocation entry is a fixed 24 bytes: `target_rva:u64`, `type:u32`, `symbol_index:u32`, `addend:i64`. Apart from RELATIVE, `symbol_index` indexes the import table; an out-of-range index is refused at load. RELATIVE does not reference a symbol; when a weak import is missing, only 64, GLOB_DAT and JUMP_SLOT are allowed and the symbol value counts as 0 (64 writes the addend, the other two write 0).

A dependency entry is a fixed 16 bytes: `name_offset:u32`, `abi_major:u32`, `flags:u32`, `reserved:u32`. All dependencies are required; `flags` and `reserved` must be 0. The name can only be a plain file name, the kernel loads only from `/lib`, and path traversal or overriding through an application directory is not allowed. A library name is bound to one ABI major version, and the dependency declaration and the library export must match.

An import table entry is a fixed 16 bytes: `dependency_index:u32`, `name_offset:u32`, `abi_major:u32`, `flags:u32`. `flags` bit 0 means WEAK and all other bits must be zero; loading is refused when a strong import is missing, and a missing weak import resolves to 0. An export table entry is a fixed 24 bytes: `name_offset:u32`, `abi_major:u32`, `value_rva:u64`, `size:u32`, `flags:u32`; export flags bit 0 means GLOBAL and the other bits are zero. `size` is the byte size of the symbol object; functions may use 0 to mean unknown size, and non-function objects must be greater than 0. Symbol names reference the string table; strings are UTF-8, NUL-terminated, at most 255 bytes long, and may not contain an embedded NUL. The import ABI major version must match that of the owning dependency; the export ABI major version must match the dependency declaration. Imports are matched exactly by dependency index, name and ABI major version; there must be exactly one match in the target library, and a duplicate export counts as a format error.

The ASLR base is chosen randomly from the kernel entropy source, with at most 64 attempts; if there is still no free non-overlapping region it refuses to start and does not fall back to a fixed base. `.zxe.debug` is a separate debug file format with its own magic, version, a matching 128-bit `build_id` and a DWARF 5 debug payload; when the ID does not match, the debug file is ignored, and the runtime loader does not parse debug information.

## 4. Loading and Validation Flow

The loader processes a ZXE file in this order:

1. Read and validate the magic, format version, architecture, file length and feature flags.
2. Validate that all tables and string references lie within the file boundaries, and check integer overflow, count limits and alignment.
3. Validate segment address ranges, inter-segment overlap, the entry point and permission combinations.
4. Establish the Ring3 address mappings for the process, map pages according to segment permissions, load file contents and zero uninitialised regions.
5. The kernel loader resolves required dependencies from the fixed `/lib`, verifies the ABI major version and maps the shared library segments.
6. The kernel loader resolves imports immediately and applies the whitelisted relocations; it refuses to start when it meets an unsupported or illegal relocation.
7. Verify that the final entry point lies in an executable user mapping, then start the program with Ring3 privileges.

A format parse failure, a missing dependency or a relocation failure should all abort loading before entering user mode; none of them may crash the kernel as a result. File path access is provided by ext4/VFS, and the ZXE format itself carries no disk partition or file system information.

## 5. System Boundaries and Component Relationships

```text
ext4
  ├── user programs: *.zxe
  ├── shared libraries: system ABI libraries, ZAF libraries and other user-mode libraries
  └── system runtime resources

Ring3
  ├── Zhaoxu Aqua Framework (ZAF) and the default Aqua theme
  ├── Aqua Desktop Environment, Desktop Window Manager, desktop compositor and applications

Yuanshuo
  ├── ZXE loader: image validation, dependency loading, immediate binding and relocation
  └── process address space, Ring3 entry, memory mapping, IPC and syscalls
```

ZXE defines only executable images and linking metadata; it does not prescribe ZAF's widget interfaces or theme resource format. ZAF's library ABI can be exposed to applications through the ZXE dynamic dependency mechanism; future themes or GUI frameworks can still use the same executable format.

Kernel images and boot programs use their own prescribed formats and boot paths. The kernel must not be stuffed into ZXE user program payloads, the boot sector or the ESP. The ESP keeps the FAT32 constraint, and ZXE user programs are not moved onto the ESP because of that constraint.

## 6. Compatibility and Migration

The repository currently has a static-segment-table ZXE and an ELF conversion tool. This document decides to redefine ZXE v1: a fixed magic `ZXE\0` with an independent version field of 1. The old static layout is incompatible with v1 here, and the new loader is not required to support the old layout; when implementing the migration, the packer, loader, samples and existing `.zxe` artifacts should be updated together so that the same magic/version never carries two interpretations.

The ELF intermediate is restricted to x86-64 ELF64 PIE (ET_DYN) accepting only PT_LOAD; the ELF interpreter and PT_DYNAMIC are forbidden. The static link stage resolves local symbols; the SHT_RELA entries that need runtime handling are converted by the packer into ZXE-specific relocation codes (1..4).

Dependencies, imports, exports and the manual relocation list are passed to the packer through non-loadable ELF custom sections whose payload is a UTF-8 JSON array:

- `.zxe.deps`: `[{"name":"libexample.zxe","abi_major":1}]`
- `.zxe.imports`: `[{"dependency_index":0,"name":"example_call","abi_major":1,"weak":false}]`
- `.zxe.exports`: `[{"name":"example_call","symbol":"example_call","abi_major":1,"type":"function","global":true}]`; `size` is optional and defaults to the ELF symbol size.
- `.zxe.relocs`: `[{"target_rva":8192,"type":"RELATIVE","symbol_index":0,"addend":0}]`; these are normally generated automatically by the packer from ELF SHT_RELA and do not have to be supplied by hand.

The `.zxe.*` non-loadable sections may be supplied by inline assembly in the source file or by objects passed in at link time; for example, use `.section .zxe.deps,"",@progbits` together with `.ascii` to write the UTF-8 JSON array bytes (with no terminating NUL). `.zxe.deps`, `.zxe.imports`, `.zxe.exports` and `.zxe.relocs` may all be omitted; omitting one means an empty table. Once the JSON records have been converted into native ZXE tables, the custom sections do not enter the runtime payload. The packer also converts SHF_ALLOC ELF SHT_RELA entries into the ZXE relocation table: x86-64 types 1/6/7/8 map to ZXE `64`/`GLOB_DAT`/`JUMP_SLOT`/`RELATIVE`; other dynamic relocation types are rejected by the packer. A non-RELATIVE ELF symbol name must uniquely match one `.zxe.imports` entry.

The system API `Zhaoxu Operating System Application Programming Interface Version 1.0` corresponds to `api_abi_major=1`, and the loader accepts only that major version. Shared library ABI major versions are matched per dependency entry. ZAF's library ABI is governed by a separate ZAF specification; ZXE carries only its ABI major version and does not bind the ZAF version to the system API ABI.

Published field semantics must not be changed silently in a future format version; an incompatible change requires a format version bump.

## 7. Confirmed Decisions and Open Questions

The following decisions were confirmed by the user and should govern later format implementation. Behaviour not listed as confirmed still has to be pinned down before coding, and the packer and loader must not each guess their own answer.

### 7.1 Confirmed v1 rules

- Fixed magic `ZXE\0` with a separate `format_version=1`; v1 is redefined and is not compatible with the old static ZXE layout.
- Little-endian, fixed-width integers; a 128-byte header with field offsets as in §3.1; every reserved field and undefined flag is zero.
- Fixed file order is header, table directory, metadata tables, segment payloads; every region is 8-byte aligned with zero-filled gaps. Table limits are 64 dependencies, 4096 imports, 4096 exports, 65536 relocations, and the string table is at most 1 MiB; `entry_size` must match the fixed entry size, and an empty table may use `count=0`.
- The shared library dependency graph rejects cycles; relocations uniformly write 8 bytes, and the target RVA is 8-byte aligned and falls entirely inside a writable DATA/ZERO segment.
- Files are at most 256 MiB; at most 32 segments.
- Header `flags` bit 0=SHARED; a shared library has `entry_rva=0`, an application image has a valid entry point; every other bit must be zero.
- All images share the 1 GiB process window, and neither a single image nor the combined images may exceed 1 GiB; ASLR uses the kernel entropy source with at most 64 attempts, and on failure refuses to start rather than falling back to a fixed base.
- A missing weak import is only allowed for 64/GLOB_DAT/JUMP_SLOT and counts as symbol value 0; RELATIVE references no symbol.
- Imports are matched exactly by dependency index, symbol name and ABI major version, must match uniquely in the dependency library, and a duplicate export is an error; the export `size` is the symbol object size, functions may use 0, and non-function objects must be greater than 0.
- `.zxe.debug` uses a separate header and carries a matching build ID, with a DWARF 5 debug payload; the loader ignores debug payloads.
- Segment entries are 48 bytes; types CODE=1, RODATA=2, DATA=3, ZERO=4; segment permissions are fixed at RX, R, RW, RW; all segment RVAs, memory sizes and `alignment` are 4 KiB aligned, segments must not overlap, and W+X is rejected.
- Dynamic linking is performed by the kernel loader with immediate binding. Relocation entries are 24 bytes; the ZXE-specific numbers are RELATIVE=1, 64=2, GLOB_DAT=3, JUMP_SLOT=4, and unassigned numbers are rejected.
- Libraries are looked up only in `/lib`, and dependency names may only be plain file names; all dependencies are required and the library ABI major version must match exactly. System API v1.0 corresponds to `api_abi_major=1` and must match exactly.
- Imports and exports use separate tables; import entries are 16 bytes and export entries 24 bytes; symbol names are UTF-8 NUL-terminated strings of at most 255 bytes with no NUL inside the name.
- Import flags bit 0 is WEAK and the other bits are zero; export flags bit 0 is GLOBAL and the other bits are zero. A missing strong import is rejected, a missing weak import resolves to 0.
- The external debug file is named `.zxe.debug`, uses a separate magic/version and carries the same 128-bit build ID; a mismatch means the file is ignored, and the runtime loader does not parse debug content.
- The first version requires no signing or compression; unconfirmed features must not be treated as v1 requirements.
- The ELF intermediate is x86-64 ELF64 PIE (ET_DYN) with PT_LOAD only; it contains no interpreter or PT_DYNAMIC. Dynamic metadata uses non-loadable `.zxe.*` JSON array sections encoded as in §6; the packer converts supported ELF SHT_RELA into ZXE-specific relocation codes.
- The system API ABI major version matches the OS exactly; the ZAF library ABI is managed independently and ZXE carries only its ABI major version.

### 7.2 Implementation status

Format v1 has landed in the repository:

- The kernel's `src/kernel/proc/zxe_loader.c` and `src/kernel/include/zxe_format.h` implement full validation of the header/table directory/segments/dependencies/imports/exports/relocations, immediate binding, PIE image mapping, W^X page permissions and 64 KiB-aligned ASLR (at most 64 attempts, refusing to start on failure). Shared library dependencies are loaded recursively from `/lib` and cyclic dependencies are rejected.
- The packer `src/tools/zxe-pack.rb` accepts x86-64 ELF64 PIE (PT_LOAD only), generates native tables from the non-loadable `.zxe.deps/.zxe.imports/.zxe.exports/.zxe.relocs` JSON sections, and converts supported ELF SHT_RELA into ZXE relocation codes.
- User programs are linked uniformly with `-fpie`/`-fPIC` at RVA 0 (`src/user/user.ld`), no longer using `-mcmodel=large`; the kernel enables EFER.NXE on both the UEFI and BIOS paths to support segment-level NX.

Validation tool and evidence:

- `src/tools/zxe-check.rb` validates any `.zxe` against this document (header constants, table boundaries and non-overlap, segment permissions and page alignment, image coverage, entry point inside CODE, writable relocation targets, UTF-8 and string length). All 11 artifacts pass.
- `src/tools/zxe-fixture-lib.rb` generates the minimal shared library `libztest.zxe`; `src/user/ztestmain.c` declares `.zxe.deps`/`.zxe.imports` and imports `ztest_value`. The `make run-iso QEMU_SMP=1` smoke log shows `[ztest] imported ztest_value() = 42` and `[ztest] exit 0`, after which the Aqua Desktop Environment desktop and the welcome/calc/install clients behave normally, proving that dependency loading, export lookup, JUMP_SLOT relocation and the ASLR base work together.
- Known limitation: the repository's existing UEFI `QEMU_SMP=4` AP bring-up halts after bringing up the first AP in some runs, unrelated to ZXE; the single-core UEFI and the BIOS paths are both fine. That problem needs a separate fix.

## 8. Implementation Acceptance Direction

Once the format reaches implementation, at least the following must be validated:

- Segment mapping, BSS zeroing and Ring3 entry execution for a valid native ZXE.
- Truncated files, out-of-bounds tables, integer overflow, invalid addresses, overlapping segments and wrong permissions are all rejected.
- Missing dependencies, missing symbols, ABI mismatches and unsupported relocations all fail explicitly.
- When several programs share read-only library mappings, permissions are correct and writable data does not bleed between them.
- User programs can be read, executed and updated from the ext4 primary file system; the FAT32 compatibility path does not change the invariant that the ESP holds no kernel.
- ZAF and applications using ZAF run as ordinary Ring3 programs/libraries, and the kernel links no GUI implementation.

This document records design goals and does not mean the acceptance items above have already passed.
