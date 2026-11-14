# Third-Party Notices

Zhaoxu OS incorporates or redistributes the following third-party material.

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

## WenQuanYi Micro Hei (文泉驿微米黑)

- File: `src/initramfs/data/wqy.ttc` → shipped as `/mnt/WQY.TTC` on the FAT32
  data image (built by `make out/data.img`)
- Author: WenQuanYi Community (info@wenqiangithub.berlios.de)
- License: GNU General Public License v3 with the font embedding exception
  (see the upstream `GPL` text). The font is only redistributed as a data
  file on the generated volume; it is not linked into the kernel image.

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
