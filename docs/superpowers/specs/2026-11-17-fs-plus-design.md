# FS+ — Native File System Design

Date: 2026-11-17
Status: Design proposal
File system name: `FS+` (exact spelling; see §1.4)

## 1. Scope

### 1.1 What FS+ is

FS+ is the native file system of Zhaoxu OS. It is a 64-bit, extent-based,
metadata-journaling file system designed for the installed system volume. The
implementation lives in Ring 3 as `/bin/fs+`; the Yuanshuo kernel keeps only
VFS forwarding and block-device plumbing. The kernel never parses FS+ metadata.

The target feature set is deliberately smaller than ext4:

- 64-bit block and inode numbers, 4 KiB file-system blocks.
- Extent-based file data with sparse files and unwritten extents.
- B+tree directories with byte-exact, case-sensitive names.
- Metadata checksums (CRC32C), ordered-data journaling, fast crash recovery.
- POSIX file types, hard links, symbolic links, truncate, atomic rename, fsync.
- A repair tool that can rebuild free counts and the directory index from
  authoritative objects.

### 1.2 What FS+ is not

FS+ v1 has no copy-on-write, snapshots, subvolumes, compression, encryption,
deduplication, quotas, project quotas, reflinks, or online shrink. The on-disk
feature flags reserve room for these, but an FS+ v1 implementation must reject
an unknown incompatible feature instead of guessing.

### 1.3 Relationship to FAT32 and ext4

- FAT32 remains mandatory for the EFI System Partition and removable media.
  Limine reads FAT32 and ISO9660, so the ESP continues to carry `BOOTX64.EFI`,
  `limine.conf`, `KERNEL.ELF`, `KERNEL.BIN`, `MiSans.ttf` and `WALL.RGB`.
- ext4 remains fully supported through `/bin/ext4fs`, including read/write
  interoperability with Linux-produced volumes. Existing ext4 installs keep
  booting and remain writable.
- FS+ becomes the default root volume for new installations. It does not replace
  the ESP, and it does not remove the ext4 service.
- FS+ never appears in the boot chain. Limine has no FS+ driver and will not
  gain one; the kernel and its modules must stay on the FAT32 ESP.

### 1.4 Naming rule

The name is exactly `FS+`, with the two ASCII letters followed by the ASCII
plus sign. Do not expand it, translate it, add a Chinese name, or change the
plus sign. This rule is contractual and applies to documentation, UI strings,
partition labels, tooling output and commit messages. Volume labels may add a
role suffix (for example `ZHAOXUROOT`), but the file-system name itself stays
`FS+`.

## 2. Design Principles

1. **Validate before trust.** Every on-disk pointer, count and length is checked
   against the volume geometry before it is followed. A malformed image must
   produce an error, never a kernel or service crash.
2. **One authoritative structure per fact.** Block ownership lives in the block
   bitmap; inode ownership lives in the inode bitmap; directory membership lives
   in the directory B+tree; file mapping lives in the extent tree. Redundant
   counters are hints and are rebuilt by fsck.
3. **Atomic metadata changes.** A namespace operation either commits as one
   journal transaction or leaves no visible change.
4. **Data before metadata.** With ordered journaling, file data blocks reach the
   device before the transaction that exposes them commits. A crash must not
   expose stale disk contents as file data.
5. **Small kernel surface.** FS+ runs in Ring 3 and receives a block capability
   from the VFS. No FS+ byte reaches `kernel.elf`.

## 3. Volume Layout

All offsets are in 4096-byte FS+ blocks unless stated otherwise. Sectors are
512 bytes. A block number is 64-bit on disk; v1 implementations may cap usable
volumes at 2^48 blocks (1 ZiB) and must reject larger values rather than
truncate them.

```
block 0                  primary superblock
block 1                  backup superblock
block 2                  journal superblock
blocks 3 .. 2+J-1        journal ring (J blocks total, including block 2)
meta = align_up(2+J, 8)  primary group descriptor table (GDT)
meta + GDT               backup group descriptor table
group0 = align_up(meta + 2*GDT, 8)
group0 .. end            block groups, each 32768 blocks (128 MiB)
```

`J` is the journal length stored in the superblock: at least 1024 blocks
(4 MiB), at most 262144 blocks (1 GiB). Unless mkfs is given an explicit size,
it uses `clamp(total_blocks / 128, 1024, 16384)`, which gives a 4 MiB journal on
a 64 MiB volume and a 64 MiB journal once the volume is 8 GiB or larger. `GDT`
is `ceil(group_count * 64 / 4096)` blocks. Because `group_count` depends on
`group0`, mkfs computes the layout by iteration until `group0` and `GDT` stop
changing; the iteration is bounded by the volume size and must fail cleanly if
the remaining space cannot hold one group.

`sb_total_blocks` is the size of the whole volume. The GDT describes only
groups that have at least 259 usable blocks. A trailing remainder smaller than
259 blocks is not part of any group: it stays unallocated, is never reachable
through a bitmap, and is excluded from `sb_free_blocks`. This avoids a group
whose own metadata would not fit. If the space before that remainder cannot
hold any group, mkfs refuses the volume.

### 3.1 Block group

A full group covers 32768 blocks. Groups are numbered from 0. The last group may
be partial. Each group begins with its metadata:

