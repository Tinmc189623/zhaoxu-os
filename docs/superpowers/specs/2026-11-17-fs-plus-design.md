# FS+ — Native Copy-on-Write File System Design

Date: 2026-11-17
Status: Design proposal
File system name: `FS+` (exact spelling; see §1.4)

## 1. Scope

### 1.1 What FS+ is

FS+ is the native file system of Zhaoxu OS. It is a 64-bit, copy-on-write
(CoW) B+tree file system for the installed system volume. Every committed
metadata or data change creates new blocks and atomically switches the root
tree pointer in a superblock slot; nothing referenced by the previous root is
modified in place. The implementation lives in Ring 3 as `/bin/fs+`; the
Yuanshuo kernel keeps only VFS forwarding and block-device plumbing. The kernel
never parses FS+ metadata.

The target feature set:

- 64-bit block numbers, 4 KiB blocks, 4 KiB B+tree nodes.
- Copy-on-write metadata and file data, with a single atomic root commit.
- Checksummed superblocks, tree nodes and per-block data checksums.
- Writable subvolumes, each with its own inode namespace and next-inode
  counter, plus read-only snapshots that share the source blocks.
- A per-block reference-count tree so shared blocks survive snapshot deletion
  and are reclaimed only when the last root drops them.
- One FS tree per subvolume for inodes, directory entries, extents and xattrs.
- Sparse files, unwritten extents, inline small files, hard links and symlinks.
- POSIX namespace operations: create, unlink, mkdir, rmdir, rename, link,
  symlink, truncate, fsync, stat and statfs.
- Crash consistency without a journal: the previous superblock root remains
  valid until the new root is complete and flushed.

### 1.2 What FS+ is not

FS+ v1 has no journal, no reflink ioctl, no compression, no encryption, no
deduplication, no quotas and no online shrink. Snapshots are read-only; a
writable clone of a snapshot is not part of v1. Snapshot creation and deletion
walk the source FS tree and are O(reachable blocks), because the reference
counts are maintained per block. A future lazy shared-root scheme can remove
that cost without changing the on-disk tree format, but v1 does not claim it.

### 1.3 Relationship to FAT32 and ext4

- FAT32 remains mandatory for the EFI System Partition and removable media.
  Limine reads FAT32 and ISO9660, so the ESP continues to carry `BOOTX64.EFI`,
  `limine.conf`, `KERNEL.ELF`, `KERNEL.BIN`, `MiSans.ttf` and `WALL.RGB`.
- ext4 remains fully supported through `/bin/ext4fs`, including read/write
  interoperability with Linux-produced volumes. Existing ext4 installs keep
  booting and remain writable.
- FS+ becomes the default root volume for new installations. It does not
  replace the ESP, and it does not remove the ext4 service.
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

1. **Never overwrite a live tree.** A block reachable from the committed root
   is immutable. A change copies every node on the path to the leaf, changes
   the copy, and publishes the new root only after the whole transaction is
   durable.
2. **The old root is the recovery point.** No journal replay exists. If power
   fails before the new superblock is committed, the previous root is still
   complete and is used on the next mount.
3. **Validate before trust.** Every key, pointer, length, tree level, checksum
   and block number is checked before it is used. A malformed image fails
   closed.
4. **One tree per concern.** The FS tree holds namespace and file mapping;
   the free-space tree owns allocation; the checksum tree owns data checksums;
   the reference-count tree owns shared-block lifetime; the root tree names
   every subvolume, snapshot and global tree. Each fact has one authority.
5. **Sharing is explicit.** A block with more than one owning root has a
   reference count greater than one. CoW replaces only the writer’s mapping;
   the other roots keep the old block until their last reference disappears.
6. **Ring 3 only.** FS+ runs in `/bin/fs+` with a per-volume block capability.
   No FS+ parsing code or on-disk structure is linked into `kernel.elf`.

## 3. Volume Layout

All offsets are in 4096-byte FS+ blocks. A block number is 64-bit on disk; v1
caps usable volumes at 2^48 blocks (1 EiB) and rejects larger values instead of
truncating them.

```
block 0    superblock slot 0
block 1    superblock slot 1
block 2+   allocatable blocks, owned by the trees
```

There are no fixed block groups, inode tables, journal or bitmaps. Every block
from block 2 onward is either reachable from the committed root or listed as
free in the free-space tree. The two superblock slots are the only reserved
blocks.

The root tree is the top of the committed object graph. Its root block and
level live in a superblock; its items name the free-space tree, checksum tree,
root metadata tree, and every subvolume or snapshot FS tree. The superblock
itself is copied between the two slots on each commit, so the other slot always
holds the previous successful root. A snapshot is just another root-tree entry
whose FS tree root is frozen and marked read-only; it is not a copied directory
tree.

### 3.1 Superblock

Both slots have the same layout. A slot is valid only when the magic, version,
geometry, feature flags and checksum validate.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x000 | 8 | `sb_magic` | `"FS+1\0\0\0\0"` |
| 0x008 | 4 | `sb_version_major` | 1 |
| 0x00C | 4 | `sb_version_minor` | 0 |
| 0x010 | 4 | `sb_block_size` | 4096 |
| 0x014 | 4 | `sb_flags` | bit 0 read-only, bit 1 I/O error |
| 0x018 | 8 | `sb_total_blocks` | volume size in blocks |
| 0x020 | 8 | `sb_free_blocks` | hint; fsck recomputes |
| 0x028 | 8 | `sb_root_tree_block` | root-tree node block |
| 0x030 | 2 | `sb_root_tree_level` | 0..8 |
| 0x032 | 2 | reserved | zero |
| 0x034 | 4 | `sb_checksum_type` | 1 = CRC32C |
| 0x038 | 8 | `sb_generation` | commit generation |
| 0x040 | 8 | `sb_root_owner` | root tree owner id, always 0 |
| 0x048 | 16 | `sb_uuid` | volume UUID |
| 0x058 | 16 | `sb_label` | UTF-8, NUL padded |
| 0x068 | 4 | `sb_incompat` | incompatible feature bits |
| 0x06C | 4 | `sb_ro_compat` | read-only-compatible bits |
| 0x070 | 4 | `sb_compat` | compatible bits |
| 0x074 | 4 | `sb_slot` | 0 or 1; must equal `sb_generation & 1` |
| 0x078 | 8 | `sb_mkfs_sec` | UTC seconds |
| 0x080 | 8 | `sb_last_mount_sec` | UTC seconds |
| 0x088 | 4 | `sb_checksum` | CRC32C of bytes 0x000..0x087 |
| 0x08C | 4 | reserved | zero |
| 0x090 | 3952 | reserved | zero |

