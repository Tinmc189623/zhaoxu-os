# FS+ — Native Copy-on-Write File System Design

Date: 2026-11-17
Status: Design target
File system name: `FS+` (exact spelling; see §1.5)

## 1. Scope and Targets

### 1.1 What FS+ is

FS+ is the native file system of Zhaoxu OS. It is a 64-bit copy-on-write
B+tree file system, implemented in Ring 3 as `/bin/fs+`, with a design target
that competes with APFS, XFS and ext4 on their own strengths instead of
collecting their missing features as v1 scope.

- APFS strengths: O(1) root-sharing snapshots, writable clones, extent
  reflinks, space sharing, data integrity.
- XFS strengths: allocation groups, parallel allocation, online growth,
  reflink, project quotas and a serious offline checker.
- ext4 strengths: POSIX surface, mature repair tooling, broad compatibility,
  online growth and predictable recovery.
- FS+ additions: data checksums, Ring 3 isolation, per-volume capability, and
  a native version-epoch reclaimer that does not need per-block snapshot
  reference counts.

FS+ never appears in the boot chain. Limine reads FAT32 and ISO9660 only, so
the ESP keeps `BOOTX64.EFI`, `limine.conf`, `KERNEL.ELF`, `KERNEL.BIN`,
`MiSans.ttf` and `WALL.RGB`. ext4 remains fully supported by `/bin/ext4fs` and
is the compatibility fallback while FS+ matures.

### 1.2 Non-negotiable targets

| Capability | Target |
|---|---|
| Snapshot creation | O(1): copy one root item, no tree walk, no per-block refcount update |
| Snapshot deletion | O(1) metadata removal; reclamation is asynchronous and bounded per AG |
| Writable clone | O(1): share the source tree version, then CoW independently |
| File reflink | O(number of extents): duplicate extent items, increment per-AG extent refs |
| Allocation | Per-AG free-space trees and locks; prepare in parallel, publish one root generation |
| Online grow | Append an AG without remounting or rewriting live trees |
| Integrity | CRC32C on superblocks, B+tree nodes and every stored data block |
| Repair | Offline `fsck.fs+` plus online scrub; never patch a live tree in place |
| POSIX | Files, directories, symlinks, hard links, rename, truncate, fsync, xattr, statfs |
| Subvolumes | Writable roots with independent inode counters; no nesting |
| Snapshots | Read-only roots sharing a version; deletion does not walk the tree |

### 1.3 Honest boundary

Compression, encryption and quotas are designed as feature-gated extensions in
this document, but they are not claimed as implemented until their milestones
pass. A volume that sets an unsupported read-only-compatible feature mounts
read-only; an unsupported incompat feature refuses mount. FS+ does not claim
deduplication, RAID, device pools or distributed operation.

### 1.4 Naming rule

The name is exactly `FS+`, with the two ASCII letters followed by the ASCII
plus sign. Do not expand it, translate it, add a Chinese name, or change the
plus sign. This applies to documentation, UI strings, volume labels, tool output
and commit messages. Volume labels may add a role suffix such as `ZHAOXUROOT`;
the file-system name itself stays `FS+`.

## 2. Design Principles

1. **Immutable committed trees.** No block reachable from a committed root is
   modified in place. A change copies the root-to-leaf path and publishes a new
   root generation.
2. **Snapshots share versions, not refcount entries.** A snapshot is a root
   item pointing at an existing FS tree version. Creating or deleting one is a
   root-tree operation, not a tree walk.
3. **Reference counts are for reflink, not for snapshots.** Snapshot liveness is
   decided by live root versions; reflink sharing is decided by per-AG extent
   counts.
4. **Allocation is per AG, commit is global.** Different AGs allocate and
   prepare CoW paths in parallel; one generation makes the changes visible.
5. **Checksums are mandatory; repair is a first-class path.** A checksum
   mismatch fails closed, and fsck rebuilds derived trees from authoritative
   roots.
6. **Ring 3 only.** The kernel forwards VFS requests and block capabilities. It
   never parses FS+ metadata.

## 3. Volume Layout

All offsets are in 4096-byte blocks. A block number is 64-bit on disk; v1 caps
usable volumes at 2^48 blocks (1 EiB) and rejects larger values rather than
truncating them.

```
block 0                 superblock slot 0
block 1                 superblock slot 1
block 2 ..              allocation groups (AGs)
AG 0                    starts at block 0 and contains both superblock slots
AG 1..N                 contiguous AG ranges, with the last AG partial
```

The volume is divided into allocation groups. AG 0 starts at block 0 and owns
the two superblock slots; blocks 0 and 1 are reserved and never appear in AG 0’s
free-space tree. The AG size is chosen by mkfs as
`clamp(total_blocks / 1024, 262144, 268435456)`, rounded up to a power of two.
That gives 1 GiB AGs on small volumes, grows with the volume, and caps at 1 TiB.
The resulting AG count never exceeds 2^20, which keeps the AG tree and owner-id
space bounded. The last AG may be smaller and is rejected if it cannot hold its
minimum metadata.

The root tree is a global B+tree named by the superblock. It names the default
subvolume, extra subvolume and snapshot roots, the root metadata tree, the AG
tree and the optional quota and key trees. Each AG descriptor in the AG tree
names that AG’s free-space, reference-count, checksum and dead-extent trees.

### 3.1 Superblock

Both slots use the same layout. A slot is valid only when magic, version,
geometry, feature flags, AG geometry and checksum validate.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x000 | 8 | `sb_magic` | `"FS+1\0\0\0\0"` |
| 0x008 | 4 | `sb_version_major` | 1 |
| 0x00C | 4 | `sb_version_minor` | 0 |
| 0x010 | 4 | `sb_block_size` | 4096 |
| 0x014 | 4 | `sb_flags` | bit 0 read-only, bit 1 I/O error |
| 0x018 | 8 | `sb_total_blocks` | volume size in blocks |
| 0x020 | 8 | `sb_free_blocks` | hint across AGs; fsck recomputes |
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
| 0x078 | 4 | `sb_ag_size_blocks` | AG size in blocks, 1 GiB..1 TiB, power of two |
| 0x07C | 4 | `sb_ag_count` | number of allocation groups |
| 0x080 | 8 | `sb_mkfs_sec` | UTC seconds |
| 0x088 | 8 | `sb_last_mount_sec` | UTC seconds |
| 0x090 | 4 | `sb_checksum` | CRC32C of bytes 0x000..0x08F |
| 0x094 | 4 | reserved | zero |
| 0x098 | 3944 | reserved | zero |

