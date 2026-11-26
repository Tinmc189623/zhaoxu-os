# Security Policy

Zhaoxu OS is an experimental x86_64 operating system. This policy covers the
Yuanshuo kernel, the Ring3 drivers and desktop, the Limine integration, the
installer, and the ZXE/Z64 user ABI. It is a small project with a best-effort
process, not a commercial product with a security team.

## Reporting a vulnerability

Do **not** open a public GitHub issue for a security problem. Send a report to
**admin@nexsteaduser.com** with the subject line `SECURITY: Zhaoxu OS`.

Include as much of the following as you can:

- The affected component and the version from `version.cfg` (or the commit
  hash / release ISO name).
- Firmware mode (UEFI or Legacy BIOS), machine or QEMU version, and whether
  the problem also reproduces with `make run-iso`.
- Steps to reproduce, including the exact reproduction steps, installer screen,
  or ZXE program involved.
- The serial log (`-serial file:...`) and, for graphics problems, a
  screendump. Remove anything private before sending.
- The impact you observed: crash, hang, memory disclosure, privilege
  escalation, disk corruption, or installer writing the wrong target.
- Whether physical access, a malicious disk image, or a malicious ZXE binary
  is required.

If you want to encrypt the report, say so in a first short message and we will
agree on a channel; there is no published PGP key in this repository.

## What happens next

- We aim to acknowledge a report within five working days, but this is a
  volunteer project and delays are possible.
- We reproduce the problem on the current `main` in QEMU when possible, then
  agree with you on a fix and a disclosure date. The default is 90 days after
  acknowledgement, shortened if a fix and release are ready earlier.
- Credit is given in `CHANGELOG.md` and the release notes unless you ask to
  remain anonymous.
- There is no bug bounty and no payment for reports.

Please test only on hardware, disks, and virtual machines you own or have
explicit permission to use. Do not run denial-of-service tests against shared
infrastructure.

## Scope

In scope:

- Yuanshuo kernel: memory management, scheduler, IPC, VFS, syscall entry,
  ZXE loader, installer (`kernel/fs/install.c`).
- Ring3 services: `/bin/drivers`, `/bin/ext4fs` block capability, `/bin/display`,
  `/bin/aqua`, Desktop Window Compositor and Desktop Window Manager.
- Boot integration: `kernel/boot/limine_boot.c`, `boot/limine.conf`, the
  installer's Limine deployment, and the hybrid ISO layout.
- ZXE v1 loading and the Z64 1.0 syscall surface.
- The newlib/libgloss/ncurses/nano port layer when the bug is in ZOS glue
  rather than upstream code.

Out of scope:

- Limine, newlib, ncurses, nano, FreeType, libwayland, libffi, Vulkan-Headers,
  or other third-party code. Report those upstream; if the ZOS integration is
  involved we will help coordinate.
- QEMU, OVMF/SeaBIOS, host kernels, compilers, and toolchain bugs.
- Hardware errata, firmware bugs, and problems that require disabling the
  documented boot requirements.
- Social engineering, physical attacks, and issues that require modifying the
  ISO or target disk before boot without a trust boundary being crossed.

## Security model and known limitations

These are deliberate properties of the current design, not secrets:

- Drivers and the desktop run in Ring 3. The kernel only contains CPU, memory,
  scheduling, IPC, VFS core, and the terminal/pipe/stat primitives needed by
  Ring3 services.
- ZXE images are loaded at a random 64 KiB-aligned base inside
  `[1 TiB, 1 TiB + 1 GiB)` and enforce W^X: code is RX, rodata is R, data and
  zero-fill are RW.
- `/bin/ext4fs` is the only holder of the trusted block capability. User
  programs cannot drive the raw disk through the block syscalls.
- The kernel and user images are not signed. Secure Boot must be off, and
  Limine is used as shipped in `src/third_party/limine`.
- There are no user accounts, passwords, or per-process UID separation yet.
  The POSIX layer reports a single built-in account.
- There is no network stack, USB HID driver, or secure enclave support.
- Kernel and user code are built without stack protector. There is currently
  no KASLR, SMEP/SMAP, or IOMMU enforcement for Ring3 DMA buffers.
- VFS descriptors 3–18 are still a process-global table; the per-process zfd
  table covers sockets, mempipes, eventfds, timerfds, epoll and pipes. A user
  able to open a file can affect another process that uses the same global
  descriptor number until this is split.
- The installer erases the selected disk from LBA 0. Treat any disk attached
  to a machine running the installer as expendable, and verify the target in
  the confirmation screen.

Do not treat Zhaoxu OS as a hardened multi-user system. Run it in a virtual
machine or on a dedicated test machine.

---

Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved. Email: admin@nexsteaduser.com
