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

## WenQuanYi Micro Hei (文泉驿微米黑)

- File: `src/initramfs/data/wqy.ttc` → shipped as `/mnt/WQY.TTC` on the FAT32
  data image (built by `make out/data.img`)
- Author: WenQuanYi 社区 (info@wenqiangithub.berlios.de)
- License: GNU General Public License v3 with the font embedding exception
  (see the upstream `GPL` text). The font is only redistributed as a data
  file on the generated volume; it is not linked into the kernel image.

## UEFI / OVMF / QEMU

UEFI firmware (OVMF) and QEMU are development and test dependencies and are not
redistributed by this repository.