Multi-byte fields are little-endian. The superblock is one 4096-byte block;
readers must not assume host struct padding matches this table.

Feature bits:

| Set | Bit | Name | Meaning |
|---|---:|---|---|
| incompat | 0 | `COW` | copy-on-write root commits are mandatory |
| incompat | 1 | `BTREE` | B+tree object format |
| incompat | 2 | `CHECKSUM` | CRC32C metadata checksums are mandatory |
| incompat | 3 | `64BIT` | 64-bit block numbers |
| incompat | 4 | `AG` | allocation-group layout is present |
| incompat | 5 | `EPOCH_RECLAIM` | dead extents are reclaimed by root versions |
| ro_compat | 0 | `INLINE_DATA` | inline small files are present |
| ro_compat | 1 | `SPARSE` | absent extents represent holes |
| ro_compat | 2 | `XATTR` | xattr items are present |
| ro_compat | 3 | `DATA_CSUM` | per-block data checksums are present |
| ro_compat | 4 | `REFLINK` | extent reference sharing is present |
| ro_compat | 5 | `SNAPSHOT` | shared-version snapshot roots are present |
| ro_compat | 6 | `COMPRESS` | compressed extents are present |
| ro_compat | 7 | `ENCRYPT` | encrypted extents are present |
| ro_compat | 8 | `QUOTA` | per-root quota records are present |
| compat | — | none | v1 defines no compatible-only features |

Version 1.0 requires incompat bits 0–5 and sets ro_compat bits 0–5. An unknown
incompat bit makes the volume unmountable. An unknown ro_compat bit makes it
mountable read-only. A reader that does not understand `REFLINK` or `SNAPSHOT`
must mount read-only; otherwise it could free a block still referenced by
another root version.

All checksums use CRC32C with reflected polynomial `0x82F63B78`, initial value
`0xFFFFFFFF`, final XOR `0xFFFFFFFF`. Seeded checksums feed the volume UUID and
the object owner id in little-endian order before the object bytes.

### 3.2 Mount and slot selection

mkfs writes generation 1 into slot 1 and leaves slot 0 zeroed. The first
read-write transaction uses generation 2 and writes slot 0. Every commit writes
`generation & 1`, leaving the other slot with the immediately previous root.

Mount reads both slots, validates each independently, and chooses the valid slot
with the highest generation. A slot whose checksum, generation parity, AG
geometry, root-tree node or level fails validation is ignored. If the newest
slot’s root tree fails its checksum, the older slot is used and a recovery
message is logged. If both slots are invalid, mount fails and `fsck.fs+` is
required.

The two-slot fallback is safe because a transaction does not reuse a block it
freed in the same commit. The previous generation still finds every block it
references untouched. At the next commit, that previous slot is overwritten, so
an even older root is not kept alive while its blocks become reusable.

## 4. B+tree and Objects

### 4.1 Node

A node is one 4096-byte block:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x000 | 4 | `node_magic` | `"FS+N"` |
| 0x004 | 1 | `node_version` | 1 |
| 0x005 | 1 | `node_level` | 0 leaf, 1..8 index |
| 0x006 | 2 | `node_nritems` | item count |
| 0x008 | 4 | `node_free` | free bytes after the last item |
| 0x00C | 4 | `node_checksum` | CRC32C of the whole block with this field zeroed |
| 0x010 | 8 | `node_generation` | transaction generation that created the node |
| 0x018 | 8 | `node_owner` | object owner id |
| 0x020 | 4064 | items | packed item records |

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
by the 32-byte header and item records. Index items store the first key of a
child and a 16-byte value `{u64 child_block, u8 child_level, 7 reserved}`.
Separator keys are strictly increasing. The root level is capped at 8.

Node checksums are seeded with the volume UUID and `node_owner`. A node is
rejected if magic, version, level, item count, free-space accounting, owner or
checksum is invalid.

### 4.2 Owner ids

| Owner | Meaning |
|---:|---|
| 0 | root tree |
| 2 | root metadata tree |
| 3 | AG tree |
| 4 | quota tree when present |
| 5 | key tree when present |
| 16+ | FS tree roots; `ri_tree_id` |
| 2^63 + ag_id*4 + 0 | AG free-space tree |
| 2^63 + ag_id*4 + 1 | AG reference-count tree |
| 2^63 + ag_id*4 + 2 | AG data-checksum tree |
| 2^63 + ag_id*4 + 3 | AG dead-extent tree |

FS tree ids are below 2^63. AG owner ids fit in 64 bits because v1 caps the
volume at 2^30 AGs. A node whose owner does not match the tree being walked is
rejected.

### 4.3 Copy-on-write updates

A tree update clones the root-to-leaf path into newly allocated blocks, edits
the new leaf, and propagates new child pointers upward. A split creates a new
sibling and promotes its first key. A root split creates a new root. Old path
blocks become dead after the transaction commits; whether they are freed
immediately or retained depends on whether an FS tree version still references
them.

Nodes are immutable once committed. The service may cache them read-only by
block number and generation; a cache hit is valid only for the transaction view
that produced it.

## 5. Root Tree

The root tree is global, owned by id 0, and named by the superblock. Its key is
an 8-byte `root_id`; its value is a root item. The root tree itself has no root
item.

### 5.1 Root item