Multi-byte fields are little-endian. The superblock is one full 4096-byte block;
readers must not assume host struct padding matches this table.

Feature bits:

| Set | Bit | Name | Meaning |
|---|---:|---|---|
| incompat | 0 | `COW` | copy-on-write root commits are mandatory |
| incompat | 1 | `BTREE` | B+tree object format |
| incompat | 2 | `CHECKSUM` | CRC32C metadata checksums are mandatory |
| incompat | 3 | `64BIT` | 64-bit block numbers are in use |
| ro_compat | 0 | `INLINE_DATA` | small files may be inline in the inode item |
| ro_compat | 1 | `DATA_CSUM` | per-block data checksums are present |
| ro_compat | 2 | `SPARSE` | absent extent items represent holes |
| ro_compat | 3 | `XATTR` | xattr items are present |
| ro_compat | 4 | `SHARED_BLOCKS` | shared FS tree and data blocks use the reference-count tree |
| compat | — | none | v1 defines no compatible-only features |

An unknown incompat bit makes the volume unmountable. An unknown ro_compat bit
makes it mountable read-only. An unknown compat bit is ignored. Version 1.0
requires incompat bits 0–3 and sets ro_compat bits 0–4. A reader that does not
understand `SHARED_BLOCKS` must mount read-only, because freeing a shared block
would corrupt every other root that still references it.

All checksums use CRC32C with the reflected polynomial `0x82F63B78`, an initial
value of `0xFFFFFFFF`, and a final XOR of `0xFFFFFFFF`. Seeded checksums feed
the seed bytes in little-endian order before the object bytes.

### 3.2 Mounting and slot selection

mkfs writes generation 1 into slot 1 and leaves slot 0 zeroed. The first
read-write transaction uses generation 2 and writes slot 0. After that, every
commit increments the generation and writes `generation & 1`, so the other slot
always holds the immediately previous successful root.

Mount reads block 0 and block 1, validates each slot independently, and chooses
the valid slot with the highest `sb_generation`. A slot whose generation does
not match its slot number, whose checksum fails, or whose root tree node or
level is invalid is ignored. If both slots are valid but one is newer and its
root tree node fails its checksum, the older slot is used and the volume is
mounted read-write after reporting recovery to the log; the failed newer slot
is overwritten by the next commit.

There is no journal replay. A successful commit means every block reachable
from the new root was written and flushed before the superblock slot was
written. If the new slot is torn, the other slot still names the previous root.
If both slots are invalid, mount fails and `fsck.fs+` is required.

The two-slot fallback stays safe because a transaction never reuses a block it
freed in the same commit. The previous generation therefore still finds every
block it references unchanged in the other slot. At the next commit, that
previous slot is overwritten, so an even older root is not kept alive while its
blocks become reusable.

A volume is always crash-consistent at a committed generation. The `sb_flags`
read-only bit is set by the service when it refuses writes because of a
checksum error, a read-only block device, or an unsupported feature.

## 4. B+tree Format

All metadata is stored in B+trees. A tree is a root block plus a level. Empty
trees use root block 0 and level 0. A non-empty tree has a root node whose level
is 0 for a leaf or 1..8 for an index node.

### 4.1 Node

A node is one 4096-byte block:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x000 | 4 | `node_magic` | `"FS+N"` |
| 0x004 | 1 | `node_version` | 1 |
| 0x005 | 1 | `node_level` | 0 leaf, 1..8 index |
| 0x006 | 2 | `node_nritems` | number of items |
| 0x008 | 4 | `node_free` | free bytes after the last item |
| 0x00C | 4 | `node_checksum` | CRC32C of the whole block with this field zeroed |
| 0x010 | 8 | `node_generation` | transaction generation that created the node |
| 0x018 | 8 | `node_owner` | 0 for the root tree, a global root id, or an FS tree’s `ri_tree_id` |
| 0x020 | 4064 | items | packed item records |

Node checksums are seeded with the volume UUID and `node_owner`. The checksum
field is zero while calculating. A node is rejected if its magic, version,
level, item count, free-space accounting, owner or checksum is invalid.

Item record:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 2 | `item_key_len` | key bytes |
| 0x02 | 2 | `item_value_len` | value bytes |
| 0x04 | 2 | `item_flags` | zero in v1 |
| 0x06 | 2 | reserved | zero |
| 0x08 | variable | key | tree-specific key |
| ... | variable | value | tree-specific value |

Items are packed without padding. `node_free` must equal the bytes not covered
by the 32-byte header and the item records. A reader must reject overlapping,
out-of-range or truncated items before decoding keys or values.

Index items store the first key of a child plus a 16-byte child value:
`{u64 child_block, u8 child_level, 7 reserved}`. Separator keys are strictly
increasing. Lookup descends to the child with the greatest separator key not
greater than the target; if the target precedes the first separator, the key is
absent from the tree. A parent’s level is exactly one greater than each child’s
level, and the root level is capped at 8.

### 4.2 Copy-on-write updates

A tree update never modifies a node reachable from the committed root. The
service clones the root-to-leaf path into newly allocated nodes, edits the new
leaf, and propagates new child pointers and separator keys upward. A split
creates a new sibling and promotes its first key. A root split creates a new
root. The old path and any old sibling become free after the transaction
commits, not before.

Nodes are immutable once committed. The service may cache them read-only by
block number and generation; a cache hit is valid only when the generation
matches the transaction view.

## 5. Trees

The root tree is named by the superblock and has owner id 0. Its key is an
8-byte `root_id`; its value is a root item. The root tree itself has no
separate root item.

