# Roadmap

This roadmap tracks the work required by the 规则 section of `CLAUDE.md` and the designs in
`docs/superpowers/`. Status is honest about what is implemented, what is only
compiled, and what is still missing. It is not a promise of dates.

## Status at a glance

| Area | State |
| --- | --- |
| Boot | Limine 12.10.0 on UEFI and Legacy BIOS; hybrid ISO and installed ESP boot-tested |
| Kernel | Yuanshuo microkernel with Ring3 drivers, scheduler, IPC, VFS core, ZXE loader |
| Hybrid kernel | Design (M0) plus the core personality API (M1) landed: **OS_5P** = Yuanshuo core + FreeBSD 15.1-RELEASE personality + IO/287 device framework, single image. M2–M5 pending |
| Desktop | Aqua Desktop Environment on Desktop Window Compositor and Desktop Window Manager, software raster path |
| Installer | Graphical GPT/MBR flow, custom ESP/data sizes, FAT32/ext4 formatting, Limine deployment |
| Executables | ZXE v1 PIE, W^X, random load base, Z64 1.0 syscalls 0–77 |
| POSIX | newlib 4.6, libgloss, pipes, dup2, tty/termios, ncurses 6.5; nano 8.4 compiles but is not yet ZXE-linked |
| Graphics API | Software OpenGL 1.1 and a software Vulkan 1.0 ICD; the desktop still uses the ZOS raster renderer |
| Shell/tools | GNU bash 5.3 runs as `/bin/bash`; job control, signals and toybox are pending |
| Networking, audio, USB HID | Not implemented |

## Now: finish the foundations

1. **nano as a real ZXE application.** Relink the compiled nano objects with
   `user/user.ld` and `ld.lld -shared`, pack with `tools/zxe-pack.py`, start it
   from a boot test, edit a file, save it, and exit cleanly.
2. **Split the POSIX glue.** `libgloss.c` currently mixes file I/O, process
   control, terminals, signals, accounts and directory streams. Move each into
   its own file under `user/lib/posix/` so rules 23/29 hold.
3. **Finish the bash surface.** GNU bash 5.3 is cross-built as `/bin/bash`.
   Remaining work is signal delivery, job control (`tcsetpgrp`, `SIGCHLD`,
   `WNOHANG` reaping) and the rest of the POSIX process surface.
4. **toybox.** Port toybox as the source of standard Unix commands required by
   rule 36. Do not add busybox.
5. **Version scheme cleanup.** `version.cfg`, `version.h`, the MOTD, the ISO
   metadata and README must agree before the next release; rule 22 governs
   when a build/version changes.

### Rule gaps still open

| Rule | What it requires | Current state |
| --- | --- | --- |
| 45 | btrfs as the default file system | The installer formats ext4 (`ZHAOXUROOT`); no btrfs implementation exists in the kernel or the Ring3 disk services |
| 1 | csh as the default shell | GNU bash 5.3 is `/bin/bash`; csh is not ported |
| 5 | SPARK-proven drivers | Ring3 drivers are Ada + C; `gnatprove` is not installed and no proof runs |
| 37/38 | Vulkan as the desktop renderer, complete OpenGL 1.1 | Software Vulkan ICD has no WSI; the desktop stays on the ZOS raster path; GL 1.1 is partial |
| 32 | ZAF on Skia | ZAF is not on Skia |

These are tracked here rather than claimed as done; the ISO layout (the media
section of `CLAUDE.md`) is now checked mechanically by `tools/iso-check.py`.

## Next: desktop and graphics

1. **Wayland/GBM/KMS M1+.** Finish the Ring3 `/bin/display` KMS service,
   GBM buffer allocation, and dmabuf sharing described in
   `docs/superpowers/specs/2026-11-14-wayland-gbm-kms-design.md`. M0 transport,
   epoll/eventfd/timerfd and the stock libwayland client core are already
   present.
2. **Vulkan as the default renderer (rule 37).** Add swapchain/WSI, descriptor
   sets and UBOs, compute and depth/stencil to the software ICD, then move the
   Desktop Window Compositor off the raster path.
3. **Complete OpenGL compatibility (rule 38).** Add display lists, evaluators,
   fog, selection/feedback and the remaining fixed-function state so the GL
   subset is a complete 1.1 implementation.
4. **ZAF on Skia (rule 32).** Split the desktop framework from Aqua theming and
   evaluate Skia once the GPU path exists; keep Aqua as the default theme.