```
group_start + 0                    group header block
group_start + 1                    block bitmap (4096 bytes, 32768 bits)
group_start + 2                    inode bitmap (512 bytes used, 4096 bits)
group_start + 3 .. +258            inode table (4096 inodes * 256 bytes)
group_start + 259 ..               data blocks
```

The group header block contains a copy of the group descriptor, the group
number, the FS+ magic and a CRC32C. The inode table holds 4096 inodes; inode
numbers are `group * 4096 + index`, where inode 0 is reserved and inode 1 is the
root directory in group 0. A group whose usable block count is smaller than 259
is invalid; mkfs must leave such tail space unallocated rather than create a
short group.

The block bitmap covers the entire 32768-block group. Bits for blocks that do
not exist in a partial last group, and bits for metadata blocks, are set to 1
(used) and never allocated. The inode bitmap covers all 4096 inode slots; bits
for slots outside the volume's `total_inodes` are set to 1.

### 3.2 Superblock

The primary superblock is block 0. Block 1 holds an identical copy with
`sb_backup = 1`; the copy is refreshed by mkfs, fsck and clean unmount. Mount
uses the primary when its checksum and geometry validate, otherwise it tries the
backup and marks the volume as needing fsck.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x000 | 8 | `sb_magic` | `"FS+1\0\0\0\0"` |
| 0x008 | 4 | `sb_version_major` | 1 |
| 0x00C | 4 | `sb_version_minor` | 0 |
| 0x010 | 4 | `sb_block_size` | 4096 |
| 0x014 | 4 | `sb_blocks_per_group` | 32768 |
| 0x018 | 4 | `sb_inodes_per_group` | 4096 |
| 0x01C | 4 | `sb_inode_size` | 256 |
| 0x020 | 4 | `sb_journal_start` | 2 |
| 0x024 | 4 | `sb_journal_blocks` | J, including the journal superblock |
| 0x028 | 8 | `sb_total_blocks` | volume size in blocks |
| 0x030 | 8 | `sb_free_blocks` | hint; fsck recomputes |
| 0x038 | 8 | `sb_total_inodes` | group_count * 4096 |
| 0x040 | 8 | `sb_free_inodes` | hint; fsck recomputes |
| 0x048 | 8 | `sb_root_inode` | 1 |
| 0x050 | 8 | `sb_first_group_block` | absolute block of group 0 |
| 0x058 | 4 | `sb_group_count` | number of groups |
| 0x05C | 4 | `sb_gdt_start` | primary GDT block |
| 0x060 | 4 | `sb_gdt_blocks` | GDT length in blocks |
| 0x064 | 4 | `sb_backup_gdt_start` | backup GDT block |
| 0x068 | 4 | `sb_state` | bit 0 clean, bit 1 error, bit 2 needs fsck |
| 0x06C | 4 | `sb_mount_count` | mounted read-write count |
| 0x070 | 8 | `sb_last_mount_sec` | UTC seconds |
| 0x078 | 8 | `sb_last_write_sec` | UTC seconds |
| 0x080 | 8 | `sb_last_check_sec` | UTC seconds |
| 0x088 | 4 | `sb_inode_hint` | allocation hint |
| 0x08C | 4 | `sb_checksum_type` | 1 = CRC32C |
| 0x090 | 16 | `sb_uuid` | volume UUID |
| 0x0A0 | 16 | `sb_label` | UTF-8, NUL padded |
| 0x0B0 | 4 | `sb_incompat` | incompatible feature bits |
| 0x0B4 | 4 | `sb_ro_compat` | read-only-compatible bits |
| 0x0B8 | 4 | `sb_compat` | compatible bits |
| 0x0BC | 4 | `sb_backup` | 0 primary, 1 backup |
| 0x0C0 | 8 | `sb_mkfs_sec` | UTC seconds |
| 0x0C8 | 4 | `sb_checksum` | CRC32C of bytes 0x000..0x0C7 |
| 0x0CC | 4 | `sb_reserved0` | zero |
| 0x0D0 | 3888 | reserved | zero |

Multi-byte fields are little-endian. The superblock is 4096 bytes; a reader must
not assume the C compiler’s struct padding matches this table.

Feature bits:

| Set | Bit | Name | Meaning |
|---|---:|---|---|
| incompat | 0 | `64BIT` | 64-bit block numbers are in use |
| incompat | 1 | `EXTENTS` | file data is described by extents |
| incompat | 2 | `DIR_BTREE` | directories are B+trees |
| incompat | 3 | `JOURNAL` | journal is present and must be replayed |
| incompat | 4 | `CHECKSUM` | CRC32C metadata checksums are mandatory |
| ro_compat | 0 | `INLINE_DATA` | small files may store data in the inode |
| ro_compat | 1 | `INLINE_XATTR` | small xattrs may be inline |
| ro_compat | 2 | `SPARSE` | holes are represented by absent extents |
| compat | — | none | v1 defines no compatible-only features |

An unknown incompat bit makes the volume unmountable. An unknown ro_compat bit
makes it mountable read-only. An unknown compat bit is ignored. Version 1.0
requires all incompat bits 0–4, and sets ro_compat bits 0–2 when those features
are present.