Root item, fixed header plus name. The value length is `88 + ri_name_len`.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `ri_block` | root node block, 0 if empty |
| 0x08 | 1 | `ri_level` | root level |
| 0x09 | 1 | `ri_key_kind` | 1 FS, 2 FREE, 3 CHECKSUM, 4 META, 5 REFCOUNT |
| 0x0A | 2 | `ri_flags` | 1 subvolume, 2 snapshot, 4 read-only, 8 default |
| 0x0C | 4 | reserved | zero |
| 0x10 | 8 | `ri_generation` | transaction that produced this root |
| 0x18 | 8 | `ri_next_inode` | next inode for this FS tree; unused for snapshots |
| 0x20 | 8 | `ri_tree_id` | FS tree identity; equals the root id for a subvolume, source `ri_tree_id` for a snapshot |
| 0x28 | 16 | `ri_uuid` | root UUID |
| 0x38 | 16 | `ri_parent_uuid` | snapshot source UUID, zero for a subvolume |
| 0x48 | 2 | `ri_name_len` | 0 for a global tree, 1..255 for a subvolume or snapshot |
| 0x4A | 6 | reserved | zero |
| 0x50 | 8 | reserved | zero |
| 0x58 | variable | `ri_name` | subvolume or snapshot name bytes |

`ri_tree_id` is the identity written into every FS tree node’s `node_owner`.
A subvolume has `ri_tree_id == root_id`. A snapshot has a new root id but copies
the source’s `ri_tree_id`, so its nodes still validate against the frozen tree.
Global trees use their root id as `ri_tree_id`. A tree walk checks
`node_owner` against `ri_tree_id`, never against the root-tree key alone;
otherwise a snapshot sharing the source tree would be rejected.

The root tree key is `root_id`. The default subvolume is root id 1, is named
`default`, has `ri_tree_id == 1`, and carries `SUBVOLUME | DEFAULT`. Extra
subvolume and snapshot root ids start at 16 and are allocated monotonically by
the root metadata’s `next_root_id`, which mkfs initializes to 16; v1 never
reuses a root id and reserves the name `default`.

Global-tree root items (ids 2–5) have `ri_name_len == 0`, zero UUIDs, zero
`ri_next_inode` and zero flags. Only FS tree roots carry names, UUIDs and inode
counters. A subvolume or snapshot name is a byte string of 1..255 bytes with no
`0x00` or `/`; names are unique across the root tree and are compared bytewise.
A snapshot sets `SNAPSHOT | READ_ONLY`, copies the source’s FS tree root block,
level and `ri_next_inode`, keeps the source’s `ri_tree_id`, and keeps
`ri_parent_uuid` equal to the source root UUID. `READ_ONLY` without `SNAPSHOT`
is invalid, and `DEFAULT` is valid only on root id 1.

Root ids:

| Root id | Content | Key | Value |
|---:|---|---|---|
| 1 | default subvolume FS tree | FS key (§6.1) | inode, directory, extent or xattr item |
| 2 | free-space tree | `u64 start_block` | `u32 length`, `u32 reserved` |
| 3 | checksum tree | `u64 physical_block` | `u32 crc32c`, `u32 reserved` |
| 4 | root metadata | 8-byte `meta_id` | `u64 next_root_id`, `u64 flags`, `u64 reserved` |
| 5 | reference-count tree | `u64 physical_block` | `u32 refcount`, `u32 reserved` |
| 6–15 | reserved | — | v1 rejects these root ids |
| 16+ | extra subvolume or snapshot root | FS key (§6.1) | inode, directory, extent or xattr item |

The free-space tree stores only free extents. Every non-superblock block that
is not listed has a live reference count. Free extents are sorted,
non-overlapping and coalesced with adjacent free extents after every
transaction. The checksum tree has one item for every allocated file-data block
when `DATA_CSUM` is set. The reference-count tree has one item for every
allocated block except the two superblock slots; a block whose count reaches
zero leaves the tree and enters the free-space tree. The root metadata item 1
holds `next_root_id`; the default subvolume’s inode counter lives in its root
item, so each subvolume has an independent inode namespace. Root inode is 1 in
every FS tree; mkfs and `SUBVOL_CREATE` initialize a tree’s `ri_next_inode`
to 2.

A root-tree entry with an unknown `ri_flags` bit, an unknown root id, or an
unknown `ri_key_kind` makes the volume read-only. The service never guesses how
another implementation shares blocks.

## 6. FS Tree

Each subvolume owns one FS tree; a snapshot references a frozen FS tree that
may also be referenced by its source and by other snapshots. Inode numbers are
scoped to that tree, directory entries may reference only inodes in the same
tree, and a cross-subvolume rename or hard link returns EXDEV. The writable
root item’s `ri_next_inode` is the authoritative allocator for that tree;
snapshots copy it but are read-only.

### 6.1 Keys

An FS tree key is variable length:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `fk_objectid` | inode number, or directory parent for a name |
| 0x08 | 1 | `fk_type` | 1 inode, 2 directory entry, 3 extent, 4 xattr |
| 0x09 | 1 | `fk_name_len` | 0..255 |
| 0x0A | 2 | reserved | zero |
| 0x0C | 8 | `fk_offset` | logical block for extent items, 0 otherwise |
| 0x14 | variable | `fk_name` | name bytes when `fk_name_len != 0` |

Key length is 20 + `fk_name_len` bytes. Keys compare by `fk_objectid`, then
`fk_type`, then `fk_offset`, then `fk_name` as unsigned bytes; shorter names
that are prefixes sort before longer names. `fk_name_len` is implied by the key
length and is not part of the comparison.

- Inode item: `(inode, 1, 0, "")`.
- Directory entry: `(parent_inode, 2, 0, name)`.
- Extent item: `(inode, 3, logical_block, "")`.
- Xattr item: `(inode, 4, 0, name)`.

Directory lookup constructs the exact key. Listing walks the range whose
`fk_objectid == parent` and `fk_type == 2`; `.` and `..` are ordinary entries.
The tree comparator, not a hash table, defines name order, so hash collisions
do not exist.

Names are byte strings, not Unicode strings. FS+ does not normalize, case-fold
or validate UTF-8. The only forbidden bytes are `0x00` and `/`; length is
1..255 bytes. Upper layers may display names as UTF-8.

### 6.2 Inode item

