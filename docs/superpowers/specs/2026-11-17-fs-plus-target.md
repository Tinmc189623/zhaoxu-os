# FS+ Target

Date: 2026-11-17
Status: Target, not an implementation claim
File system name: `FS+` (exact spelling; paths and identifiers use `fsplus`)

## 1. One Sentence

FS+ makes the state of Zhaoxu OS a versioned object: every committed system
state can be snapshotted, cloned, verified, repaired, encrypted and booted
without copying the file tree.

This document defines the target. It does not justify missing features. If the
implementation design cannot meet a target here, the design changes; the target
does not get relabelled as an acceptable limitation.

## 2. Why the Target Is Not APFS, XFS or ext4

APFS, XFS and ext4 each solve part of the problem:

- APFS has cheap CoW snapshots and clones, but it is closed, Apple-only and not
  designed around Zhaoxu OS’s Ring 3 capability model.
- XFS has allocation groups, parallel allocation, online growth and reflink,
  but no native snapshot/rollback model and no end-to-end self-healing.
- ext4 has maturity, repair tooling and compatibility, but no CoW versioning.

FS+ is not “one more clone of APFS”. It has to be the file system that makes
OS updates, rollback, recovery and integrity one mechanism rather than four
unrelated tools.

The performance floor is parity with the best of the three on the same
hardware. Parity is not the goal. It is the minimum needed to be considered.

## 3. North-Star Capabilities

### 3.1 Versioned OS state

FS+ exposes a version graph, not just files and directories.

- A snapshot is a read-only state in the graph. Creating or deleting one does
  not walk the file tree.
- A clone is a writable state. It shares storage until the first write, then
  diverges copy-on-write.
- Every system update, package transaction and configuration migration creates
  a versioned state.
- The boot path can select a previous state. The kernel and Limine files remain
  on the FAT32 ESP, because Limine cannot read FS+; the FS+ root selector is
  passed to the kernel and the initramfs mounts that state.
- `fsplus-diff`, `fsplus-send` and `fsplus-receive` move a version delta without copying
  unchanged data.
- Retention policies, pinning and automatic pruning are first-class operations,
  not shell scripts.
- A factory reset is deleting a writable state, not overwriting the disk.

Target gates:

- Snapshot and clone creation p99 at or below 5 ms for a tree with one million
  files.
- Snapshot deletion metadata p99 at or below 5 ms; reclamation continues in the
  background.
- A system update creates its rollback state before the first package write.
- Booting a previous FS+ state requires no file copy and no full-volume restore.
- The version graph survives crash, power loss and service restart.

### 3.2 Self-healing integrity

Every stored data block and every metadata object is verified. Corruption is
detected, isolated and repaired when local redundancy exists.

- CRC32C or stronger per stored block; metadata tree nodes checksummed.
- Local mirror and erasure-coded profiles. A single-device bit flip is detected
  on read and repaired from redundancy without taking the volume offline.
- Online scrub walks the version graph, verifies checksums, validates extent
  mappings and repairs degraded profiles.
- A read never returns unverified bytes. A checksum failure returns EIO for that
  range and records a health event.
- `fsplus-health` reports per-AG error counts, scrub progress, degraded profiles
  and repair history.

Target gates:

- 100% of data reads are checksum-verified.
- Single-block corruption is detected in every fault-injection case.
- With a mirror profile, a single-block corruption is repaired without data
  loss and without unmounting.
- Scrub sustains at least 1 GB/s per core on NVMe.
- No silent corruption in the crash, bit-flip and torn-write matrices.

### 3.3 Capability security

FS+ is secure by construction, not by mounting a generic file system read-only.

- Per-subvolume and per-file encryption with authenticated encryption.
- Metadata encryption for names, sizes, directory structure and extent maps.
- Keys are sealed to TPM or a system keyring; key rotation is online.
- Snapshots and clones inherit the source key policy; cross-key sharing is
  rejected.
- The Ring 3 service holds a per-volume capability. It cannot widen its block
  range, read another volume or delegate the capability.
- Snapshots are immutable by policy; a writable state is a separate root.
- Anti-rollback protects security-sensitive states from being silently replaced
  by an older signed state.
- Secure Boot attestation can include the FS+ service and the root state id.

Target gates:

- No plaintext user data, file names, directory entries or extent metadata on
  an encrypted volume.
- AES-256 authenticated encryption sustains at least 2 GB/s per core with
  AES-NI, with no more than 10% overhead on the filesystem benchmark.
- Key rotation does not require a full-volume rewrite.
- A capability escape test from the Ring 3 service fails.
- A rollback to an older state is either explicitly authorized or refused.

### 3.4 Performance and scale

FS+ must not win by being slower and calling that acceptable.

Architecture targets:

- Per-AG allocation and per-AG preparation; one atomic root publication.
- O(1) snapshot and clone metadata operations.
- Extent-level reflink; no data copy on clone.
- mmap through a page-cache window, not a service round trip per page.
- Online growth and offline shrink.
- 1 EiB volume, 2^48 blocks, 4 KiB blocks; 2^64 inode ids per lineage.

Measured gates on the same NVMe and CPU, published with scripts and raw data:

| Metric | Gate |
|---|---|
| Metadata operations per second | at least XFS on the same volume size |
| Small-file create/unlink | at least 1.5x ext4 |
| Sequential read/write | at least 90% of raw device bandwidth |
| Snapshot/clone p99 | at most 5 ms at one million files |
| Snapshot deletion p99 | at most 5 ms metadata; background reclaim bounded per AG |
| Crash recovery to first mount | at most 2 seconds, independent of file count |
| Scrub | at least 1 GB/s per core |
| Encryption | at least 2 GB/s per core with AES-NI |
| Compression | LZ4 at least 1 GB/s per core; ZSTD selectable |
| Reflink of a 100 GiB file | O(extent count), no data copy |
| Multi-AG scaling | throughput grows with AG workers up to the device limit |

No benchmark may compare against a different block size, cache state or dataset
without publishing the difference.

### 3.5 Microkernel-native architecture

FS+ is part of the microkernel boundary, not a kernel file system stacked on a
service.

- The Yuanshuo kernel contains no FS+ parser, no FS+ allocator and no FS+
  transaction code.
- `/bin/fsplus` runs in Ring 3 with a per-volume block capability.
- The service is crash-only: it can be killed and restarted, and the kernel
  returns EIO rather than panicking.
- Commit state is a model-checked state machine; every crash point is injected.
- Online scrub and fsck never patch a live tree in place.
- The VFS protocol is versioned; an incompatible service is refused, not
  guessed at.
- `nm out/kernel.elf` contains no FS+ symbols.

### 3.6 Compatibility and POSIX surface

FS+ must cover the table stakes before claiming to be a general-purpose file
system:

- POSIX files, directories, hard links, symlinks, rename, truncate, fsync.
- Sparse files, reflink, quotas, xattrs, ACLs, case-sensitive names.
- `mmap` with read-only shared mappings first, then writable private mappings
  through the page-cache fault path.
- `statfs`, `fiemap`, `fallocate`, `O_DIRECT`-style bypass where the block path
  supports it.
- ext4 and FAT32 import/export tools. The ESP stays FAT32 because Limine cannot
  read FS+.
- A migration path from ext4 that preserves ownership, modes, xattrs and
  checksums.

### 3.7 Operability

FS+ is observable without a debugger.

- `fsplus-status`: version graph, AG health, reclaimer state, free/reclaimable
  space, worker saturation.
- `fsplus-diff`: version-to-version metadata and data differences.
- `fsplus-send` / `fsplus-receive`: incremental version transfer.
- `fsplus-scrub`: online verification and repair.
- `fsplus-fsck`: offline structural repair.
- `fsplus-clone`: snapshot, clone and reflink front end.
- Structured events for checksum errors, degraded profiles, reclaimer stalls and
  capability violations.

## 4. Explicit Non-Goals

- Network, distributed or clustered file systems.
- General-purpose block-device pooling beyond local mirror/erasure profiles.
- Silent feature degradation. An unsupported read-only-compatible feature mounts
  read-only; an unsupported incompatible feature refuses mount.
- A release label before the gates pass. The project version follows the actual
  `version.cfg`; no artificial “1.0” label is used to excuse missing targets.
- A kernel-side FS+ implementation for convenience.
- Proprietary formats or tools that cannot be audited.

## 5. Release Gates

A release is not called a target release until all of these pass. Missing gates
block the release; they are not converted into deferred notes.

| Area | Gate |
|---|---|
| Versioning | O(1) snapshot/clone, bootable rollback, update transaction, diff/send/receive |
| Integrity | checksummed reads, scrub, single-block repair with mirror/erasure |
| Security | metadata and data encryption, TPM/keyring, key rotation, anti-rollback, capability escape tests |
| Performance | all measured gates in §3.4 on published hardware |
| Scale | 1 EiB simulated image, one million snapshots, multi-AG stress |
| POSIX | full test matrix, mmap, quotas, ACL, xattr, reflink |
| Repair | fsck and online scrub clean after every crash/bit-flip matrix |
| Microkernel | no FS+ symbols in kernel, service restart, EIO containment |
| Compatibility | ext4 fallback, FAT32 ESP, import/export |

## 6. Relationship to the Design Document

`2026-11-17-fs-plus-design.md` describes how the target is implemented. It is
subordinate to this document.

If a design decision cannot satisfy a target gate, the design must change. It
must not be written up as a deliberate limitation. If a target is genuinely
impossible, this document must be amended explicitly, with a measured reason,
before the design changes.

---

Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.