The fixed header is 96 bytes; the value length is `96 + ri_name_len`.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `ri_block` | root node block, 0 if empty |
| 0x08 | 1 | `ri_level` | root level |
| 0x09 | 1 | `ri_key_kind` | 1 FS, 2 META, 3 AG, 4 QUOTA, 5 KEY |
| 0x0A | 2 | `ri_flags` | 1 subvolume, 2 snapshot, 4 read-only, 8 default, 16 clone, 32 deleting |
| 0x0C | 4 | reserved | zero |
| 0x10 | 8 | `ri_generation` | transaction that produced this root item |
| 0x18 | 8 | `ri_next_inode` | next inode for this writable FS tree |
| 0x20 | 8 | `ri_tree_id` | FS tree lineage; equals root id for a subvolume, source id for a snapshot or clone |
| 0x28 | 8 | `ri_tree_version` | generation of the FS tree version this root points at |
| 0x30 | 16 | `ri_uuid` | root UUID |
| 0x40 | 16 | `ri_parent_uuid` | snapshot or clone source UUID, zero for an original subvolume |
| 0x50 | 4 | `ri_key_id` | encryption key id, 0 = none |
| 0x54 | 1 | `ri_compression` | 0 none, 1 LZ4, 2 ZSTD |
| 0x55 | 3 | reserved | zero |
| 0x58 | 2 | `ri_name_len` | 0 for a global tree, 1..255 for a root |
| 0x5A | 6 | reserved | zero |
| 0x60 | variable | `ri_name` | subvolume, snapshot or clone name |

`ri_tree_version` is the key to snapshot reclamation. Every FS tree root item
stores the generation of the version it points at. A snapshot or clone copies
that version at creation. Writes to any writable root advance only that root’s
version.

Flags:

- `SUBVOLUME`: writable root with its own `ri_next_inode`.
- `SNAPSHOT`: read-only root sharing a tree version.
- `READ_ONLY`: required with `SNAPSHOT`.
- `DEFAULT`: only root id 1.
- `CLONE`: writable root created from a snapshot or another root; shares the
  copied version until its first write.
- `DELETING`: a writable root whose tombstone walk is in progress; new opens and
  mounts are refused, and the root item is removed when the walk completes.

### 5.2 Root ids

| Root id | Content | Key | Value |
|---:|---|---|---|
| 1 | default subvolume FS tree | FS key (§6.1) | inode, directory, extent or xattr item |
| 2 | root metadata tree | 8-byte `meta_id` | `u64 next_root_id`, `u64 next_ag_id`, `u64 flags` |
| 3 | AG tree | `u32 ag_id` | 96-byte AG descriptor (§7.1) |
| 4 | quota tree, optional | `u64 root_id` | quota limits and usage |
| 5 | key tree, optional | `u32 key_id` | wrapped key material |
| 6–15 | reserved | — | v1 rejects these root ids |
| 16+ | extra subvolume, snapshot or clone root | FS key (§6.1) | inode, directory, extent or xattr item |

The default subvolume is root id 1, named `default`, with
`ri_tree_id == 1`. Extra roots start at root id 16; the root metadata’s
`next_root_id` is initialized to 16 and `next_ag_id` to `sb_ag_count`. Root ids
are retired, never reused.

Global roots 2–5 have `ri_name_len == 0`, zero UUIDs, zero inode counters and
zero flags. Their `ri_tree_id` equals the root id and their `ri_tree_version` is
zero. The AG tree’s per-AG trees use the AG owner ids from §4.2, not root ids. A
root name is 1..255 bytes with no `0x00` or `/`; names are unique across live
roots and compared bytewise.

A snapshot copies the source root block, level, `ri_tree_id`,
`ri_tree_version`, `ri_next_inode`, key id and compression policy, sets
`SNAPSHOT | READ_ONLY`, and gets a new root id and UUID. A clone copies the same
fields but sets `CLONE` and remains writable. A snapshot of a snapshot copies
the snapshot version, not the current source version.

`SUBVOL_CREATE` allocates a new root id at or above 16, uses that value as
`ri_tree_id`, creates an FS tree containing only root inode 1 with `.` and `..`,
and initializes `ri_next_inode` to 2. Subvolumes do not nest; a directory entry
can never point into another FS tree, and a subvolume appears in the VFS only as
a separate mount.

## 6. FS Tree

Each subvolume, snapshot and clone root names an FS tree version. Inode numbers
are scoped to the writable root’s lineage; directory entries can reference only
inodes in the same tree version. Cross-root rename and hard link return EXDEV.
A writable root’s `ri_next_inode` is the allocator; snapshots copy it but never
allocate.

### 6.1 Keys

FS tree key:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `fk_objectid` | inode number, or directory parent |
| 0x08 | 1 | `fk_type` | 1 inode, 2 directory entry, 3 extent, 4 xattr |
| 0x09 | 1 | `fk_name_len` | 0..255 |
| 0x0A | 2 | reserved | zero |
| 0x0C | 8 | `fk_offset` | logical block for extent items, 0 otherwise |
| 0x14 | variable | `fk_name` | name bytes when present |

Key length is `20 + fk_name_len`. Keys compare by objectid, type, offset, then
name bytes as unsigned values; a shorter prefix sorts first. Inode items use
`(ino, INODE, 0)`, directory entries `(parent, DIR, 0, name)`, extents
`(ino, EXTENT, logical_block)`, and xattrs `(ino, XATTR, 0, name)`.

Directory lookup constructs the exact key. Listing walks the range with
`objectid == parent` and `type == DIR`; `.` and `..` are ordinary entries. Names
are byte strings, not Unicode; FS+ does not normalize, case-fold or validate
UTF-8. The forbidden bytes are `0x00` and `/`; length is 1..255 bytes.

### 6.2 Inode item

Fixed header 96 bytes plus `fi_inline_len` bytes of inline data.

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 2 | `fi_mode` | POSIX type and permissions |
| 0x02 | 2 | `fi_flags` | bit 0 inline data |
| 0x04 | 4 | `fi_uid` | owner user id |
| 0x08 | 4 | `fi_gid` | owner group id |
| 0x0C | 4 | `fi_links` | hard-link count |
| 0x10 | 8 | `fi_size` | file size in bytes |
| 0x18 | 8 | `fi_nbytes` | allocated stored bytes, after compression |
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

A regular file with `fi_size <= 256` and inline flag set stores data in the
inode item. Symbolic link targets up to 256 bytes are inline; longer targets use
extents. Directories have no inline data and no extent items. An encrypted
subvolume disables inline data: small files use one-block extents so every data
byte is encrypted and checksummed consistently.