Inode item value:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 2 | `fi_mode` | POSIX type and permission bits |
| 0x02 | 2 | `fi_flags` | bit 0 inline data |
| 0x04 | 4 | `fi_uid` | owner user id |
| 0x08 | 4 | `fi_gid` | owner group id |
| 0x0C | 4 | `fi_links` | hard-link count |
| 0x10 | 8 | `fi_size` | file size in bytes |
| 0x18 | 8 | `fi_nbytes` | allocated data bytes |
| 0x20 | 8 | `fi_atime_sec` | UTC seconds |
| 0x28 | 4 | `fi_atime_nsec` | 0..999999999 |
| 0x2C | 4 | `fi_mtime_nsec` | 0..999999999 |
| 0x30 | 8 | `fi_mtime_sec` | UTC seconds |
| 0x38 | 8 | `fi_ctime_sec` | UTC seconds |
| 0x40 | 4 | `fi_ctime_nsec` | 0..999999999 |
| 0x44 | 4 | `fi_btime_nsec` | 0..999999999 |
| 0x48 | 8 | `fi_btime_sec` | creation time, UTC seconds |
| 0x50 | 8 | `fi_generation` | inode generation |
| 0x58 | 4 | `fi_data_flags` | zero in v1 |
| 0x5C | 2 | `fi_inline_len` | inline data bytes, 0..256 |
| 0x5E | 2 | reserved | zero |
| 0x60 | variable | `fi_inline` | inline file data |

The fixed header is 96 bytes. A regular file with `fi_size <= 256` and
`fi_flags & 1` stores its data in `fi_inline`; otherwise it has extent items.
A symbolic link stores its target inline when the target is at most 256 bytes,
and in extents when longer. Directories have `fi_inline_len == 0` and no extent
items; their namespace lives in directory-entry items.

`fi_nbytes` counts allocated file-data bytes, not tree nodes. `st_blocks`
returns `fi_nbytes / 512` rounded up. A file with holes reports only allocated
bytes. Inline data is part of the inode item and has no data blocks.

Inode numbers are unique for the life of one FS tree. V1 does not reuse an
inode number after `fi_links` reaches zero; that tree’s `ri_next_inode` only
increases. This removes a free-inode tree and makes crash recovery
unambiguous: an allocated inode unreachable from the namespace is an orphan
only if its data must be reclaimed, and its number is never handed out again.
Subvolumes may use the same numeric inode values independently; a snapshot
shares the source’s FS tree and therefore its inode numbers.

### 6.3 Directory entry item

Directory entry value is 16 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `de_inode` | child inode |
| 0x08 | 1 | `de_type` | 1 regular, 2 directory, 3 symlink, 4 char, 5 block, 6 fifo, 7 socket |
| 0x09 | 3 | reserved | zero |
| 0x0C | 4 | `de_generation` | child inode generation hint |

The key name is the directory entry name. A directory cannot contain two equal
keys, so duplicate names are rejected by the B+tree before a transaction
commits. `.` and `..` are stored in every directory; root `.` and `..` both
point to inode 1. A rename that moves a directory updates the `..` entry in the
moved directory in the same transaction.

Directory `fi_size` is the number of live entries, not bytes. The tree is
authoritative; `fi_size` is a stat hint and is recomputed by fsck.

### 6.4 Extent item

Extent item value is 24 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `ex_physical` | first physical block |
| 0x08 | 4 | `ex_length` | blocks, 1..65536 |
| 0x0C | 4 | `ex_flags` | bit 0 unwritten |
| 0x10 | 8 | `ex_generation` | transaction that created the mapping |

The key offset is the logical block. Extent items for one inode are sorted by
logical block and must not overlap. A hole is any logical block with no item;
reads return zeros. An unwritten extent reads as zero until a write converts
it. `ex_physical + ex_length` must not exceed `sb_total_blocks`.

CoW data writes never modify the old physical blocks. A write splits the
overlapped extent items at the write boundaries. Logical blocks fully covered
by the write are mapped to newly allocated blocks. A partially covered block is
copied to a new block, patched with the new bytes and remapped. Untouched
blocks keep their old physical mappings, and the split extent items around the
write are rewritten to describe the new layout. Old blocks that are no longer
referenced become free after the new root commits. This keeps every committed
generation self-contained without rewriting whole extents for a small patch.

### 6.5 Xattr item

The key name is the xattr name, including its namespace prefix (`user.`,
`system.`, and so on); the value is the raw xattr value. FS+ stores xattrs as
normal FS tree items, so a value is limited only by the item size that fits in
a B+tree leaf. ACLs and security labels are not enforced by the current system,
which has no user-account separation; the mode, uid and gid fields are still
stored and returned by stat.

## 7. Copy-on-Write Transactions

### 7.1 Transaction state

A transaction has a new generation, a set of newly allocated blocks, a set of
blocks freed by the change, and a dirty set of tree roots. The transaction
allocates blocks from an in-memory view of the free-space tree. The on-disk
free-space tree is not modified during the transaction; its new version is one
of the trees published by the commit.

Every allocated block has one reference-count item with a count of at least
one. A normal allocation starts at one. Creating a snapshot increments every
FS tree node and data block reachable from the source; the source and the
snapshot then own those blocks equally. A CoW replacement decrements the old
block; the block moves to the free-space tree only when its count reaches zero.
Global trees (root tree, free-space tree, checksum tree, reference-count tree
and root metadata) are never shared by snapshots and keep count one.

Rules:

- A block allocated in the transaction cannot be allocated twice.
- A block freed in the transaction cannot be reused in the same transaction.
  The previous committed root may still reference it until the new root is
  durable.
- New tree nodes are written once, with the transaction generation in
  `node_generation`.
- The transaction either publishes every tree change through one superblock
  slot or publishes none of them.
- ENOSPC aborts the transaction before commit. New blocks already written are
  not referenced by the old root and remain free in the old free-space tree.
- The allocator may coalesce free extents in the new free-space tree but must
  not merge across a block that is still live in the current root.
- Reference-count updates for new and replaced blocks are part of the same
  transaction as the tree change that causes them. A tree never points at a
  block whose count was not updated in the same commit.

### 7.2 Commit sequence

1. Write all file-data blocks and all new non-root tree nodes.
2. Flush the block device and wait for completion.
3. Write the new root-tree node last, after every tree it names, then flush
   again.
4. Write the alternate superblock slot with the new generation, root block and
   root level, then flush.
5. Only after the superblock flush succeeds does the service reuse old blocks
   in a later transaction.

The old root remains in the other superblock slot. A failure at step 1, 2 or 3
leaves it untouched. A failure while writing step 4 leaves the old slot valid;
a checksum mismatch in the new slot makes mount choose the old one. A completed
step 4 makes the new root authoritative even if the flush report is delayed,
because all blocks it references were flushed before the superblock write.