All checksums use CRC32C with the reflected polynomial `0x82F63B78`, an initial
value of `0xFFFFFFFF`, and a final XOR of `0xFFFFFFFF`. The directory name hash
uses the same function over the raw name bytes with no seed. Checksums that are
seeded with the volume UUID or an inode number feed those bytes into the CRC
before the object bytes, in little-endian order.

### 3.3 Group descriptor

Each descriptor is 64 bytes; the GDT is an array indexed by group number.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `gd_start` | first block of the group |
| 0x08 | 8 | `gd_free_blocks` | hint; bitmap is authoritative |
| 0x10 | 8 | `gd_free_inodes` | hint; bitmap is authoritative |
| 0x18 | 4 | `gd_block_bitmap` | bitmap block number |
| 0x1C | 4 | `gd_inode_bitmap` | bitmap block number |
| 0x20 | 4 | `gd_inode_table` | first inode-table block |
| 0x24 | 4 | `gd_group_blocks` | usable blocks in this group |
| 0x28 | 4 | `gd_used_dirs` | directory inode count hint |
| 0x2C | 4 | `gd_flags` | zero in v1 |
| 0x30 | 4 | `gd_checksum` | CRC32C |
| 0x34 | 12 | reserved | zero |

`gd_checksum` covers the descriptor with the checksum field zeroed, seeded with
the volume UUID and the group number. The bitmap and inode-table pointers must
fall inside `[gd_start, gd_start + gd_group_blocks)`, and the metadata region
must not overlap the journal or the GDT.

The group header block is a self-checking copy used when the GDT is damaged:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 4 | `gh_magic` | `"GRP1"` |
| 0x04 | 4 | `gh_group` | group number |
| 0x08 | 64 | `gh_descriptor` | copy of the group descriptor |
| 0x48 | 4 | `gh_checksum` | CRC32C of the header with this field zeroed, seeded with the UUID and group number |
| 0x4C | 4020 | reserved | zero |

## 4. Inodes

An inode is exactly 256 bytes. Inode 1 is the root directory. Unallocated inode
slots are zeroed.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 2 | `i_mode` | POSIX type and permission bits |
| 0x02 | 2 | `i_flags` | inode flags (below) |
| 0x04 | 4 | `i_uid` | owner user id |
| 0x08 | 4 | `i_gid` | owner group id |
| 0x0C | 4 | `i_links` | hard-link count; 0 means freed or orphaned |
| 0x10 | 8 | `i_size` | file size in bytes |
| 0x18 | 8 | `i_blocks` | allocated 512-byte sectors: data, extent-tree or directory-node blocks |
| 0x20 | 8 | `i_atime_sec` | UTC seconds |
| 0x28 | 4 | `i_atime_nsec` | 0..999999999 |
| 0x2C | 4 | `i_mtime_nsec` | 0..999999999 |
| 0x30 | 8 | `i_mtime_sec` | UTC seconds |
| 0x38 | 8 | `i_ctime_sec` | UTC seconds |
| 0x40 | 4 | `i_ctime_nsec` | 0..999999999 |
| 0x44 | 2 | `i_extent_count` | inline extent count, 0..4 |
| 0x46 | 2 | `i_xattr_size` | inline xattr bytes, 0..64 |
| 0x48 | 8 | `i_btime_sec` | creation time, UTC seconds |
| 0x50 | 4 | `i_btime_nsec` | 0..999999999 |
| 0x54 | 4 | `i_generation` | NFS-style generation |
| 0x58 | 4 | `i_data_flags` | zero in v1 |
| 0x5C | 4 | `i_checksum` | CRC32C |
| 0x60 | 96 | `i_extents[4]` | four 24-byte inline extents; for a directory, entry 0 is the B+tree root pointer |
| 0xC0 | 64 | `i_inline` | inline data or xattrs |

`i_flags`:

| Bit | Name | Meaning |
|---:|---|---|
| 0x0001 | `INLINE_DATA` | `i_inline` holds file data; `i_size <= 64` |
| 0x0002 | `EXTENT_TREE` | `i_extents[0].physical` points to the root extent-tree block |
| 0x0004 | `INLINE_XATTR` | `i_inline` holds xattrs, not file data |
| 0x0008 | `DIR_HASHED` | directory nodes contain valid name hashes and `i_extents[0]` is the root-node pointer; set on every v1 directory |

`INLINE_DATA` and `INLINE_XATTR` are mutually exclusive. A regular file with
`i_size == 0` has neither flag and no extents. A directory sets `DIR_HASHED`,
sets `i_extent_count = 1`, and stores its root-node block in
`i_extents[0].e_physical`; `e_logical` is zero, `e_length` is one, `e_flags` is
zero and `EXTENT_TREE` is clear. Directory child pointers are physical block
numbers, so directory nodes are not mapped through file extents and do not need
the extent tree. An empty directory still needs one root leaf containing `.` and
`..`. Symbolic links whose target is at most 64 bytes use `INLINE_DATA`; longer
targets use extents. If an inode needs both inline data and xattrs, it must move
the data to extents and use `INLINE_XATTR`; v1 has no inode layout that stores
both in `i_inline`.

`i_checksum` is CRC32C over the 256-byte inode with the checksum field zeroed,
seeded with the volume UUID and the inode number. A bad checksum makes the inode
invalid; fsck reports it and never follows its extent pointers.

