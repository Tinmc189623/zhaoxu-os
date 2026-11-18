# Third-Party Notices

Zhaoxu OS incorporates or redistributes the following third-party material.

The Zhaoxu OS project itself — the original Yuanshuo kernel, userland and
tooling — is licensed under the GNU General Public License v2.0 (GPL-2.0-only);
see `LICENSE`. The components below retain their own licenses and are aggregated
with, not relicensed under, the project license.

The XNU-style hybrid kernel design integrates the Linux kernel (7.4-rc3,
`GPL-2.0 WITH Linux-syscall-note`) as an in-kernel auxiliary personality,
preserving its upstream terms. Linux is not yet vendored; its entry is added to
this file at the M2 integration milestone (see
`docs/superpowers/specs/2026-11-18-xnu-linux-design.md`).

## font8x8

- Files: `src/kernel/font/font8x8_basic.h`
- Author: Daniel Hepper <daniel@hepper.net>
- Based on: `font8x8.h` by Marcel Sondaar and the public-domain IBM VGA fonts
- License: Public Domain

The original header carries the following notice:

```
8x8 monochrome bitmap fonts for rendering
Author: Daniel Hepper <daniel@hepper.net>

License: Public Domain

Based on:
// Summary: font8x8.h
// 8x8 monochrome bitmap fonts for rendering
//
// Author:
//     Marcel Sondaar
//     International Business Machines (public domain VGA fonts)
//
// License:
//     Public Domain
```

## Flat Assembler (FASM)

- File: `src/tools/fasm` (Linux x86_64 executable)
- Author: Tomasz Grysztar
- Project: https://flatassembler.net/
- License: Flat Assembler is free to use and redistribute; see the project
  documentation for the full terms.

The binary is bundled only as a build convenience. It is not linked into the
Zhaoxu OS kernel image.

## libwayland

- Files: `src/third_party/wayland/`
- Version: 1.23.1
- Copyright © 2008-2012 Kristian Høgsberg, © 2010-2012 Intel Corporation,
  © 2011 Benjamin Franzke, © 2012 Collabora, Ltd.
- License: MIT (see `src/third_party/wayland/COPYING`)
- The client core (`wayland-client.c`, `wayland-util.c`, `connection.c`) and the
  generated core/xdg-shell protocol bindings are compiled into the Ring3
  `/data/WLSTOCK.ZXE` regression client. The Wayland XML sources are included
  under `protocol/`; the checked-in bindings were produced with the upstream
  `wayland-scanner`.

## libffi

- Files: `src/third_party/libffi/`
- Version: 3.4.6 (x86_64 SysV `ffi_call` subset)
- Copyright © 1996-2024 Anthony Green, Red Hat, Inc and others
- License: MIT (see `src/third_party/libffi/COPYING`)
- libwayland uses libffi to invoke event listeners. The Windows/EFI closure
  paths are not linked; the build keeps only the SysV call path used by the
  client.

## Vulkan-Headers

- Files: `src/third_party/vulkan-headers/include/vulkan/`
- Project: https://github.com/KhronosGroup/Vulkan-Headers
- Copyright © 2015-2026 The Khronos Group Inc.; portions © Valve Corporation,
  LunarG, Inc. and contributors
- License: Apache License 2.0 (see
  `src/third_party/vulkan-headers/LICENSE.md`)
- Only the core C headers needed by the software ICD are vendored
  (`vulkan.h`, `vulkan_core.h`, `vulkan_beta.h`, `vk_platform.h`, `vk_icd.h`).
  The ICD implementation, loader entry points and SPIR-V interpreter are
  original Zhaoxu OS code.

## Limine

- Files: `src/third_party/limine/`
- Version: 12.10.0
- Copyright © 2019-2026 Mintsuki, 2026 Kamila Szewczyk, and contributors
- License: BSD-2-Clause (see `src/third_party/limine/LICENSE`); some bundled
  components are 0BSD, listed in `src/third_party/limine/3RDPARTY.md`
- Limine is the boot manager. Its `bin/` images are packed into the ISO and
  copied to installed ESPs, `include/limine.h` supplies the Limine Boot
  Protocol used by `src/kernel/boot/limine_boot.c`, `limine-bios-hdd.h` is
  the source of the installer's BIOS HDD image (extracted by
  `src/tools/limine-bios-hdd.rb`), and `limine.c` builds the host-side
  `limine bios-install` helper used by `make iso`.

## UEFI / OVMF / QEMU

UEFI firmware (OVMF) and QEMU are development and test dependencies and are not
redistributed by this repository.

## Fonts

- **Inter** — © The Inter Project Authors, SIL Open Font License 1.1.
  Fetched at build time by `src/tools/fetch-fonts.rb`.
- **MiSans / MiSans Global** — © Xiaomi. Distributed under the MiSans font
  licence, which forbids redistributing the font files. They are fetched at
  build time by `src/tools/fetch-fonts.rb` and are not committed to this
  repository.