The service must issue a real cache flush. If the block driver cannot flush,
FS+ mounts read-only and reports that durability cannot be guaranteed.

### 7.3 What a crash can leave behind

| Crash point | Mount result |
|---|---|
| during data or metadata writes | previous generation |
| during root-tree write | previous generation |
| torn new superblock slot | previous generation |
| after new superblock write, before its flush | previous generation or new generation if the device persisted it |
| after successful commit | new generation |

Both outcomes are complete trees. There is no journal and no replay step. A
block allocated by an incomplete transaction is absent from the old free-space
tree, but it is also absent from the old tree, so it remains free and can be
reused. A block freed by an incomplete transaction remains referenced by the
old tree and is not free in the old free-space tree. No leak and no dangling
reference is possible without an on-disk bit flip, which checksums catch.

### 7.4 Subvolumes and snapshots

A subvolume is a root-tree entry with `SUBVOLUME`, its own FS tree, and its own
`ri_next_inode`. Creating one allocates a new root id at or above 16, sets
`ri_tree_id` to that same id, writes an FS tree containing only root inode 1
with `.` and `..`, and adds the root item in the same transaction. Subvolume
names are unique byte strings of 1..255 bytes; the root tree is scanned on
create, list and mount, so v1 does not need a separate name index. Subvolumes
do not nest: a directory entry can never point into another FS tree, and a
subvolume appears in the VFS only as a separate mount.

A snapshot is a root-tree entry with `SNAPSHOT | READ_ONLY` and a new root id.
Its FS tree root block, level, `ri_tree_id` and `ri_next_inode` are copied from
the source root, and its `ri_parent_uuid` names the source. The source must be
a subvolume or snapshot root with key kind FS; a global free-space, checksum,
metadata or reference-count tree can never be a snapshot source. The snapshot
creation transaction:

1. Walks every node of the source FS tree.
2. Increments the reference count of every FS tree node.
3. Walks every inode item’s extent items and increments every data block.
4. Adds the new root item and commits.

The root tree, free-space tree, checksum tree, reference-count tree and root
metadata are global and are not shared by the snapshot; their blocks stay at
count one. A snapshot of a snapshot is allowed and adds one more reference to
the same frozen FS tree.

Snapshot creation and deletion are O(reachable FS tree nodes plus data blocks)
in v1. They run as one transaction under the volume lock. If the reference
count tree cannot be updated because of ENOSPC or a counter overflow, the
transaction aborts and no snapshot appears; a partially updated reference
count is never published. The service reports the failure instead of retrying
with a half-created root.

All write, mkdir, unlink, rename, truncate, link and symlink operations on a
snapshot return EROFS. A read-only mount of a snapshot uses the same FS tree
lookup path as a subvolume. Deleting a snapshot removes its root item and
walks the snapshot FS tree once, decrementing every node and data block; a
block whose count reaches zero is freed and its data checksum removed. Deleting
a snapshot or subvolume is refused while that root is mounted or has open
descriptors; deleting the default subvolume is refused outright. Root ids and
inode numbers are retired, never reused.

## 8. Allocation and Checksums

### 8.1 Free-space tree

The free-space tree maps a physical start block to a length. Allocation takes
the first extent with `length >= requested`, splits it, inserts a reference
count of one for every allocated block, and updates the in-memory tree.
Freeing decrements the block’s reference count; only a zero count removes the
reference item and inserts the range into the free-space tree, coalescing with
the immediate predecessor and successor. Free extents never overlap and never
cover the two superblock slots. A block with a positive count must never appear
in the free-space tree, and a block with a zero count must never remain in the
reference-count tree.

Allocation preference, in order:

1. A free extent in the same 1 GiB region as the owning inode’s previous
   extent, to keep related data together.
2. The first suitable extent in the tree.

The free-space tree is itself copy-on-write. Its new nodes are allocated from
the same in-memory free set and are recorded as used in the new tree. If a
transaction cannot find enough free blocks for data plus tree nodes, it aborts
and returns ENOSPC.

### 8.2 Reference-count tree

The reference-count tree is global and keyed by physical block. Its value is a
32-bit count and 32 bits of reserved space. Every allocated block except the
two superblock slots has an item. A normal allocation inserts one; creating a
snapshot increments the FS tree node and data blocks it shares; replacing a
block in any root decrements the old block. Reaching zero removes the item and
frees the block.

The tree is itself copy-on-write and its own new nodes are allocated with count
one. Snapshot creation must reject a source whose increment would overflow a
32-bit count, and both snapshot creation and deletion must reject a reachable
block with no reference item as corruption. The service must never free a
shared block merely because the writer stopped referencing it; only the count
reaching zero is authoritative. `fsck.fs+` recomputes counts from every live
root and reports a mismatch before repair.

### 8.3 Data checksums

When `DATA_CSUM` is set, every allocated file-data block has one checksum-tree
item keyed by its physical block. Writes compute CRC32C over the full 4096-byte
block and insert or replace the item. Reads fetch the extent, read the block,
compute CRC32C and compare it with the tree item. A missing or mismatched
checksum returns EIO and marks the volume as having an I/O error; it never
returns unverified data.

Inline data is covered by the inode item’s node checksum. Metadata nodes are
always checksummed regardless of `DATA_CSUM`.

## 9. POSIX Semantics

| Topic | FS+ v1 behavior |
|---|---|
| File types | regular, directory, symlink, char device, block device, FIFO, socket |
| Permissions | mode, uid and gid are stored and returned by stat; current VFS has no account enforcement, so the service does not deny access by owner |
| Timestamps | atime, mtime, ctime, btime with second and nanosecond fields; atime updates may be deferred and coalesced |
| Hard links | supported for non-directories within one FS tree; `fi_links` max 2^32-1; cross-subvolume link returns EXDEV |
| Symlinks | target up to 256 bytes inline, longer targets in extents; `readlink` never follows |
| rename | atomic replacement of a file, atomic move of an empty directory, cycle rejection, one transaction; cross-subvolume rename returns EXDEV |
| subvolumes | writable, independent inode namespaces, listed by the root tree; default subvolume id 1 |
| snapshots | read-only FS tree roots sharing source blocks through the reference-count tree; creation and deletion are O(reachable blocks) |
| fsync | complete data and metadata write, flush, root commit, superblock flush |
| truncate | replacement of affected extents with new blocks, zeroed tail, holes on extension |
| sparse | holes read as zero; `fi_nbytes` reports only allocated bytes |
| mmap | not part of FS+ v1; the VFS maps memfd objects today and FS+ files are read/written through the service |
| case | case-sensitive, byte-exact |
| max name | 255 bytes |
| max file size | 2^63-1 bytes, bounded in practice by volume size |
| max volume | 2^48 blocks (1 EiB) enforced by v1 |
| max symlink target | 4095 bytes |
| max hard links | 2^32-1 |
| max reference count | 2^32-1 per block |
| max path | whatever the VFS accepts; the FS+ service validates each component independently |