### 4.1 Extent format

Every extent record is 24 bytes, little-endian:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `e_logical` | first logical block in the file |
| 0x08 | 4 | `e_length` | number of blocks; 1..65536 |
| 0x0C | 4 | `e_flags` | bit 0: unwritten |
| 0x10 | 8 | `e_physical` | first physical block on the volume |

Extents describe regular files and symbolic-link targets. Directory inodes use
`i_extents[0]` only as the root-node pointer described above. Extents are sorted
by `e_logical` and must not overlap. A hole is any logical range not covered by
an extent; reads return zeros. An unwritten extent returns zeros until a write
converts the affected blocks to written extents. `e_length` is capped at 65536
blocks (256 MiB) so one extent stays within a bounded write. `e_physical +
e_length` must not exceed `sb_total_blocks`.

Inline extents cover up to four extents. When a file needs a fifth, the inode
sets `EXTENT_TREE`, sets `i_extent_count = 1`, and `i_extents[0]` becomes a root
pointer: `e_logical` and `e_length` are zero, `e_flags` is zero, and
`e_physical` is the extent-tree root block. The inode’s original extents are
moved into the tree in the same transaction.

## 5. Extent Tree

An extent-tree block is 4096 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 4 | `et_magic` | `"EXT1"` |
| 0x04 | 2 | `et_level` | 1 = leaf, 2..8 = index |
| 0x06 | 2 | `et_entries` | number of entries |
| 0x08 | 4 | `et_checksum` | CRC32C of the whole block with this field zeroed |
| 0x0C | 4 | reserved | zero |
| 0x10 | 4080 | entries | 24 bytes each; 170 maximum |

Leaf entries use the extent format from §4.1. Index entries use:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `ei_logical` | smallest logical block in the child |
| 0x08 | 8 | `ei_child` | child block number |
| 0x10 | 4 | `ei_entries` | child entry count, a validation hint |
| 0x14 | 4 | reserved | zero |

Index keys are strictly increasing. Lookup descends to the child with the
greatest `ei_logical <= key`; if the target precedes the first index key, the
target is a hole before the first extent and the lookup stops without reading a
child. At a leaf, the containing extent is selected, or the target is reported
as a hole. The root level is capped at 8; a deeper tree is corruption. Every
node must have a valid checksum, a level one greater than its children, and
child keys consistent with its own keys.

Allocation and split rules:

- A leaf split promotes the first key of the new right leaf to the parent.
- A root split creates a new root block and increments depth.
- Merging is optional in v1; fsck may compact underfull nodes but must not
  change the logical mapping.
- Extent-tree blocks are allocated from the same block bitmap as data and are
  charged to `i_blocks`.

## 6. Directories

### 6.1 Node format

A directory is a B+tree of individually allocated node blocks. The directory
inode’s `i_extents[0].e_physical` names the root node, and every child pointer
is a physical block number; node blocks are not file-data extents. The root
node is replaced by allocating a new block and updating that pointer, so a root
split never has to remap logical file blocks.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 4 | `dn_magic` | `"DIR1"` |
| 0x04 | 2 | `dn_level` | 0 = leaf, 1..8 = index |
| 0x06 | 2 | `dn_entries` | entry count |
| 0x08 | 4 | `dn_checksum` | CRC32C |
| 0x0C | 4 | reserved | zero |
| 0x10 | 4080 | entries | variable length |

Leaf entry:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 4 | `de_name_hash` | CRC32C of the name bytes |
| 0x04 | 8 | `de_inode` | inode number |
| 0x0C | 1 | `de_type` | 1 regular, 2 directory, 3 symlink, 4 char, 5 block, 6 fifo, 7 socket |
| 0x0D | 1 | `de_name_len` | 1..255 |
| 0x0E | 2 | reserved | zero |
| 0x10 | variable | `de_name` | raw name bytes |
| ... | 0..3 | padding | zero to a 4-byte boundary |

Index entry:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 4 | `di_name_hash` | hash of the separator name |
| 0x04 | 8 | `di_child` | child block number |
| 0x0C | 1 | `di_name_len` | 1..255 |
| 0x0D | 3 | reserved | zero |
| 0x10 | variable | `di_name` | separator name bytes |
| ... | 0..3 | padding | zero to a 4-byte boundary |

An index entry is the first key of its child, and the key is the full pair
`(di_name_hash, di_name)`; separators are strictly increasing. This makes a
split inside a run of equal hashes unambiguous: the promoted separator carries
the name, so lookup never has to guess which side of a hash collision it needs.
The child pointer must be a valid directory node one level lower. Every child
has the parent’s level minus one, and the root level is capped at 8. Directory
node checksums cover the block with the checksum field zeroed and are seeded
with the directory inode number and node block number.

### 6.2 Names

- Names are byte strings, not Unicode strings. FS+ does not normalize, case-fold
  or validate UTF-8. The upper layers may display them as UTF-8.
- The only forbidden bytes are `0x00` and `/`. Length is 1..255 bytes.
- `.` and `..` are real leaf entries in every directory and are maintained by
  mkdir, rmdir and rename. A rename that changes a parent must update the child
  directory’s `..`.
