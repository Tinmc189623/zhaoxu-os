# Fedora-sourced btrfs and the glibc toolchain ports

Owner directive 2026-11-26: btrfs comes from Fedora 45; port glibc, gcc,
binutils, gdb, llvm, lua and python to Zhaoxu OS.

## Provenance check (2026-11-26)

No fabricated source is allowed, so the exact Fedora release is recorded per
package.  The Fedora 45 tree is served by the kernel.org mirror:

- `mirrors.kernel.org/fedora/releases/45/Everything/source/tree/` returns 200
  with `repodata/repomd.xml` (repository revision 1793218514) and the source
  package set; `Packages/b/btrfs-progs-7.1-1.fc45.src.rpm` is listed.
- `dl.fedoraproject.org` and `archives.fedoraproject.org` were stale or
  unreachable from this host and are not used.
- `tools/fedora-fetch.py` reads the real `primary.xml.zst`, checks the
  repodata checksum, downloads each SRPM and verifies its SHA-256 against the
  published value before writing it under `third_party/fedora/45/SRPMS/`;
  every download is recorded in `third_party/fedora/45/SOURCES.md`.

## glibc syscall contract (from the current kernel)

The core exposes syscalls 0-84 today.  A glibc port on `sysdeps/x86_64/
zhaoxu` needs these additions; the numbers are Zhaoxu-native, not Linux's:

- memory: `brk` and anonymous `mmap`.  The existing `SYS_MMAP` only maps a
  memfd (`args.fd >= 64`).  An anonymous mapping can be layered on a fresh
  memfd (`memfd_create` + `memfd_truncate` + `memfd_mmap`, then release the
  creator reference because `memfd_mmap` holds one), but the current memfd
  window (`1 TiB + 2 GiB` to `3.5 GiB`), the 64-mapping table and the
  monotonic `g_memfd_va` make it unsuitable for malloc/TLS churn.  A proper
  per-process anonymous region allocator (VMA list plus page mapping) is the
  real prerequisite.
- thread-local storage: `arch_prctl(ARCH_SET_FS)` — the ZXE loader and the
  kernel's user entry currently never set FS, so this is new ABI, not a
  renumbering.
- signals: `rt_sigaction`, `rt_sigprocmask`, `rt_sigreturn`, `tgkill`.
  The tty/scheduler has no signal delivery yet.
- synchronisation: `futex` (wait/wake on a user address) over the scheduler's
  sleep/wake primitives.
- time: `clock_gettime` already has a libc glue implementation over `SYS_RTC`
  and `SYS_UPTIME`; glibc needs the syscall form so the loader can call it.
- process: `set_tid_address`, `gettid`, `exit_group`; fork/exec/pipe/dup2/stat
  exist.
- randomness: `getrandom` (the kernel CSPRNG or a rdrand-backed pool).

Each entry is added behind `syscall_dispatch` with the same pointer
validation as the existing calls, and is verified from a freestanding ZXE
program before glibc is attempted.

## btrfs

Two distinct pieces, both from the Fedora source RPMs:

- **btrfs-progs** (userland: `mkfs.btrfs`, `btrfs`, `fsck.btrfs`,
  `btrfs-convert`) builds against the ported glibc and is the first btrfs
  milestone because it is testable on its own (`mkfs.btrfs` on a memfd, then
  `btrfs check`).  Fedora's spec requires e2fsprogs (convert), libacl, lzo,
  blkid/uuid, libgcrypt, libudev, zlib and zstd; btrfs-progs' own configure
  can drop convert, libudev, lzo, zstd, backtrace, documentation and python,
  and `--with-crypto=builtin` removes libgcrypt.  The irreducible set is
  libuuid (or a vendored UUID implementation) and zlib, both taken from
  Fedora 45 sources like the rest.
- **the kernel filesystem** comes from the Fedora kernel SRPM's `fs/btrfs`.
  Fedora's btrfs is Linux kernel code: it binds to the Linux VFS, page cache,
  workqueue and bio layers.  It cannot be dropped into OS_5P unchanged.  The
  plan ports it as the project's own VFS backend behind the rule-51 provider
  (the same seam `vfs.c` uses today), keeping the on-disk format identical to
  Fedora's so btrfs-progs and the kernel agree.  Rule 45 (btrfs as the default
  root) is the target; until the port is complete the initramfs rootfs stays
  the boot path.

## Order of the ports

1. **Kernel ABI surface for glibc.**  glibc uses a Linux-shaped syscall set
   that the current table (0-84) does not have: `rt_sigaction`/`rt_sigprocmask`,
   `set_tid_address`, `arch_prctl` (FS base for TLS), anonymous `mmap`/`brk`,
   `futex`, `clock_gettime`, `getrandom`, `tgkill`, `mprotect`, `madvise`.
   Add a Zhaoxu-native variant of each (not the Linux numbers) behind the
   same `syscall_dispatch`, and test them from a freestanding program before
   glibc is attempted.
2. **binutils** (Fedora SRPM) cross-configured for `x86_64-zhaoxu-elf`, then
   rebuilt as a host tool once glibc exists.
3. **glibc** (Fedora SRPM) with a new `sysdeps/x86_64/zhaoxu` port: startup,
   TLS (static and dynamic), `sysdeps/unix/sysv/zhaoxu` syscall list, locale
   and nptl minimal.  Dynamic loader support requires `SYS_MMAP` for files
   and the ZXE loader to honour `PT_INTERP`.
4. **gcc** (Fedora SRPM) with the glibc/libgcc pair; freestanding now, native
   after step 3.
5. **gdb**, **llvm**, **lua**, **python** in that order; each Fedora SRPM's
   exact version and checksum is recorded when vendored.

Each step is verified on a disk image (`out/target.img`) rather than an ISO,
per the standing test rule; the ISO remains the installer.

## Immediate next actions

- `tools/fedora-fetch.py`: resolves a package from the Fedora 45 source tree,
  verifies the repodata, downloads the SRPM, checks its published SHA-256 and
  records the provenance ledger; mirrors are probed and fail over.
- `tools/srpm-extract.py` unpacks SRPMs in pure Python (RPM headers plus
  zstd/xz/gzip payload plus cpio newc with path confinement).
- Verified and downloaded: `btrfs-progs 7.1`, `glibc 2.44`, `binutils 2.47`,
  `gcc 16.2.1`, `gdb 17.2`, `llvm 23.1.2`, `lua 5.5.1`, `python3.14 3.14.7`
  (all `.fc45`, ledger in `third_party/fedora/45/SOURCES.md`; SRPM
  containers under the git-ignored `build/fedora/45/SRPMS/`).  btrfs-progs and
  glibc are extracted to `build/fedora/45/`.
- Start with `btrfs-progs` and `glibc` because they define the libc and
  on-disk-format surfaces the rest depend on.

Copyright © 2026 Nexsteaduser. All Rights Reserved.