Directory `st_size` returns the live entry count. `st_blocks` returns
`ceil(fi_nbytes / 512)`. `st_blksize` returns 4096. `statfs` reports
total/free blocks from the free-space tree and per-subvolume inode usage from
the mounted root item’s `ri_next_inode`; retired inode numbers are not reported
as free. The counts are validated against the geometry before they are
returned.

Rename over an existing directory is allowed only when the destination is an
empty directory. The service rejects any move that would create a directory
cycle by walking the parent chain with a bounded depth and validating every
inode item it visits. `unlink` of an open file keeps the inode and its blocks
alive in memory until the last handle closes; a crash in that window leaves an
inode with `fi_links == 0` that fsck reports and reclaims, while the inode
number remains retired.

## 10. Ring 3 Integration

### 10.1 Service

`/bin/fs+` implements the VFS request window used by `/bin/ext4fs`: one control
page plus the current 4 KiB data window (`FS_DATA_SIZE`), with a state machine
of `IDLE -> REQUEST -> DONE -> IDLE`. A move to a 64 KiB window is a separate
ABI change and is not assumed here; FS+ must stay correct with 4 KiB requests
and chunk larger transfers.

The existing `struct fs_rpc` is the starting point. FS+ needs these operations:

| Operation | Purpose |
|---|---|
| `MOUNT` / `PROBE` | validate a candidate volume and choose a superblock slot |
| `STAT` / `LIST` | inode and directory reads |
| `READ` / `WRITE` | file data, with CoW replacement of affected extents |
| `MKDIR` / `RMDIR` | directory creation and removal |
| `RENAME` | atomic move/replace, with source and destination paths |
| `READLINK` / `SYMLINK` | symlink target access |
| `LINK` | hard link |
| `TRUNCATE` | set file size |
| `UNLINK` | remove a name and reclaim its inode when links reach zero |
| `SYNC` / `FSYNC` | commit a generation and flush |
| `STATFS` | volume statistics |
| `SUBVOL_CREATE` / `SUBVOL_DELETE` | create or remove a writable subvolume root |
| `SUBVOL_LIST` | enumerate root-tree entries and flags |
| `SNAPSHOT_CREATE` / `SNAPSHOT_DELETE` | create or remove a read-only snapshot root |
| `MOUNT_ROOT` | mount a named subvolume or snapshot at a VFS mount point |
| `FORMAT` | create a new FS+ volume |

`RENAME` needs two paths. The control page gains a second 256-byte destination
field. Paths never share memory with file data.

### 10.2 Block capability

FS+ must not depend on a process-name whitelist. The kernel already has a
trusted block path for `/bin/ext4fs`; the design moves that path to an explicit
capability:

- The VFS creates a block capability for one partition range when it starts a
  storage service.
- The capability is passed in the service’s spawn argument and is required by
  `blk_read`, `blk_write` and `blk_flush`.
- Only the VFS can mint the capability. A service cannot widen its range, read
  another volume, or hand the capability to an unrelated process.
- `/bin/ext4fs` moves to the same mechanism. During the transition, the kernel
  may accept the old process-name check for ext4fs only, but new code must not
  extend that exception to FS+.

The service must call `blk_flush` at the commit points in §7.2. A driver that
cannot flush forces read-only mounting.

### 10.3 Mount and fallback

The VFS storage startup sequence is:

1. Start `/bin/fs+` with the candidate root partition and ask `PROBE`.
2. If FS+ validates, mount it at `/mnt` through the service.
3. If not, try `/bin/ext4fs` `MOUNT`.
4. If not, fall back to the kernel FAT32 path, then to the embedded `/data`
   mirror.

New installs create FS+ on the root partition, so step 1 succeeds on a normal
system. Existing ext4 disks continue through step 3. The ESP is never mounted
through FS+; it stays on the FAT32 path.

The default subvolume mounts at `/mnt`. `SUBVOL_LIST` returns only root items
with `SUBVOLUME` or `SNAPSHOT`; global trees are never mountable. `MOUNT_ROOT`
attaches a named subvolume or snapshot at a caller-provided mount point. Two mounts of the same writable subvolume share
one service-side tree and commit lock; a snapshot mount is read-only. Renaming
a file between mount points that belong to different FS trees returns EXDEV.

The kernel must not start `/bin/fs+` before `/bin/drivers` reports its block
device ready. A service timeout marks the volume as having an I/O error and
returns `EIO` to VFS callers instead of blocking the desktop forever.

### 10.4 Boot and installer

- Limine continues to load `KERNEL.ELF`, `KERNEL.BIN`, `MiSans.ttf` and
  `WALL.RGB` from the FAT32 ESP. FS+ never holds boot-loader files.
- The installer formats the ESP as FAT32 and the root partition as FS+, then
  writes the boot payload to the ESP. The FS+ `FORMAT` operation creates the
  two superblocks, the root tree, the default subvolume FS tree, the
  free-space tree, the checksum tree, the reference-count tree, the root
  metadata tree and the default subvolume’s root directory.
- The installer writes user-visible files through the FS+ service, not through
  a second in-kernel formatter.
- The installed root label remains `ZHAOXUROOT` so existing disk-role scanning
  and repair tooling keep working; the file-system magic, not the label,
  selects FS+.
- The optional `ZHAOXUDATA` partition stays FAT32 in v1 for interoperability
  with other systems.

### 10.5 Root pivot

FS+ mounts at `/mnt` while `/` remains the initramfs root. A later milestone
may pivot the system directories to FS+ after the storage service is ready.
That pivot must preserve the recovery path: if the FS+ service fails, the
initramfs shell and `/data` mirror must still be available. Root pivot is not
part of v1.