- Ordering is by the full key `(name_hash, name bytes)`; comparisons of names
  use unsigned byte values, shorter prefixes sort before longer names. Hash
  collisions are resolved by comparing the full name, and the index separator
  contains the full name, so a collision never makes the routing ambiguous.
- Lookup computes the key, descends through index entries to the child with the
  greatest separator key not greater than the target, then scans that leaf. If
  the target is smaller than the first separator, the name is absent. A leaf
  split promotes the first key of the new right leaf, name included, so the
  parent always has enough information to route both halves.
- The root directory (inode 1) is created by mkfs with `.` and `..` both pointing
  to inode 1.
- Directory `i_size` is the number of live entries as a 64-bit count, not bytes.
  This is a hint for `st_size`; the tree is authoritative.

### 6.3 Operations

| Operation | Transaction contents |
|---|---|
| `create` | allocate inode, set type/mode/owner/times, insert name in parent leaf, update parent counters |
| `mkdir` | as create plus allocate one leaf block with `.` and `..`, set links |
| `unlink` | remove leaf entry, decrement inode links, free inode and data/extent-tree blocks if links reaches 0 |
| `rmdir` | require only `.`/`..`, remove from parent, free every directory node and the inode |
| `rename` | validate, then remove destination entry if present, move source entry, update affected `..` and link counts, all in one transaction |
| `link` | increment target links, insert new name |
| `symlink` | allocate inode, store short target inline or in extents |
| `truncate` | free extents beyond the new size, zero the partial tail block, update `i_size`; extending creates a hole |

Rename over an existing directory is allowed only when the destination is an
empty directory. A rename must reject any move that would create a directory
cycle (destination inside the source subtree). The cycle check walks `..`
upward with a depth cap and validates every inode it visits.

`unlink` of an open file: the service keeps the inode alive while an open handle
exists, marks it unlinked in memory, and frees it when the last handle closes.
If the system crashes in that window, the on-disk inode has `i_links == 0`;
fsck treats allocated inodes with zero links as orphans and frees them. FS+ v1
does not need an on-disk orphan list because this rule is sufficient to reclaim
the space.

## 7. Journal

### 7.1 Journal superblock

Block 2 is the journal superblock:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `js_magic` | `"FS+J\0\0\0\0"` |
| 0x08 | 4 | `js_version` | 1 |
| 0x0C | 4 | `js_block_size` | 4096 |
| 0x10 | 4 | `js_journal_blocks` | J, same as `sb_journal_blocks` |
| 0x14 | 4 | `js_head` | next free journal block, relative to block 2 |
| 0x18 | 4 | `js_tail` | oldest possibly-live transaction block |
| 0x1C | 4 | `js_sequence` | next transaction sequence |
| 0x20 | 4 | `js_flags` | zero |
| 0x24 | 4 | `js_checksum` | CRC32C of bytes 0x00..0x23 with field zeroed |
| 0x28 | 4056 | reserved | zero |

`js_head`, `js_tail` and record offsets are relative to journal block 2, in the
range `1 .. J-1`; offset 0 is the journal superblock. The ring treats the last
journal block as preceding offset 1. An empty journal has `js_head == js_tail`.
The writer must leave at least one block free, so `js_head` must never catch
`js_tail`; when the free run is too short for a transaction, checkpointing must
advance the tail before the transaction starts.

### 7.2 Transaction format

A transaction is a descriptor block, zero or more data blocks, and a commit
block:

Descriptor block (`"FS+J"` family, magic `"JDS1"`):

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 4 | `jd_magic` | `"JDS1"` |
| 0x04 | 4 | `jd_sequence` | transaction sequence |
| 0x08 | 2 | `jd_type` | 1 |
| 0x0A | 2 | `jd_count` | number of target records, 1..255 |
| 0x0C | 4 | `jd_checksum` | CRC32C of the whole descriptor block with this field zeroed |
| 0x10 | 16 | first record | records follow |
| ... | ... | records | 16 bytes each |

A target record is `{u64 target_block, u32 journal_offset, u32 flags}`. The
data block for a record is at `journal_offset`; flags bit 0 means the target
should be zeroed and no data block is stored, in which case `journal_offset` is
zero. Target blocks must be file-system metadata: GDT, group headers, bitmaps,
inode table, extent-tree nodes and directory nodes. File-data blocks, journal
blocks and the superblock are not valid targets; volume state is committed
separately after the journal transaction.

Commit block (`"JCM1"`): 32 bytes used, rest zero.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 4 | `jc_magic` | `"JCM1"` |
| 0x04 | 4 | `jc_sequence` | must match the descriptor |
| 0x08 | 4 | `jc_count` | number of target records |
| 0x0C | 4 | `jc_crc` | CRC32C over all descriptor and data blocks of the transaction |
| 0x10 | 4 | `jc_checksum` | CRC32C of the commit block with this field zeroed |
| 0x14 | 12 | reserved | zero |

### 7.3 Commit and recovery

Metadata journaling is always ordered:

1. Write dirty file-data blocks and flush them.
2. Allocate journal space for descriptor, data and commit blocks.
3. Write descriptor and metadata data blocks.
4. Write the commit block with `jc_crc`; flush the journal.
5. Checkpoint the metadata target blocks in place.
6. Advance `js_tail` past the committed transaction and flush the journal
   superblock.