Inode numbers are unique per writable root lineage and never reused. A retired
number is not reassigned. Writable clones that diverge may allocate the same
numeric inode independently; the roots are separate namespaces, and fsck walks
each root version independently.

### 6.3 Directory entry

Value is 16 bytes: `{u64 de_inode, u8 de_type, 3 reserved, u32 de_generation}`.
The key name is the entry name. Duplicate keys are impossible in one B+tree.
`.` and `..` are stored in every directory; root points both to inode 1.
Directory `fi_size` is the live entry count, a stat hint recomputed by fsck.

### 6.4 Extent item

Extent value is 32 bytes:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `ex_physical` | first physical block |
| 0x08 | 4 | `ex_logical_len` | logical blocks covered, 1..65536 |
| 0x0C | 4 | `ex_physical_len` | stored blocks, 1..65536 |
| 0x10 | 4 | `ex_flags` | bit 0 unwritten, 1 compressed, 2 encrypted, 3 reflink-shared |
| 0x14 | 1 | `ex_compression` | 0 none, 1 LZ4, 2 ZSTD |
| 0x15 | 3 | reserved | zero |
| 0x18 | 8 | `ex_generation` | transaction that created the mapping |

The key offset is the logical block. Extents for an inode are sorted,
non-overlapping, and never cross an AG boundary. A hole is absent; reads return
zeros. An unwritten extent reads zero until written. For an uncompressed
extent, `ex_logical_len == ex_physical_len`. A compressed extent stores
`ex_physical_len` blocks and expands to `ex_logical_len` blocks; its data
checksum covers the stored blocks. `ex_physical + ex_physical_len` must not
exceed the volume or the AG end.

CoW data writes never modify old physical blocks. A write splits overlapped
extent items at logical boundaries. Fully covered logical blocks map to newly
allocated blocks; a partially covered block is copied, patched and remapped;
untouched blocks keep their old mappings. For a compressed extent, a partial
write decompresses, patches and recompresses into new blocks. For an encrypted
extent, the service decrypts with the root key, patches, encrypts and writes new
blocks. Old blocks are either released immediately or recorded as dead for
epoch reclamation.

### 6.5 Reflink

Reflink duplicates extent items and increments the source AG’s reference count
for every extent. It is O(number of extents), not O(blocks), and never copies
file data. A clone between different roots returns EXDEV; cross-root sharing is
provided by the snapshot/clone root mechanism, not by reflink.

The per-AG reference-count tree stores only counts above one, keyed by physical
extent start with `{u32 length, u32 refcount}`. A physical extent with no item
has an implicit count of one. When the last reflink owner drops an extent, the
extent is recorded as dead rather than immediately freed, because an older root
version may still reference it.

### 6.6 Xattr

The key name is the xattr name including its namespace prefix; the value is the
raw value. Xattrs are normal FS tree items. ACL enforcement depends on the
account model, which the current system does not have; mode, uid and gid are
still stored and returned by stat.

### 6.7 Unlink of an open file

The service keeps the inode and its extents alive while an open descriptor
exists, even after `fi_links` reaches zero. When the last handle closes, the
inode item and its extents are removed in a CoW transaction. A crash in that
window leaves an allocated inode with zero links and no directory entry; fsck
reports it as an orphan and reclaims it. The inode number is retired, never
reissued. A snapshot that captured the file before the unlink keeps its own
version and is unaffected.

## 7. Allocation Groups and Parallel Allocation

### 7.1 AG descriptor

The AG tree key is `u32 ag_id`; the value is a 96-byte descriptor:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0x00 | 8 | `ad_start` | first block |
| 0x08 | 4 | `ad_blocks` | usable blocks |
| 0x0C | 4 | `ad_flags` | zero in v1 |
| 0x10 | 8 | `ad_free_root` | free-space tree root |
| 0x18 | 1 | `ad_free_level` | root level |
| 0x19 | 7 | reserved | zero |
| 0x20 | 8 | `ad_ref_root` | reference-count tree root |
| 0x28 | 1 | `ad_ref_level` | root level |
| 0x29 | 7 | reserved | zero |
| 0x30 | 8 | `ad_csum_root` | data-checksum tree root |
| 0x38 | 1 | `ad_csum_level` | root level |
| 0x39 | 7 | reserved | zero |
| 0x40 | 8 | `ad_dead_root` | dead-extent tree root |
| 0x48 | 1 | `ad_dead_level` | root level |
| 0x49 | 7 | reserved | zero |
| 0x50 | 8 | `ad_generation` | AG metadata generation |
| 0x58 | 4 | `ad_checksum` | CRC32C of the descriptor |
| 0x5C | 4 | reserved | zero |

AG 0 starts at block 0 and contains the two superblock slots. Its free-space
tree excludes blocks 0 and 1. The AG size follows the superblock’s adaptive
formula (1 GiB minimum, 1 TiB maximum); the last AG may be smaller. mkfs refuses
an AG that cannot hold the minimum four tree roots and metadata. The AG tree
root is root id 3; each AG’s four tree roots are named by its descriptor.

AG 0 reserves a global-metadata pool of
`min(max(AG0_blocks / 100, 4096), AG0_blocks / 4)` blocks for root-tree and
AG-tree growth. The pool is kept out of normal file allocation. Root and AG tree
CoW nodes come from this pool; if it is exhausted, the service compacts those
trees before returning ENOSPC. File data and per-AG metadata never consume the
reserve. The reserve is permanently marked used in AG 0’s free-space tree, and
fsck preserves it even though it is not reachable as a normal file.

### 7.2 Per-AG trees

| Tree | Key | Value |
|---|---|---|
| free-space | `u64 start_block` | `u32 length`, `u32 reserved` |
| reference-count | `u64 physical_start` | `u32 length`, `u32 refcount` |
| data-checksum | `u64 physical_block` | `u32 crc32c`, `u32 reserved` |
| dead-extent | `u64 physical_start` | `u32 length`, `u64 tree_id`, `u64 birth`, `u64 death`, `u8 kind`, 7 reserved |

