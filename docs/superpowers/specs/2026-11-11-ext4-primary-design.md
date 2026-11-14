# ext4 as the Primary File System — Design

Date: 2026-11-11
Status: Approved (user confirmed the direction)

## 1. Background and Goals

An installed Zhaoxu OS has to make ext4 its primary file system, keeping FAT32 only for
compatibility purposes, and the kernel must not appear in the boot sector or the ESP (boot
volume). The existing working tree already holds an uncommitted implementation, but it has
two hard defects:

- When legacy BIOS boots from the ISO it reads a floppy-emulation image through EDD, which
  SeaBIOS does not support, so booting fails.
- `make all` still copies `KERNEL.ELF` into `esp/EFI/BOOT/`.

Targets the user chose:

1. **Full ext4 Linux interoperability**: able to mount volumes produced by default
   `mkfs.ext4` parameters (JBD2 journal, metadata_csum, 64bit, flex_bg, htree,
   extents / indirect blocks), with `e2fsck -fn` clean after reads and writes.
2. **The full ext4 driver lives in Ring3**: a new `/bin/ext4fs` service; the kernel keeps
   only VFS forwarding and retains no ext4 implementation in the kernel (no dual
   implementation).
3. FAT32 serves only the ESP and legacy disk compatibility.

Non-goals: encryption, casefold, verity, bigalloc, DAX; quotas are handled only in the
"do no harm" sense — quota management is not implemented.

## 2. Disk Layout (install target disk)

MBR:

```
LBA 0        stage1 (512 bytes, incl. partition table)
LBA 1        stage2 (MBR; ≤32 KiB)
LBA 1+S..2047 gap (zeroed)
LBA 2048     ESP (FAT32, 64 MiB, label ZHAOXUEFI)
             contains only EFI/BOOT/BOOTX64.EFI
after that   ext4 root partition (label ZHAOXUROOT)
             contains KERNEL.ELF, KERNEL.BIN, WQY.TTC and user data
```

In the GPT layout stage2 moves to LBA 34 (after the header and entries at LBA1/2); the rest
is identical, with a backup GPT at the end.

Invariants:

- Kernel bytes appear only inside the ext4 partition. The stage2 parameter block holds only
  the LBA/byte count/sector count of `KERNEL.BIN`; neither the 512-byte boot sector nor
  stage2 contains the kernel.
- The ESP holds only the UEFI loader; `KERNEL.ELF` is no longer written into it.
- `KERNEL.BIN` must be a single contiguous extent (BIOS stage2 supports only one block
  table entry). `WRITE_CONTIG` in `ext4fs` is responsible for satisfying this; the
  installer verifies it and then patches the stage2 parameter block.

## 3. Boot Flow

### 3.1 UEFI

- ISO: `BOOTX64.EFI` reads `/install/KERNEL.ELF` and `/install/WQY.TTC` with a minimal
  ISO9660 reader (existing logic retained).
- Installed disk: `BOOTX64.EFI` reads `/KERNEL.ELF` and `/WQY.TTC` with a minimal read-only
  ext4 implementation inside the firmware (existing logic retained; add the 64bit group
  descriptor flag and ignore metadata_csum).
- The read-only implementation does not verify csum and does not replay the journal; the
  installer guarantees that a freshly written volume is clean.

### 3.2 legacy BIOS

- Installed disk: stage1 (MBR) → stage2 → reads `KERNEL.BIN` from ext4 by EDD using the
  parameter block.
- ISO: El Torito switches to **no-emulation**. `bios.img` contains only stage1 + stage2;
  the firmware loads the whole image at 0x7C00; stage1 detects the "already resident" flag
  and jumps straight to 0x8000. `KERNEL.BIN` sits at the disc root as an ordinary ISO9660
  file, and after the build a Ruby tool (rule 6) parses the El Torito directory and the
  ISO9660 directory records to patch its LBA into the stage2 parameter block inside the
  ISO. The script must verify that the file is a single extent, otherwise the build fails.

## 4. System Architecture

```
Ring3  /bin/ext4fs
         │ blk_read / blk_write / blk_flush (trusted syscalls)
         ▼
Kernel  VFS ── request page + 64 KiB data window ──► ext4fs
         │
         └─ dev_disk_read/write ──► /bin/drivers (AHCI, Ring3)

Bootloader  UEFI carries its own read-only ext4; BIOS uses an LBA block table
Installer   Kernel does MBR/GPT/ESP/stage2; the ext4 partition is handed to ext4fs
```