Recovery scans from `js_tail` toward `js_head`, validating sequence numbers and
checksums. It applies complete transactions in sequence order and stops at the
first incomplete or invalid transaction; that transaction and everything after
it are discarded by setting `js_head` to its start. Replay is idempotent: a
target block written twice receives the same bytes. Mount replays whenever
`js_head != js_tail`, even if the superblock claims clean; a clean flag alone
does not prove that the journal ring is empty. After replay, the service writes
a clean journal superblock and updates the volume state.

`fsync` and `fdatasync` flush file data (when applicable), commit the journal,
checkpoint metadata and flush the device. The volume superblock is updated only
when the clean/dirty state or the free-space hints change, not on every
transaction.

Journal exhaustion blocks new transactions until checkpointing advances the
tail; it must not overwrite live journal blocks.

## 8. Allocation

- Block allocation is first-fit within the target group, starting from a
  rotating hint. New file data prefers the inode’s group, then nearby groups.
- Directory and extent-tree metadata prefers the inode’s group.
- Inode allocation uses the inode bitmap and the group’s `gd_free_inodes`.
- A block or inode bit is set before the object that references it becomes
  visible. A failed transaction restores the bits in the same journal
  transaction, so recovery never sees a half-applied allocation.
- A block freed by an uncommitted transaction is not reused until that
  transaction has committed and been checkpointed. Reusing it earlier would let
  a crash replay stale metadata over live data.
- Free counts are updated as hints; fsck recounts bitmaps and resets them.
- ENOSPC is returned before any partial transaction commits. A write that
  cannot reserve all required blocks and journal space fails without changing
  the file size or extents.
- Preallocation, delayed allocation and multi-block reservations beyond one
  transaction are not part of v1.

## 9. Extended Attributes

FS+ v1 stores xattrs only inline in `i_inline`, using at most 64 bytes total.
Each entry is `{u16 name_len, u16 value_len, u32 type, name bytes, value bytes}`
padded to a 4-byte boundary, terminated by a zero `name_len`. Names are not
NUL-terminated on disk. External xattr blocks are a reserved future feature and
must be rejected as unknown incompat if ever introduced. ACLs and security
labels therefore do not fit in v1 storage; the `i_mode/i_uid/i_gid` fields cover
the current system, which has no user-account separation yet.

## 10. POSIX Semantics

| Topic | FS+ v1 behavior |
|---|---|
| File types | regular, directory, symlink, char device, block device, FIFO, socket |
| Permissions | mode, uid and gid are stored and returned by stat; current VFS has no account enforcement, so the service does not deny access by owner |
| Timestamps | atime, mtime, ctime, btime with second and nanosecond fields; noatime-style updates are not required, but atime updates may be deferred and coalesced |
| Hard links | supported for non-directories; `i_links` max 2^32-1 |
| Symlinks | target up to 64 bytes inline, longer targets in extents; `readlink` never follows |
| rename | atomic replacement of a file, atomic move of an empty directory, cycle rejection |
| fsync | ordered journal commit plus device flush |
| truncate | frees extents, zeroes the partial tail, creates holes on extension |
| sparse | holes read as zero; `i_blocks` reports only allocated sectors |
| mmap | not part of FS+ v1; the VFS maps memfd objects today and FS+ files are read/written through the service |
| case | case-sensitive, byte-exact |
| max name | 255 bytes |
| max file size | 2^63-1 bytes, bounded in practice by volume size |
| max volume | 2^48 blocks (1 ZiB) enforced by v1 |
| max symlink target | 4095 bytes |
| max hard links | 2^32-1 |
| max path | whatever the VFS accepts; the FS+ service validates each component independently |

Directory `st_size` returns the live entry count. `st_blocks` returns
`i_blocks`. `st_blksize` returns 4096. `statfs` reports total/free blocks and
inodes from the superblock and group hints, after validating them against the
volume geometry.

## 11. Ring 3 Integration

### 11.1 Service

`/bin/fs+` implements the same VFS request window used by `/bin/ext4fs`:
one control page plus the current 4 KiB data window (`FS_DATA_SIZE`), with a
state machine of `IDLE -> REQUEST -> DONE -> IDLE`. The existing `struct fs_rpc`
and `FS_OP_*` operations are the starting point. A move to a 64 KiB window is a
separate ABI change and is not assumed here; FS+ must stay correct with 4 KiB
requests and chunk larger transfers. FS+ needs these additional operations, and
the kernel VFS needs matching wrappers:

| Operation | Purpose |
|---|---|
| `MKDIR` | create a directory |
| `RMDIR` | remove an empty directory |
| `RENAME` | atomic move/replace, with source and destination paths |
| `READLINK` | copy a symlink target into the data window |
| `SYMLINK` | create a symlink |
| `LINK` | create a hard link |
| `TRUNCATE` | set file size |
| `FSYNC` | commit and flush |
| `STATFS` | volume statistics |
| `PROBE` | validate an FS+ superblock on a candidate partition |

The control page path fields are 256 bytes in the current ABI. `RENAME` needs
two paths, so the control page gains a second 256-byte destination field. Paths
never share memory with file data.

### 11.2 Block capability