Free extents are sorted, non-overlapping and coalesced. A reference-count item
exists only for a count above one; absence means one. A data-checksum item
exists for every allocated file-data block. A dead-extent item exists for every
FS tree metadata block or data extent no longer reachable from its current root.

### 7.3 Parallel prepare, serial publish

Allocation takes the per-AG lock for the target AG, walks that AG’s free-space
tree, reserves blocks, and clones the affected tree paths. Different AGs prepare
in parallel. Tree updates that touch several AGs are merged before the commit
root is built.

The global root-tree commit is serialized. This is the publication point, not
the allocation lock. A generation can contain independent operations from
several AGs; their new AG roots are linked into one root tree and one superblock
slot. Allocation parallelism therefore survives while the final root switch
stays atomic.

Online growth appends a new AG descriptor and adjusts the superblock AG count.
No existing tree is rewritten. Offline shrink works by evacuating an AG,
updating references, and removing its descriptor; FS+ does not claim online
shrink.

## 8. Snapshots, Clones and Epoch Reclamation

A root is deleted only when it is unmounted and has no open descriptors. The
default subvolume cannot be deleted. These checks apply to snapshots, clones and
subvolumes alike.

### 8.1 Snapshot and clone creation

Creating a snapshot or clone is O(1):

1. Allocate a new root id.
2. Copy the source root item’s block, level, `ri_tree_id`, `ri_tree_version`,
   `ri_next_inode`, key id and compression policy.
3. Set `SNAPSHOT | READ_ONLY` for a snapshot or `CLONE` for a writable clone.
4. Insert the new root item in the root tree in one transaction.

No FS tree node is visited. No data block changes its reference count. The
source and the new root share the same immutable tree version. The snapshot or
clone becomes visible only when the new root generation commits.

A snapshot is read-only; every mutating request returns EROFS. A clone is
writable: its first write CoWs only the affected path, and its new root gets a
new `ri_tree_version`.

### 8.2 Why snapshots do not need per-block counts

A block in an FS tree lineage has a lifetime interval `[birth, death)` in
commit-generation space:

- `birth` is the generation that allocated and first published the block.
- `death` is the generation in which the current root stopped referencing it.
- A root version `V` can reference the block only when `birth <= V < death`.

A snapshot root carries the exact tree version it froze. If no live root version
of that `ri_tree_id` falls inside a dead extent’s interval, no snapshot or clone
can reach the block, so it is safe to free. This replaces a per-block snapshot
reference count and makes snapshot creation and deletion O(1).

The check is conservative inside the interval: a version in `[birth, death)`
may not actually reach a particular block, but it keeps the block alive. That is
safe and avoids a full tree walk. Reclamation is deferred and batched, which is
the same trade-off APFS and other CoW file systems make for instant snapshots.

### 8.3 Dead-extent tracking

When CoW replaces a block or a file drops its last reflink reference, the
service records a dead-extent item in the owning AG:

- `physical_start` and `length`;
- `tree_id`;
- `birth` and `death`;
- `kind`: FS metadata or file data.

Dead extents are not in the free-space tree while they wait for reclamation.
The dead tree is per AG, so its reclaimer runs in parallel with other AGs and
never scans the whole volume.

### 8.4 Reclaimer

The service keeps a sorted live-version index per `ri_tree_id`, updated from the
root tree at every commit. The reclaimer is a background worker with a
synchronous path for space pressure. For each dead extent it performs an
interval query against that index. If no live version satisfies
`birth <= V < death`, it frees the extent:

1. Remove the dead-extent item.
2. Remove data-checksum items for its stored blocks.
3. Remove the reference-count item, or assert that the count is zero.
4. Insert the range into the AG free-space tree and coalesce.
5. Commit the new AG and root generation.

Deleting a snapshot removes its root item in O(1); the reclaimer notices that
the frozen version is no longer live and releases every dead extent that
depended on it. Deleting a clone that never wrote is also O(1) because it still
points at the source version. Once a clone has its own current version, deleting
it uses the tombstone walk in §8.6. Snapshot deletion is metadata-cheap; space
reuse is asynchronous, which is the price of not walking the tree during
creation.

Space pressure runs the reclaimer for the allocating AG before returning
ENOSPC. `statfs` reports free blocks and may report reclaimable dead blocks
separately; it never counts a dead block as safely free.

### 8.5 Reflink plus snapshots

A reflink creates an explicit count above one. A CoW write to a reflinked
extent allocates new blocks, decrements the old extent’s count, and records the
old extent as dead only when the count reaches zero. A snapshot does not change
the count; it protects the old extent through its root version. Combining both
is safe: the extent is freed only when the reflink count is zero and the epoch
check finds no live root version in the interval.

### 8.6 Deleting a root

Snapshot deletion is O(1): every block unique to the snapshot was already made
dead when the source replaced it, and every still-live block is shared with a
newer root version. Removing the root item is enough; the reclaimer releases
what the version interval no longer protects.

Deleting a writable subvolume or clone is different. Its current version may
contain blocks that have no dead-extent record. The service therefore marks the
root `DELETING`, refuses new opens and mounts, and runs a background tombstone
walk over that one tree version: each visited metadata block is recorded in the
dead tree, and each visited data extent drops its reflink reference, becoming
dead only when that count reaches zero. When the walk finishes, the root item
is removed and the reclaimer frees what no other live version needs. A
writable-root delete is therefore O(version blocks) in the background, never a
synchronous whole-tree stall. The walk is idempotent: a crash restarts it from
the `DELETING` root, and re-recording an already-dead extent is a no-op.

## 9. Transactions and Durability

### 9.1 Transaction state

A transaction has a new generation, newly allocated blocks, dead extents,
dirty AG roots and a dirty root tree. AG-local allocation may run in parallel;
the final merge and root publication are serialized.

Rules:

- A block allocated in a transaction is never allocated twice.
- A block freed in a transaction is not reused in the same transaction.
- The previous committed root may still reference a freed block until the new
  root is durable, so the two-slot fallback remains valid.
