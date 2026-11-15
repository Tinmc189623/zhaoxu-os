# Legacy boot code archive

This directory holds the boot manager sources that Zhaoxu OS used before
Limine became the only boot manager. Nothing here is built, packaged into the
ISO, or installed to a target disk. Keep it for history only.

## What is here

| File | Former role |
| --- | --- |
| `efi_bootmgr.c` | `BOOTX64.EFI` first stage: loaded `OSLDRX64.EFI` |
| `efi_main.c` | UEFI second stage: read the kernel and built the old `boot_info` |
| `bios_stage1.asm` | Legacy BIOS MBR boot sector |
| `bios_stage2.asm` | Legacy BIOS stage 2: VBE, E820, RSDP, kernel hand-off |
| `uefi.h` | Minimal UEFI headers for the two UEFI stages |
| `zhaoxu.cfg` | Old boot manager configuration |
| `zhaoxuenv` | Old environment descriptor |
| `fonts/unicode.pf2` | Menu font for the old boot loader |
| `tools/bios-img.rb` | Built `out/bios.img` from stage 1 and stage 2 |
| `tools/iso-bios.rb` | Patched the ISO9660 kernel extent into stage 2 |

## What replaced it

Limine 12.10.0 in `src/third_party/limine` now owns both firmware paths:

- UEFI: `EFI/BOOT/BOOTX64.EFI` + `limine.conf`.
- Legacy BIOS: Limine's boot sector/stage 2 plus `boot/limine/limine-bios.sys`.
- `src/kernel/boot/limine_boot.c` converts the Limine protocol into
  `struct boot_info` and installs the identity map and boot stack.

The current boot contract lives in `AGENTS.md` and `docs/INSTALL.md`. Do not
reintroduce these files into the build; the ISO rule no longer references
them.