- `src/fs/ext4/` holds the OS-independent core: `ext4_io.h` defines the
  `read/write/flush/alloc/free/log` callbacks. The same source is used for:
  - `/bin/ext4fs` (Ring3)
  - the Linux host test fixture (file-mapped block device)
- The kernel no longer compiles ext4 sources; `install.c` no longer includes `ext4.h`.
- The kernel gives `/bin/ext4fs` the same identity mapping as `/bin/drivers` (decided by
  process name) so the service can reach the control page and data window the kernel
  allocated.
- Boot order: `dev_start()` → `fs_service_start()` waits for READY →
  `vfs_mount_data()` → Aqua Desktop Environment. With no volume the existing behaviour is
  kept (`/mnt` falls back to the initramfs `/data`).
- The FAT32 read/write implementation stays in the kernel for now as the legacy-disk
  compatibility fallback; ESP formatting also stays in the kernel. It can be migrated
  separately later and does not block this design.

## 5. Kernel ↔ ext4fs Protocol

The kernel allocates one control page and 16 data pages (64 KiB); the physical addresses
are passed to the service through `spawn_arg`. Both sides access them through the identity
mapping; the state machine is:

```
IDLE → REQUEST → DONE → IDLE
```

Control page:

```c
struct fs_rpc {
    volatile u32 magic;      /* 'ZFS1' */
    volatile u32 state;
    u32 op;
    i32 status;
    u32 flags;
    u32 length;              /* valid bytes in the data window */
    u64 offset;              /* offset within the file */
    u64 value0, value1;      /* FORMAT: base/sectors; CONTIG returns LBA/block count */
    char path[256];
    char label[17];
};
```

Operations:

| op | semantics |
|---|---|
| MOUNT | probe every disk partition, prefer the volume labelled `ZHAOXUROOT`; return volume info |
| STAT | path → is_dir/size |
| LIST | path → serialized entries (type+size+name), returned in chunks |
| READ | path+offset+len → data window |
| WRITE | path+offset+len ← data window (truncate/append chosen by flags) |
| UNLINK | delete a file |
| SYNC | flush metadata and the block device |
| FORMAT | disk/base/sectors/label → create a new ext4 volume |
| WRITE_CONTIG | write one contiguous file, returning its start LBA and block count |

The kernel allows only one outstanding request at a time (VFS lock); after a 5 s timeout it
marks the volume as having an I/O error. The path lives in the control page, separate from
the data window, which avoids squeezing a 300-byte path into a 64-byte IPC message.

## 6. Trusted Block I/O Syscalls

New syscalls (numbers continue after 37):

```
38 = blk_count()                       -> disk count
39 = blk_info(disk, *sectors, *port)   -> 0/-1
40 = blk_read(disk, lba, count, buf)   -> sector count/-1
41 = blk_write(disk, lba, count, buf)  -> sector count/-1
42 = blk_flush(disk)                   -> 0/-1
```

- Only the process named `/bin/ext4fs` holds `CAP_BLOCK`; calls from any other process
  return -1.
- The kernel validates the user buffer (`user_range_ok`) and reuses `dev_disk_read/write`
  internally, serialized by `g_dev_busy`, so two Ring3 processes cannot concurrently write
  the driver's shared page.
- At most 8 sectors (4 KiB) per call, matching the existing dev RPC limit.

## 7. ext4 Feature Matrix

### 7.1 Mounting and features

- Supported: `extents`, `filetype`, `64bit`, `flex_bg`, `meta_bg`,
  `sparse_super`/`sparse_super2`, `metadata_csum`, `uninit_bg`,
  `extra_isize`, `large_file`, `huge_file`, `dir_index`, `journal`,
  `orphan_file`, `inline_data` (read-only).
- Rejected (mount read-only and report an error): `bigalloc`, `encrypt`, `casefold`,
  `verity`.
- Journal: on mount, replay JBD2 if `s_errors`/the journal requires recovery (v2/v3
  checksums); the write path commits transactions as metadata journaling.
- Orphans: clear `s_last_orphan` and `orphan_file` on mount.

### 7.2 Reads

- File data: inline extents, the extent tree (including uninitialized extents and holes),
  indirect block mapping (ext2/3), inline_data.