- A tree never points at a block whose checksum, owner or reference state was
  not updated in the same commit.
- ENOSPC aborts before publication. New blocks are unreferenced by the old root
  and remain free in the old free-space trees.
- Dead extents recorded in an aborted transaction never become visible.

### 9.2 Commit sequence

1. Write all file-data blocks and all new non-root tree nodes, including new AG
   tree roots.
2. Flush the block device.
3. Write the new root-tree node last; flush again.
4. Write the alternate superblock slot with the new generation, root block and
   root level; flush.
5. Only after the superblock flush succeeds may old blocks be reused in a later
   transaction.

The old root remains in the other slot. A failure before step 4 leaves it
untouched. A torn new slot is rejected by checksum, and mount chooses the old
one.

The service must issue a real cache flush. If the block driver cannot flush,
FS+ mounts read-only and reports that durability cannot be guaranteed.

### 9.3 Crash outcomes

| Crash point | Mount result |
|---|---|
| during data or metadata writes | previous generation |
| during AG or root-tree write | previous generation |
| torn new superblock slot | previous generation |
| after new slot write, before flush | previous or new generation, whichever device persisted |
| after successful commit | new generation |

Both outcomes are complete trees. There is no journal replay. A block allocated
by an incomplete transaction is absent from the old free-space trees and absent
from the old tree, so it remains reusable. A block freed by an incomplete
transaction remains referenced by the old root and is not free in the old AG
free-space tree. Checksums catch bit rot; they are not a substitute for the
commit ordering.

## 10. Allocation, Checksums and Optional Features

### 10.1 Per-AG allocation

Allocation is first-fit inside the target AG, with a rotating cursor and a
preference for the AG of the inode or the previous extent. A physical extent
never crosses an AG boundary. Free extents are coalesced on free and reclaim.

Metadata for a subvolume prefers the AG of its inode; root-tree and AG-tree
metadata prefer AG 0 or the AG that owns the affected tree. The allocator
reserves room for all new tree nodes before publishing; if the reservation
fails, the transaction aborts.

### 10.2 Data checksums

Every stored file-data block has a per-AG checksum item keyed by physical block.
Writes compute CRC32C over the full 4096-byte stored block and insert or replace
the item. Reads verify before returning data. A missing or mismatched checksum
returns EIO and marks the volume as having an I/O error. Inline data is covered
by the inode’s node checksum. Metadata nodes are always checksummed.

### 10.3 Compression

Compression is feature-gated by `COMPRESS` and a per-root policy. Supported
algorithms are LZ4 (fast default) and ZSTD (higher ratio, optional). A write
compresses a full extent; if the stored size is smaller, the extent is marked
compressed and `ex_physical_len` is smaller than `ex_logical_len`. Partial
writes decompress, patch and recompress into new blocks. Data checksums cover
stored bytes. A file that does not benefit stays uncompressed.

### 10.4 Encryption

Encryption is feature-gated by `ENCRYPT`. A root item carries `ri_key_id`; the
key tree stores wrapped key material and the service keeps unwrapped keys only
in memory. Data extents use AES-256-XTS with a per-extent tweak derived from the
physical block and root UUID; metadata remains checksummed and readable by fsck.
Encrypted roots disable inline data. Snapshots and clones inherit the key id;
cross-root reflink and cross-root hard links are rejected. Key loss means data
loss; there is no recovery key in v1.

### 10.5 Quotas

Quotas are feature-gated by `QUOTA`. The quota tree stores per-root block and
inode limits and usage. Snapshot and reflink sharing is charged to the owning
root by logical bytes; exact shared-space accounting is a later refinement.
Quota checks run before the transaction publishes, so a rejected write leaves
the old generation intact.

### 10.6 Scrubbing and growth

`scrub.fs+` walks every tree and data block, verifies checksums, reference
counts, dead intervals and AG free-space accounting. It runs online against a
snapshot of the current root. Online growth appends an AG and updates the AG
tree and superblock. Offline shrink evacuates an AG, rewrites references and
removes the descriptor.

## 11. POSIX Semantics

| Topic | FS+ behavior |
|---|---|
| File types | regular, directory, symlink, char, block, FIFO, socket |
| Permissions | mode, uid and gid stored; enforcement follows the system account model |
| Timestamps | atime, mtime, ctime, btime with second and nanosecond fields |
| Hard links | non-directories, same root only; cross-root EXDEV |
| Symlinks | target <=256 bytes inline, longer in extents; readlink never follows |
| rename | atomic replace or move, cycle rejection; cross-root EXDEV |
| fsync | data and metadata write, flush, root commit, superblock flush |
| truncate | CoW replacement, zeroed tail, holes on extension |
| sparse | holes read zero; `fi_nbytes` counts allocated stored bytes |
| reflink | extent clone, same root only; O(extents), no data copy |
| snapshots | read-only, O(1) create/delete, asynchronous reclamation |
| clones | writable, O(1) create, independent CoW after first write |
| mmap | read-only page-cache mapping target in M7; v1 file I/O goes through VFS read/write |
| case | byte-exact, case-sensitive |
| max name | 255 bytes |
| max file | 2^63-1 bytes, bounded by volume |
| max volume | 2^48 blocks = 1 EiB |
| max refcount | 2^32-1 per extent |
| snapshots | no writable snapshot; writable clones are separate roots |

Directory `st_size` returns live entry count. `st_blocks` returns
`ceil(fi_nbytes / 512)`. `statfs` reports free blocks from the AG free-space
trees and inode usage from the mounted root item. A dead block is not reported
as free.

## 12. Ring 3 Integration

`/bin/fs+` implements the VFS request window used by `/bin/ext4fs`: one control
page plus the current 4 KiB data window (`FS_DATA_SIZE`), with a state machine
of `IDLE -> REQUEST -> DONE -> IDLE`. A 64 KiB window is a separate ABI change;
FS+ stays correct with 4 KiB requests and chunks larger transfers.

Required operations beyond the current `struct fs_rpc`:

| Operation | Purpose |
|---|---|
| `MOUNT` / `PROBE` | validate a volume, choose a superblock slot |
| `STAT` / `LIST` | inode and directory reads |
| `READ` / `WRITE` | file data with CoW extent replacement |
| `MKDIR` / `RMDIR` / `UNLINK` | namespace mutation |
| `RENAME` | atomic move/replace; two paths |
| `LINK` / `SYMLINK` / `READLINK` | links |
| `TRUNCATE` | set size |
| `REFLINK` | extent clone within a root |
| `SYNC` / `FSYNC` | commit and flush |
| `STATFS` | free, reclaimable and quota statistics |
| `SUBVOL_CREATE` / `SUBVOL_DELETE` / `SUBVOL_LIST` | subvolume management |
| `SNAPSHOT_CREATE` / `SNAPSHOT_DELETE` | O(1) read-only snapshot roots |
| `CLONE_CREATE` | writable clone from a snapshot or root |
| `MOUNT_ROOT` | mount a named subvolume, snapshot or clone |
| `GROW` / `SHRINK` | append or evacuate AGs |
| `SCRUB` | start online verification |
| `FORMAT` | create a new FS+ volume |

The service uses one worker thread per active AG for allocation and tree
preparation, with a serialized commit thread. The AG count of workers is capped
by a configuration value; allocation parallelism does not require one kernel
thread per AG.

File mmap is a later ABI extension. The service maintains a per-file page cache
and exposes read-only pages through a shared-memory capability; write faults are
forwarded to the service as CoW writes. M7 targets read-only `MAP_SHARED`;
writable shared mappings are not promised before the page-cache fault path has
its own crash and revocation tests.

The service receives a per-volume block capability from the VFS. Only the VFS
mints it; it is scoped to the partition range, cannot be widened, and cannot be
delegated. `/bin/ext4fs` moves to the same capability model. The service must
flush at commit points; a driver that cannot flush forces read-only mounting.

Mount startup order: start `/bin/fs+`, `PROBE` and mount FS+; if it fails, try
`/bin/ext4fs`; if that fails, use the kernel FAT32 path and the `/data` mirror.
The default subvolume mounts at `/mnt`; `SUBVOL_LIST` and `MOUNT_ROOT` expose
other roots. Snapshots mount read-only. The ESP is never mounted through FS+.

The installer formats the ESP as FAT32 and the root partition as FS+, then
writes the boot payload to the ESP. The FS+ `FORMAT` operation creates the
superblock slots, AG 0, the root tree, the default subvolume, the AG tree and
the initial per-AG trees. Limine keeps loading everything from the ESP.

## 13. Tools

| Tool | Role |
|---|---|
| `src/fs/fs+/` | core: superblocks, AGs, B+tree, FS tree, refcount/dead/checksum trees, epoch reclaimer, checker |
| `src/apps/fs+/` | Ring 3 service: RPC, block capability, AG workers, commit, reclaimer |
| `mkfs.fs+` | Ring 3 and host-buildable formatter |
| `fsck.fs+` | offline checker and repair tool |
| `scrub.fs+` | online checksum, tree and AG verifier |
| `fs+resize` | online grow and offline shrink |
| `fs+clone` | snapshot, clone and reflink front end |
| `src/tools/fs+-test.rb` | Ruby host matrix |
| `src/tools/fs+-fault.rb` | Ruby crash and reclaimer fault injection |

Repository scripts are Ruby. The formatter, checker and service are C programs.
Host tests build the same core against a file-backed block device; no second
implementation is allowed.

`fsck.fs+` order:

1. Validate both superblocks, AG geometry and the AG tree.
2. Walk the root tree and every root version; validate node checksums, owner,
   level, key order and item bounds.
3. Validate root ids, names, UUIDs, tree versions, snapshot and clone flags;
   resume or complete any `DELETING` root tombstone walk.
4. Walk each FS tree version: inodes, directories, links, extents and xattrs.
5. Validate reflink counts, dead intervals and data checksums.
6. Rebuild each AG’s free-space, reference-count, checksum and dead trees from
   reachable roots, valid reflink references and the permanent AG 0 metadata
   reserve.
7. Recompute free hints, quota usage and directory entry counts.
8. Publish the repair through a normal CoW commit; never patch a live tree.

Repair never writes when it cannot prove the metadata it replaces is safe. A
snapshot or clone root is never silently converted or deleted.

## 14. Testing

### 14.1 Host matrix

- Volumes: 64 MiB, 1 GiB, 16 GiB and a sparse 16 TiB image; one-AG, multi-AG,
  partial last AG, maximum AG count metadata, no free space, fragmented free
  trees.
- Operations: create/read/write/append/truncate, partial blocks, sparse files,
  inline-to-extent promotion, directory splits, long names, rename, hard links,
  symlinks, unlink of open files, ENOSPC, full inode counter, xattrs.
- Snapshots: create and delete 100 snapshots under time bounds that prove no
  tree walk; write the source, verify every snapshot, delete snapshots in random
  order, verify reclaimer epochs and free-space accounting.
- Clones: writable clone diverges, both roots allocate independent inode numbers,
  cross-root rename/link/reflink returns EXDEV.
- Reflink: clone a large fragmented file, modify source and clone, drop each
  reference, verify counts and dead entries, free only when the last root version
  leaves the interval.
- AGs: concurrent writers on different AGs, per-AG ENOSPC, online grow, offline
  shrink evacuation.
- Optional features: LZ4/ZSTD round trips, encrypted extent read/write,
  quota rejection, scrub detection of a flipped data byte.
- After every scenario `fsck.fs+ -n` and `scrub.fs+ -n` must be clean.

### 14.2 Crash and reclaimer fault injection

- Drop writes at every transaction point; mount must return the old or new
  generation with no key loss and no failed checksum.
- Tear the superblock at every byte boundary; the other slot must be selected.
- Crash during snapshot creation and deletion; the root item is either absent or
  complete, never half-inserted.
- Crash during reclaimer commit; dead entries and free-space entries must apply
  together or not at all.
- Simulate stale root versions: keep a snapshot live while dead extents age,
  then delete it and prove the reclaimer frees exactly the blocks it should.
- Corrupt a reflink count, a dead interval, an AG free extent and a data
  checksum; each must fail closed or be repaired.