FS+ must not depend on a process-name whitelist. The kernel already has a
trusted block path for `/bin/ext4fs`; the design moves that path to an explicit
capability:

- The VFS creates a block capability for one partition range when it starts a
  storage service.
- The capability is passed in the service’s spawn argument and is required by
  `blk_read`, `blk_write` and `blk_flush`.
- Only the VFS can mint the capability. A service cannot widen its range,
  read another volume, or hand the capability to an unrelated process.
- `/bin/ext4fs` moves to the same mechanism. During the transition, the kernel
  may accept the old process-name check for ext4fs only, but new code must not
  extend that exception to FS+.

This keeps the kernel free of FS+ metadata while preserving the one-service,
one-volume rule.

### 11.3 Mount and fallback

The VFS storage startup sequence is:

1. Start `/bin/fs+` with the candidate root partition and ask `PROBE`.
2. If FS+ validates, mount it at `/mnt` through the service.
3. If not, try `/bin/ext4fs` `MOUNT`.
4. If not, fall back to the kernel FAT32 path, then to the embedded `/data`
   mirror.

New installs create FS+ on the root partition, so step 1 succeeds on a normal
system. Existing ext4 disks continue through step 3. The ESP is never mounted
through FS+; it stays on the FAT32 path.

The kernel must not start `/bin/fs+` before `/bin/drivers` reports its block
device ready. A service timeout marks the volume as having an I/O error and
returns `EIO` to VFS callers instead of blocking the desktop forever.

### 11.4 Boot and installer

- Limine continues to load `KERNEL.ELF`, `KERNEL.BIN`, `MiSans.ttf` and
  `WALL.RGB` from the FAT32 ESP. FS+ never holds boot-loader files.
- The installer formats the ESP as FAT32 and the root partition as FS+, then
  writes the boot payload to the ESP. The FS+ `FORMAT` operation creates the
  superblock, journal, GDT, bitmaps, inode table and root directory.
- The installer writes user-visible files through the FS+ service, not through
  a second in-kernel formatter.
- The installed root label remains `ZHAOXUROOT` so existing disk-role scanning
  and repair tooling keep working; the file-system magic, not the label, selects
  FS+.
- The optional `ZHAOXUDATA` partition stays FAT32 in v1 for interoperability
  with other systems. A future FS+ data volume requires a deliberate migration
  and is not part of this design.

### 11.5 Root pivot

The first FS+ milestone mounts FS+ at `/mnt` while `/` remains the initramfs
root. A later milestone may pivot the system directories to FS+ after the
storage service is ready. That pivot must preserve the recovery path: if the
FS+ service fails, the initramfs shell and `/data` mirror must still be
available.

## 12. Tools

| Tool | Role |
|---|---|
| `src/fs/fs+/` | OS-independent FS+ core: geometry, superblock, bitmaps, inodes, extents, directory B+tree, journal, allocator, checker |
| `src/apps/fs+/` | Ring 3 service: RPC loop, block capability calls, cache, mount/recovery |
| `mkfs.fs+` | Ring 3 and host-buildable formatter |
| `fsck.fs+` | Ring 3 and host-buildable consistency checker and repair tool |
| `src/tools/fs+-test.rb` | Ruby host-test driver: build images, run the core against a file-backed device, compare with `mkfs.fs+`/`fsck.fs+` |
| `src/tools/fs+-fault.rb` | Ruby fault-injection driver for journal replay tests |

Repository scripts are Ruby. The formatter and checker are C programs in the
tree, not shell scripts. Host tests build the same core sources for Linux with a
file-backed block-device shim; they must not fork a separate implementation.

`fsck.fs+` repair order:

1. Validate superblock and geometry; choose primary or backup.
2. Validate GDT checksums and group bounds.
3. Rebuild block and inode bitmaps from reachable objects, while permanently
   reserving the journal, both GDTs, every group header, bitmap and inode
   table, and any unallocated tail outside the groups.
4. Walk the directory tree from inode 1; detect cycles, duplicate names,
   bad `..`, wrong types and link-count mismatches.
5. Walk extent trees; reject out-of-range, overlapping or cyclic mappings.
6. Free allocated inodes with zero links (orphans).
7. Recompute free counts, write a new journal superblock and mark the volume
   clean.

Repair must be able to run read-only for diagnosis and must never write when it
cannot prove the metadata it is rewriting is safe.

## 13. Testing

### 13.1 Host tests

- Images: 64 MiB, 128 MiB, 1 GiB, and a sparse 16 TiB image to exercise 64-bit
  block numbers. One image with a partial last group; one with the minimum
  journal; one with the maximum journal.
- Operations: create/read/write/append/truncate, sparse writes, inline-to-extent
  promotion, extent-tree depth growth, directory splits, collisions in the name
  hash, rename across directories, hard links, symlinks, unlink of open files,
  ENOSPC, full inode table, maximum name length.
- After every scenario, `fsck.fs+ -n` must report clean.
- Fault injection: for every journal write point, drop all later writes, reopen
  the image and assert that replay produces either the old or the new state,
  and never a state that fails fsck.
- ASAN/UBSAN builds of the core; malformed images with corrupt counts, cyclic
  extent trees, bad checksums, overlapping extents and out-of-range block
  pointers must fail closed.

### 13.2 QEMU tests