- Directories: linear directories + htree (linear scan per leaf block; no dependence on the
  dx index).
- Metadata: verify csum (on mismatch the object is treated as corrupt rather than crashing).

### 7.3 Writes

- Allocator: 32/64 group descriptors, flex_bg metadata groups, bitmap updates and csum.
- Files: extent tree insert/split, indirect block allocation, truncate, holes.
- Directories: linear insert/delete/split; for htree directories, insert within the leaf
  block first, and when space runs out split and update the dx index.
- Metadata: full-chain csum updates for superblock, GDT, bitmaps, inodes, extent blocks,
  directory blocks and the journal.
- Journal: JBD2 descriptor/commit blocks, revoke, v3 checksums; write the journal before
  writing in place.
- `fsync`/`SYNC` map to journal commit + `blk_flush`.

### 7.4 Explicitly Out of Scope

Quota management, ACL/xattr writes, project quotas, encryption, compression, snapshots,
`mmp`.

## 8. Installer Flow

1. Scan the target disk and verify it is blank or already carries a Zhaoxu layout (existing
   logic).
2. Compute the layout: 64 MiB ESP, the remainder for the ext4 root.
3. Create ext4 through the `FORMAT` RPC (label `ZHAOXUROOT`).
4. `READ` this machine's `boot_info` font and running kernel image:
   - `WRITE` `/KERNEL.ELF` (for UEFI)
   - `WRITE_CONTIG` `/KERNEL.BIN` (for BIOS, returning LBA/block count)
   - `WRITE` `/WQY.TTC`
5. The kernel writes MBR/GPT, stage2 (patching the parameter block), the ESP (only
   `BOOTX64.EFI`), zeroes the gap and calls `blk_flush`.
6. Verify: the ESP is FAT32, the ext4 magic is present, stage1 and the parameter block agree.

When `ext4fs` needs the font it can call the `boot_font` syscall directly; the kernel can
also send the font along with `WRITE` as before. The two are equivalent.

## 9. VFS Behaviour

- `/` is still initramfs; `/mnt` is served by `ext4fs`.
- `vfs_mount_data()`: ask `ext4fs` for `MOUNT` first; on failure fall back to the kernel
  FAT32 implementation, and on failure again fall back to the `/data` image.
- `open/read/close` become streaming: an fd stores only the path, file size and offset;
  `read` issues chunked `READ` requests over the 64 KiB data window and copies straight
  into the caller's user buffer instead of pulling the whole file into the kernel heap, so
  the 16 MiB limit no longer applies (`vfs_read_all` stays for old paths that genuinely
  need the whole block, and its limit is unchanged).
- `write_file`/`unlink` go through `WRITE`/`UNLINK`.
- `readdir` goes through `LIST`; the kernel turns serialized entries back into the existing
  callback.
- Kernel fs syscalls return -1 while the service is not ready instead of panicking.

## 10. Build and Packaging

- `src/fs/ext4/`: core sources + `ext4_io.h`; not part of the kernel object list.
- New `src/apps/ext4fs/main.c`: service loop + block I/O callbacks; packaged into the
  initramfs as `/bin/ext4fs`, with `RAMFS_FILES` in the Makefile updated to match.
- `src/tests/ext4/`: host fixture; driven by `tools/ext4-test.rb` (rule 6).
- Kernel `C_SRCS` drops `kernel/fs/ext4.c` and `kernel/fs/ext4_write.c`.
- `make all` drops the `esp/EFI/BOOT/KERNEL.ELF` dependency and copy; `clean` drops it too.
- ISO: `bios.img` becomes a no-emulation stage1+stage2 image; `install/KERNEL.BIN` is an
  ordinary file; `tools/iso-bios.rb` patches the LBA after the build.
- `tools/bios-img.rb` no longer stitches the kernel into the boot image and outputs only
  stage1+stage2.

## 11. Test Strategy

### 11.1 Host fixture (before QEMU)

- Compile the ext4 core under Linux with a file-mapped block device.
- `tools/ext4-test.rb` generates images from a matrix:
  - file systems: ext2, ext3, ext4
  - block sizes: 1 KiB, 2 KiB, 4 KiB
  - options: `-O 64bit`, `-O metadata_csum`, `-O flex_bg`,
    `-O meta_bg`, `-O ^has_journal`, `-O dir_index`