### 14.3 QEMU

1. Boot the ISO under OVMF and SeaBIOS.
2. Install GPT and MBR targets; both create a FAT32 ESP and an FS+ root.
3. Write files, create snapshots and clones, reboot, verify contents.
4. Modify a source after a snapshot; snapshot stays frozen; clone diverges.
5. Delete snapshots and clones; verify reclaimer frees shared blocks and the
   source remains readable.
6. Kill QEMU during a write and during snapshot creation; remount old or new.
7. Verify `nm out/kernel.elf` has no FS+ symbols and no undefined symbols.
8. Replace the FS+ root with ext4; the ext4 fallback still mounts and writes.
9. Verify the ESP contains only FAT32 boot files and no FS+ metadata.

## 15. Milestones

### M0 — Structures and mkfs

- Freeze the superblock, AG, root, B+tree, inode, extent and root-version
  structures.
- Implement mkfs: superblocks, AG 0, root tree, AG tree, initial per-AG trees,
  default subvolume and root directory.
- Host-buildable `mkfs.fs+`; read-only fsck on a fresh image.
- Acceptance: host matrix formats valid images; `fsck -n` passes.

### M1 — Read-only

- Slot selection, AG tree, root tree, FS tree lookup, directory listing, inline
  files, extents, checksums, snapshots and clones enumerated read-only.
- Acceptance: read tests pass; malformed images fail closed.

### M2 — CoW, AG allocation and commit

- Per-AG free-space allocation, path copying, splits, data CoW, directory
  operations, per-AG locks, serialized root commit, dual-slot durability.
- Acceptance: host write matrix passes; crash injection returns old or new
  generation; parallel AG preparation has no lost updates.

### M3 — O(1) snapshots, clones, epoch reclaimer

- Root-version snapshots, writable clones, dead-extent tree, asynchronous
  reclaimer, space-pressure reclaimer, snapshot delete.
- Acceptance: 100 snapshots created and deleted without walking the source;
  dead extents retained and freed exactly by version intervals; crash tests.

### M4 — Reflink and repair

- Per-AG reflink reference counts, extent clone, count splitting, dead tracking,
  offline fsck, checksum and AG repair.
- Acceptance: reflink matrix passes; fsck rebuilds all per-AG trees; no shared
  block is freed while a count or live version holds it.

### M5 — Optional features

- LZ4/ZSTD compression, AES-XTS encryption, quotas, key tree and quota tree.
- Acceptance: feature-gated volumes pass round trips; unsupported readers mount
  read-only; encryption key loss is documented and tested.

### M6 — Service, installer and compatibility

- `/bin/fs+` RPC, block capability, AG workers, commit thread, installer default,
  ext4 fallback, subvolume/snapshot/clone mounts.
- Acceptance: fresh GPT/MBR installs boot; FS+ mounts at `/mnt`; ext4 volumes
  still mount; ESP intact.

### M7 — Operations and tuning

- Online grow, offline shrink, scrub, TRIM, quota limits, allocation locality,
  reclaimer tuning, read-only file mmap, performance against ext4 and XFS
  benchmarks.
- Acceptance: growth/shrink/scrub/mmap pass; throughput and snapshot latency
  meet published targets; no crash-test regression.

## 16. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Epoch reclaimer frees a block a snapshot still needs | Free only when no live root version lies in `[birth, death)`; fsck recomputes intervals and root versions |
| Snapshot creation slower than expected | It copies one root item and no FS nodes; test 100 snapshots under a bound and reject any implementation that walks |
| Reflink count drift | Per-AG refcount tree updated in the same transaction; fsck rebuilds from extent items |
| AG parallel prepare loses an update | Per-AG locks protect allocation; a serialized root merge publishes one generation |
| Two-slot fallback reads freed blocks | Freed blocks are never reused in the same transaction; previous slot is overwritten only after a new commit |
| Reclaimer causes long stalls | Per-AG workers, batched dead extents, synchronous reclaim only under space pressure |
| Writable-root delete is expensive | `DELETING` tombstone walk runs in the background; snapshots stay O(1) and the root refuses new opens |
| Compression/encryption complexity | Feature-gated with read-only fallback; not claimed until M5 tests pass |
| Encryption key loss | Documented as data loss; no recovery key in v1 |
| Online shrink corrupts references | Offline shrink only; evacuate, rewrite, verify, then remove the AG |
| Ring 3 service holds the block capability | Capability scoped to one partition; cannot be widened or delegated |
| 64-bit arithmetic overflow | Checked helpers against `sb_total_blocks`, AG bounds and 2^48 |
| Ext4 compatibility regresses | Keep `/bin/ext4fs` and its tests; FS+ only mounts when its magic validates |

## 17. Explicit Decisions

1. **Snapshots share root versions.** No per-block snapshot reference counting.
   Snapshot creation and snapshot deletion are O(1); writable-root deletion uses
   a background tombstone walk. Reclamation is epoch-based and asynchronous.
2. **Reflink is extent-level.** Per-AG reference counts, implicit one, explicit
   above one. Reflink is same-root only.
3. **Allocation groups are real.** Per-AG free-space, refcount, checksum and
   dead trees, per-AG locks, parallel prepare, serialized root publication.
4. **Writable clones are separate roots.** Snapshots remain read-only; a clone
   is writable and shares until its first write.
5. **Data checksums are mandatory.** Metadata checksums are mandatory. A checksum
   failure never returns unverified data.
6. **Optional features are gated.** Compression, encryption and quotas ship only
   when their milestones pass; unsupported readers mount read-only.
7. **No journal.** Crash consistency is immutable trees plus two superblock slots.
8. **ext4 remains the fallback.** FS+ does not remove `/bin/ext4fs`, and FAT32
   remains the ESP format because Limine cannot read FS+.
9. **Root pivot later.** FS+ mounts at `/mnt` in the first implementation;
   pivoting `/` is a separate recovery-reviewed project.
10. **No deduplication, RAID or distributed pools.** Those are not FS+ v1 scope.

---

Copyright © 2026 Nexsteaduser. All Rights Reserved.