## 11. Tools

| Tool | Role |
|---|---|
| `src/fs/fs+/` | OS-independent FS+ core: geometry, superblocks, B+tree, FS tree, free-space tree, checksum tree, reference-count tree, subvolume roots, transactions, checker |
| `src/apps/fs+/` | Ring 3 service: RPC loop, block capability calls, cache, mount, subvolume and snapshot management, commit |
| `mkfs.fs+` | Ring 3 and host-buildable formatter |
| `fsck.fs+` | Ring 3 and host-buildable consistency checker and repair tool |
| `src/tools/fs+-test.rb` | Ruby host-test driver: build images, run the core against a file-backed device, compare with `mkfs.fs+`/`fsck.fs+` |
| `src/tools/fs+-fault.rb` | Ruby fault-injection driver for CoW commit tests |

Repository scripts are Ruby. The formatter and checker are C programs in the
tree, not shell scripts. Host tests build the same core sources for Linux with a
file-backed block-device shim; they must not fork a separate implementation.

`fsck.fs+` repair order:

1. Validate both superblock slots; choose the newest valid root.
2. Walk the root tree, then every subvolume and snapshot FS tree, the
   free-space tree, checksum tree, metadata tree and reference-count tree;
   validate node checksums, levels, key order and item bounds.
3. Validate root items: unique root ids, names and non-zero UUIDs, one default
   subvolume, snapshot parent UUIDs, read-only flags, `ri_tree_id` consistency
   and `ri_next_inode` bounds.
4. Walk every inode and directory in each FS root version: detect cycles,
   duplicate names, bad `..`, wrong types, missing children and link-count
   mismatches. Cross-tree inode references are corruption. Root items that
   share one `ri_tree_id` are versions of one tree but may point at different
   historical root blocks; validate each version independently. A repair
   rewrites only the version whose root item is being repaired, through CoW;
   other versions keep their old blocks and reference counts.
5. Walk every extent: reject out-of-range, overlapping or unaligned mappings;
   verify the data checksum item for each allocated data block.
6. Recompute reference counts: global trees count once, and each subvolume or
   snapshot root version counts its own FS tree nodes and data blocks. Compare
   with the reference-count tree and report each mismatch before repair.
7. Rebuild the reference-count tree, free-space tree and checksum tree from the
   reachable allocation map, preserving the two superblock slots and the
   committed tree blocks.
8. Recompute directory entry counts and free-space hints.
9. Write a repaired generation through the normal CoW commit path; never patch
   a live tree in place.

`fsck.fs+ -n` is read-only diagnosis. Repair never writes when it cannot prove
that the metadata it is replacing is safe.

## 12. Testing

### 12.1 Host tests

- Images: 64 MiB, 128 MiB, 1 GiB, and a sparse 16 TiB image to exercise 64-bit
  block numbers. Include an image with no free space, one with a fragmented
  free-space tree, one with the checksum tree disabled and one with it enabled.
- Operations: create/read/write/append/truncate, partial-block writes, sparse
  writes, inline-to-extent promotion, extent splitting and coalescing, directory
  splits, long names, rename across directories, hard links, symlinks, unlink of
  open files, ENOSPC, full inode counter and maximum name length.
- Subvolumes: create, mount, write, rename within the tree, reject cross-tree
  rename and hard link with EXDEV, delete only after unmount, default root
  deletion refusal.
- Snapshots: create from a subvolume, read the frozen tree, reject every write
  with EROFS, modify the source, verify the snapshot still sees the old blocks,
  create a snapshot of a snapshot, delete each snapshot and verify the source’s
  blocks and reference counts remain correct, delete the source subvolume while
  its snapshot remains readable, and delete the last reference to a shared
  block and verify it returns to the free-space tree.
- After every scenario, `fsck.fs+ -n` must report clean, every data block
  checksum must verify and every reference count must match the live roots.
- Every test image is mounted twice: once normally, once by forcing mount to
  choose the older slot, to prove the previous root is complete.

### 12.2 Crash and fault injection

- For every write point in a transaction, drop all later writes and remount.
  The mounted tree must be exactly the old generation or exactly the new one,
  with no key loss, duplicate name, lost extent or failed checksum.
- Tear the superblock slot at each byte boundary and verify that mount falls
  back to the other slot.
- Corrupt a tree node checksum, a data block checksum, a free-space extent and
  an extent pointer, one at a time; each must fail closed or be repaired by
  `fsck.fs+`.
- Exhaust free space in the middle of a large write and verify that the old
  generation is intact and ENOSPC is returned.
- Kill the service between the root write and the superblock write; the old
  root must mount with no repair.
- Kill the service during snapshot creation and during snapshot deletion; the
  volume must mount either without the snapshot with unchanged counts, or with
  the complete snapshot with correct counts. A half-incremented reference count
  must never be visible.
- Exhaust free space during a snapshot creation to force an abort, then verify
  that the source tree and every reference count are unchanged.

### 12.3 QEMU tests

1. Build a normal image and boot the ISO under OVMF and SeaBIOS.
2. Install to a blank GPT disk and to a blank MBR disk; both must create a
   FAT32 ESP and an FS+ root.
3. Mount FS+, create and read files under `/mnt`, reboot, verify contents.
4. Create a subvolume, mount it, write files, create a read-only snapshot,
   modify the subvolume and verify the snapshot is unchanged after reboot.
5. Delete the snapshot and verify the source still reads correctly and the
   shared blocks are reclaimed only when the last reference disappears.
6. Force a QEMU quit during a write; remount and confirm the previous or new
   generation, then run `fsck.fs+ -n`.
7. Verify `nm out/kernel.elf` has no FS+ symbols and no undefined symbols.
8. Replace the FS+ root with an ext4 image and confirm the ext4 fallback still
   mounts and writes.
9. Confirm the ESP contains only FAT32 boot files and no FS+ metadata.

### 12.4 Security checks

- The kernel must reject malformed RPC lengths, paths without terminators and
  out-of-range offsets before copying to or from the data window.
- The service must validate every block number, tree level, item length, key
  order, extent length and root pointer.
- A node must not be followed if its checksum, `node_owner` does not match the
  root item’s `ri_tree_id`, or its generation is invalid.