- Each round: read every file → write new files/overwrite/delete/mkdir → `sync` →
  `e2fsck -fn`, and assert that the content visible through `debugfs` matches.
- Fault injection: drop subsequent writes at each stage of journal commit; after remount
  the volume must either replay or come back clean.
- Build with ASAN/UBSAN.

### 11.2 QEMU regression

1. `make -j4`; `nm out/kernel.elf | grep ext4_` must be empty; no undefined symbols.
2. ISO: both UEFI and BIOS reach the desktop, and the log shows the kernel coming from
   `/install`.
3. Install: `make run-install` (GPT) and `make run-install-bios` (MBR), or headless 8sh
   `install mbr|gpt`.
4. Boot afterwards from the target disk alone: both UEFI and BIOS show
   `Kernel: /KERNEL.ELF from ext4.` / the stage2 parameter read, plus
   `[ext4fs] mounted "ZHAOXUROOT"`, and Chinese text renders correctly.
5. List the ESP with `mtools` and assert it contains only `EFI/BOOT/BOOTX64.EFI`; read
   LBA0/1 with `dd` and assert no kernel ELF magic is present.
6. In 8sh, write/read/delete a file under `/mnt`; the content is still there after reboot.
7. Swap the target disk for an image pre-formatted with default `mkfs.ext4` parameters and
   verify the read-only mount.

## 12. Milestones and Acceptance

### M0 Service skeleton and de-kernelisation

- Extract the `src/fs/ext4/` core, keeping current functionality; the host fixture can run
  against the current volume.
- Add `/bin/ext4fs`, the RPC, VFS forwarding and the block I/O syscalls.
- Switch the installer to the RPC; delete the kernel ext4 sources and the ESP kernel copy.
- Fix BIOS ISO booting (El Torito no-emulation + LBA patching).
- Acceptance: UEFI/BIOS install and reboot to the desktop; the ESP holds only the loader;
  `nm` shows no ext4.

### M1 Read-only interoperability

- JBD2 replay, 64bit, flex_bg/meta_bg, indirect blocks, extent tree,
  csum verification, htree, inline_data, sparse files.
- Acceptance: the whole host matrix reads correctly, `e2fsck -fn` reports no errors.

### M2 Write interoperability

- Allocator, extent tree growth, directory split/htree insert, indirect block writes,
  full-chain csum, JBD2 commit, orphan handling.
- Acceptance: `e2fsck -fn` is clean after reads and writes across the host matrix;
  power-cut injection is recoverable.

### M3 Hardening

- Large files/directories, ENOSPC semantics, read-only degradation, error injection,
  performance baseline.
- Acceptance: fault injection and stress tests pass.

### M4 Documentation and release

- Update AGENTS.md (layout, validation, architecture), README, CHANGELOG.
- Full QEMU smoke checklist.

## 13. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| JBD2 replay/commit is complex | M1 is read-only replay first, M2 adds writes; host fault injection |
| htree splits and csum | leaf-block insert first, split later; `e2fsck -fn` at every step |
| ISO patching depends on a contiguous file | the build script enforces a single extent and aborts on failure |
| RPC round trips are slow | 64 KiB data window + batched reads; concurrent requests can come later |
| Installer fails after ext4 leaves the kernel | one-shot switch in M0, backed by headless install regression |
| Legacy FAT32 disk compatibility regresses | keep the kernel FAT32 fallback, marked as a compatibility path |

## 14. File Layout

```
src/fs/ext4/           OS-independent ext4 core (read, write, journal, csum)
src/apps/ext4fs/       Ring3 service (main.c, rpc.c, blk.c)
src/kernel/fs/vfs.c    VFS forwarding and fallbacks
src/kernel/proc/fsrpc.c control page/data window, waiting and timeouts
src/kernel/proc/dev.c  existing driver RPC (block I/O reused)
src/kernel/proc/syscall.c  new blk_* and CAP_BLOCK checks
src/kernel/fs/install.c    layout + ESP + stage2, ext4 handed to the service
src/boot/bios_stage1.asm   support for the no-emulation direct jump
src/boot/bios_stage2.asm   parameter-block read (EDD/CHS adaptive)
src/tools/iso-bios.rb      ISO9660 + El Torito patching
src/tests/ext4/            host fixture
src/tools/ext4-test.rb     host matrix test
```