5. **Installer hardening.** Recoverable failure paths, progress that reflects
   real block I/O, verification of every installed file, and clearer warnings
   for disks that already contain a system.

## Later

- **OS_5P, the XNU-style hybrid kernel.** Integrate FreeBSD 15.1-RELEASE as an
  in-kernel auxiliary personality (Yuanshuo as the Mach-like core, FreeBSD as
  the BSD-like personality) plus **IO/287**, the in-kernel C++ device framework
  in the IOKit role, all in a single `KERNEL.ELF`, per
  `docs/superpowers/specs/2026-11-20-xnu-freebsd-design.md`. M0 (design) and
  M1 (core personality API, `kernel/personality/` + `kernel/ipc/kport.c`,
  verified by `PERSONALITY_SELFTEST=1` serial markers) are done; M2 has its
  build driver (`tools/freebsd-personality.py`: fetch, checksum, overlay) and a
  native `buildkernel KERNCONF=ZHAOXU` inside the FreeBSD 15.1 VM completes in
  162 s (`kernel` 13.3 MB). Slices 1–7 of the port are in: a
  `FREEBSD_PERSONALITY` gate with a weak boot seam, the linker-side symbols the
  FreeBSD objects need (`_end`, `_etext`, `_DYNAMIC`, `brsection`, `set_*`),
  the versioned `zx_core_services` table with `freebsd_pmap_init()`, a shared
  `core_abi.h` that compiles in both kernels (`probe.c`, `PROBE=0`), and
  `pmap_glue.c`, the first port-side source. The full FreeBSD kernel object set
  plus the glue relocates into one object with only 64 linker-provided symbols
  left; 154 of `pmap.o`'s 171 externals resolve inside the FreeBSD objects.
  Symbol isolation: 19 collisions between 13,287 FreeBSD and 546 Yuanshuo
  globals, handled at link time with `--allow-multiple-definition` (objcopy
  transforms of the merged object are ruled out; they manufacture phantom
  undefined names). The direct link of 1,069 FreeBSD objects plus the 80
  Yuanshuo objects now completes with zero undefined symbols; the blocker was
  the misspelled `brsection` (the real symbol is `brwsection`). The test image
  is 73 MB with debug info (text 5.6 MB, rodata 0.8 MB, data 0.7 MB, bss
  84 MB, ending at `0xffffffff85885580`); `KERNEL_STACK_TOP` moved to
  `0xffffffff87f00000` so that image now links with `LINK=0` under the shipped
  script and ASSERT, and the default ISO still boots to `Boot complete`.
  The 19 duplicate globals are separated in the source: the core's copies are
  `yuanshuo_*`, and `tools/freebsd-link.sh` now reports `0 collision(s)` and
  links without `--allow-multiple-definition` or any object rewriting.
  The WSL2 cross `kernel-toolchain` still fails, and the machine port
  plus IO/287 are not started. M3's core half (boot handoff
  and console service) is in and covered by the self-test; the personality
  still cannot reach `mi_startup` without the M2 port. M4–M5 bring up
  subsystems and syscall routing. CDDL subsystems (ZFS, DTrace) stay unlinked. Requires the owner's r9
  amendment (rule 41) for in-kernel drivers and drivers outside Ring3.
- File system completeness: full ext4 write coverage, FAT32 edge cases, media
  probing, and safe removal.
- Drivers: NVMe, USB xHCI/HID, virtio-gpu 3D, audio, and power management,
  always in Ring3.
- SMP scaling, kernel preemption review, and per-process VFS descriptor
  tables.
- Security hardening: stack protector, KASLR, SMEP/SMAP, IOMMU-backed DMA
  buffers, signed release images, and a documented update path.
- Networking and a package/update story that does not weaken the
  single-ISO install rule.
- Internationalization and accessibility work on top of the MiSans/Inter
  font stack.

## Working rules

- Every item is judged against the 规则 section of `CLAUDE.md`; a feature that violates a rule is
  not done, even if it runs.
- Third-party code is used as shipped (rule 39). No self-developed component
  is presented as an upstream port (rule 42).
- No placeholder code and no simulated results. A milestone is complete only
  when it has a serial log or a screenshot from a real QEMU/hardware run.
- Version numbers change only in `version.cfg`, followed by `make version`.

---

Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved. Email: admin@nexsteaduser.com