- A root item marked `SNAPSHOT | READ_ONLY` must reject every mutating request;
  a snapshot source must be an FS root, never a global tree.
- No recovery path may allocate memory proportional to an unvalidated on-disk
  count. Tree depth is capped at 8 and extent length at 65536 blocks.
- Free-space entries and reference counts must never disagree: a free block
  has count zero and no reference item, and a live block is absent from the
  free-space tree. Validation happens before a transaction commits.

## 13. Milestones

### M0 — Specification and image tooling

- Freeze the on-disk structures in this document.
- Implement geometry, superblocks, B+tree node encoding, initial root tree,
  FS tree, free-space tree, checksum tree and reference-count tree, plus a
  host-buildable `mkfs.fs+`.
- Implement read-only `fsck.fs+` checks for a freshly formatted image.
- Acceptance: the host matrix creates valid images and `fsck -n` passes.

### M1 — Read-only mount

- Slot selection, root-tree walk, subvolume and snapshot enumeration, read-only
  snapshot mount, FS-tree lookup, directory listing, inode and extent reads,
  inline files, checksum verification.
- Acceptance: all host read tests pass; malformed images fail closed; the QEMU
  service can list a prebuilt FS+ image and mount its snapshots read-only.

### M2 — CoW writes and commits

- Transaction path copying, node splits, free-space allocation, data CoW,
  extent replacement, directory operations, inode allocation, checksum-tree
  and reference-count-tree updates, superblock commit and fsync.
- Acceptance: full host write matrix passes `fsck -n`; crash injection returns
  old or new generation; QEMU writes survive reboot.

### M3 — Subvolumes and snapshots

- Root-tree subvolume entries, per-subvolume inode counters, named mounts,
  read-only snapshots, shared-block reference counts, snapshot creation and
  deletion, cross-tree EXDEV checks and refcount validation.
- Acceptance: source writes after a snapshot leave the snapshot unchanged;
  deleting the snapshot frees only blocks whose count reaches zero; multiple
  mounted subvolumes stay isolated across commits under the shared volume
  commit lock; crash tests never expose a half-created root.

### M4 — Recovery and repair

- Older-slot fallback, read-only degradation, orphan handling, reference-count
  recomputation, free-space tree rebuild, checksum tree rebuild and fsck repair.
- Acceptance: forced power cuts recover; `fsck.fs+` repairs every injected
  inconsistency that is repairable without data loss.

### M5 — Installer and VFS default

- Add the FS+ operations to the VFS RPC, add the block capability, start
  `/bin/fs+` before the ext4 fallback, and make the installer format FS+ for
  new root volumes.
- Acceptance: a fresh GPT install and a fresh MBR install boot to the desktop,
  mount FS+ at `/mnt`, and leave the ESP intact; ext4 volumes still mount.

### M6 — Root pivot and performance

- Optional pivot of the installed system directories to FS+.
- Allocation locality, node cache, free-space search, snapshot metadata
  scaling and commit batching.
- Acceptance: boot time and file throughput are measured against the ext4
  path; no regression in crash tests.

## 14. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| CoW circularity while allocating tree nodes | Allocate from the in-memory free-space view; the new free-space tree records its own nodes as used and is committed as one root |
| Torn superblock loses the newest root | Two slots with generation and checksum; mount chooses the newest valid slot |
| Free-space tree corruption | fsck rebuilds it from reachable blocks; validation rejects overlaps before commit |
| Data checksum tree drifts from extents | Update both in one transaction; fsck cross-checks every allocated data block |
| Extent splitting leaves stale data | New blocks are copied and zero-filled before mapping; old blocks are freed only after commit |
| CoW fragmentation and write amplification | First-fit with locality, free-extent coalescing, and benchmarks in M6 |
| Service crash during commit | Kernel returns EIO; the old superblock slot remains the recovery root |
| Ring 3 service holds the block capability | Capability is scoped to one partition and cannot be widened or delegated |
| Snapshot creation walks a large tree | O(reachable blocks) and one transaction in v1; fail with ENOSPC/EFBIG instead of publishing a partial root; lazy shared-root refs are future work |
| Refcount drift frees a shared block | Update tree and refcounts in one transaction; fsck recomputes from all live roots; never treat a writer’s drop as a free until the count reaches zero |
| A snapshot write corrupts the source | Snapshot root items are read-only; every mutating RPC checks the root flags before touching a tree |
| 64-bit arithmetic overflow | Check every multiply/add against `sb_total_blocks` and 2^48; use checked helpers |

## 15. Explicit v1 Decisions

1. **No journal.** Crash consistency comes from immutable committed trees and
   the alternating superblock slots. There is no replay log and no journal
   space.
2. **Writable subvolumes, read-only snapshots.** Each subvolume has its own FS
   tree and inode counter. A snapshot is a read-only root item that shares the
   source FS tree blocks. Writable snapshots and reflink ioctls are not v1.
3. **Per-block reference counts.** Every allocated block has a reference-count
   tree item. Snapshot creation increments the shared FS tree nodes and data
   blocks; the last reference frees the block.
4. **Snapshot cost is O(reachable blocks).** V1 creates and deletes a snapshot
   synchronously in one transaction. A huge source may fail with ENOSPC/EFBIG;
   partial snapshots are never published.
5. **Copy-on-write data.** File data blocks are never overwritten in place.
   Partial-block writes allocate a new block, copy the unchanged bytes and
   write the new data.
6. **No inode or root-id reuse.** Inode numbers increase per FS tree and root
   ids increase globally; retired values are never reassigned.
7. **FAT32 data volume.** `ZHAOXUDATA` stays FAT32 in v1. FS+ is the native root
   volume; creating FS+ data volumes without a migration plan would split the
   installer and repair paths.
8. **Root pivot later.** FS+ mounts at `/mnt` in v1; pivoting `/` is a separate
   project because it must preserve the initramfs recovery shell and `/data`
   fallback.
9. **Block capability.** FS+ ships only with an explicit per-volume block
   capability. The old process-name exception may remain for `/bin/ext4fs`
   during the M5 transition, but it must not be extended to `/bin/fs+`.
10. **Data checksums enabled.** New v1 volumes set `DATA_CSUM`; the reader must
    still handle a volume without it as read-only-compatible input.

---

Copyright © 2026 Nexsteaduser. All Rights Reserved.