1. Build a normal image and boot the ISO under OVMF and SeaBIOS.
2. Install to a blank GPT disk and to a blank MBR disk; both must create a
   FAT32 ESP and an FS+ root.
3. Mount FS+, create and read files under `/mnt`, reboot, verify contents.
4. Run `fsck.fs+` after a forced QEMU quit during a write; it must recover.
5. Verify `nm out/kernel.elf` has no FS+ symbols and no undefined symbols.
6. Replace the FS+ root with an ext4 image and confirm the ext4 fallback still
   mounts and writes.
7. Confirm the ESP contains only FAT32 boot files and no FS+ metadata.

### 13.3 Security checks

- The kernel must reject malformed RPC lengths, paths without terminators, and
  out-of-range offsets before copying to or from the data window.
- The service must validate every block number, group index, inode number,
  extent length and B+tree child pointer.
- Journal replay must reject target blocks outside metadata regions, duplicate
  targets in one transaction, and records that exceed the descriptor count.
- No recovery path may allocate memory proportional to an unvalidated on-disk
  count.

## 14. Milestones

### M0 — Specification and image tooling

- Freeze the on-disk structures in this document.
- Implement geometry, superblock, GDT, bitmaps, inode formatting, root
  directory creation, and a host-buildable `mkfs.fs+`.
- Implement read-only `fsck.fs+` checks for a freshly formatted image.
- Acceptance: the host matrix creates valid images and `fsck -n` passes.

### M1 — Read-only mount

- Superblock/backup selection, group validation, inode and extent reads,
  directory B+tree lookup and listing, symlink reads, checksum verification.
- Acceptance: all host read tests pass; malformed images fail closed; the QEMU
  service can list a prebuilt FS+ image.

### M2 — Writes and journal

- Block/inode allocation, extent creation and tree growth, directory insert and
  split, create/unlink/mkdir/rmdir/rename/link/symlink/truncate, journal commit
  and checkpoint, fsync, ordered data.
- Acceptance: full host write matrix passes `fsck -n`; fault injection recovers;
  QEMU writes survive reboot.

### M3 — Recovery and repair

- Journal replay on mount, fsck repair, orphan cleanup, free-count rebuild,
  read-only degradation on unrecoverable corruption.
- Acceptance: forced power-cut tests recover; `fsck.fs+` repairs every
  injected inconsistency that is repairable without data loss.

### M4 — Installer and VFS default

- Add the FS+ operations to the VFS RPC, add the block capability, start
  `/bin/fs+` before the ext4 fallback, and make the installer format FS+
  for new root volumes.
- Acceptance: a fresh GPT install and a fresh MBR install boot to the desktop,
  mount FS+ at `/mnt`, and leave the ESP intact; ext4 volumes still mount.

### M5 — Root pivot and performance

- Optional pivot of the installed system directories to FS+.
- Allocation locality, directory lookup cache, journal checkpoint tuning.
- Acceptance: boot time and file throughput are measured against the ext4
  path; no regression in the recovery tests.

## 15. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| B+tree directory bugs corrupt names | host matrix with split and collision cases; fsck rebuilds reachable names; checksum every node |
| Extent-tree corruption causes out-of-range I/O | validate every logical/physical/length combination; cap depth at 8; fault injection |
| Journal replay applies a partial transaction | commit checksum covers descriptor and data; sequence numbers; idempotent replay; tail/head validation |
| Ordered data exposure after crash | flush data before commit; test every write point; never expose metadata before data |
| Ring 3 service crash loses the volume | VFS marks EIO, keeps the initramfs fallback, and reports fsck-required; no kernel panic |
| Installer writes FS+ through an untrusted path | only the FS+ service formats and writes; the installer holds no FS+ parser |
| ext4 compatibility regresses | keep `/bin/ext4fs` and its tests; FS+ is tried first only when its magic validates |
| 64-bit arithmetic overflows | check every multiply/add against `sb_total_blocks` and `2^48`; use checked helpers |
| A future feature breaks old readers | incompat bits are mandatory; unknown bits refuse mount; ro_compat degrades read-only |

## 16. Explicit v1 Decisions

These choices are part of the design and are not left to implementation drift:

1. **Root pivot.** FS+ is mounted at `/mnt` in M4. The initramfs remains `/`
   through v1. A root pivot is a separate project because it must preserve the
   initramfs recovery shell, the `/data` fallback and the single-volume service
   model.
2. **Data volume.** `ZHAOXUDATA` stays FAT32 in v1. FS+ is the native root
   volume; creating FS+ data volumes without a migration plan would split the
   installer and repair paths.
3. **Xattr growth.** v1 stores only inline xattrs and rejects any external-xattr
   incompat bit. ACLs and security labels wait for the account model.
4. **Directory size.** `st_size` for a directory is the live entry count. Tools
   that need byte offsets must not treat it as a byte length.
5. **Block capability.** FS+ ships only with an explicit per-volume block
   capability. The old process-name exception may remain for `/bin/ext4fs`
   during the M4 transition, but it must not be extended to `/bin/fs+`.
6. **No snapshots in v1.** The journal and extent tree are designed so snapshots
   can be added later, but v1 does not claim copy-on-write or subvolume support.

---

Copyright © 2026 Nexsteaduser. All Rights Reserved.
