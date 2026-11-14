# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Zhaoxu OS is an x86_64 microkernel OS. The kernel is **Yuanshuo** (C), the boot manager is **ZhaoxuBootMgr** (UEFI + legacy BIOS), the shell is **8sh**, the desktop is **Aqua Desktop Environment**, the compositor is **Desktop Window Compositor**, and the window manager is **Desktop Window Manager**. Applications use **ZOS API 1.0** and the **ZXE v1** executable format. Drivers and desktop code run in Ring 3; `kernel.elf` contains only CPU/memory/scheduling/IPC/VFS core.

Names are contractual: never rename or abbreviate Yuanshuo, ZhaoxuBootMgr, 8sh, Aqua Desktop Environment, Desktop Window Compositor, Desktop Window Manager, ZAF, or ZOS API. Never write "Yuanshuo Shell". `AGENTS.md` is the authoritative, detailed project guide; read it before non-trivial work. `docs/superpowers/specs/` and `docs/superpowers/plans/` hold designs; `version.cfg` is the only version source.

Branding: the brand is `Nexsteaduser`, always written as one complete word and never shortened or split. Email `admin@nexsteaduser.com`; brand copyright `Copyright © 2026 Nexsteaduser. All Rights Reserved.` The team `Nexlyh` has its own `Copyright © 2026 Nexlyh. All Rights Reserved.`; keep brand, team, and personal work separate and do not mix their attribution.

## Build, run, test

All build commands run from `src/`.

```bash
# The Makefile is the build system. Windows builds through WSL2:
wsl -d Ubuntu-26.04 -- bash -lc 'cd "/mnt/g/Apps/OS/x86/AMD64/Zhaoxu OS/src" && make -j4'
make -j4                            # Linux/macOS/WSL with the toolchain
make iso                            # out/zhaoxu_{os_version}_installer.iso
make run-iso                        # UEFI QEMU boot from the ISO
make host-test                      # pure-Ruby host-detection test

ruby tools/build.rb --check         # auxiliary host tool matrix
ruby tools/zxe-pack.rb in.elf out.zxe
```

Ruby is auxiliary only: `tools/*.rb` handles ELF→ZXE packing, version syncing, ISO metadata, smoke/screenshot, and optional host probing. `tools/build.rb` is a convenience wrapper around make, not the build system.

A clean ISO build also needs `ffmpeg` for `tools/wallpaper-raw.rb`; the Makefile reuses `build/ade/wallpaper.rgb` whenever it is newer than the JPG. WSL2 is the preferred Windows host. Ada auto-detects `gcc-16`/`gcc-15`/`gcc-14` (Ubuntu 26.04 ships `gcc-16` + `gnat-16`). Native and WSL builds share `build/`; a `make clean` is the safe reset when switching compilers.

Single user program: `make out/hello.zxe` or `make out/net_test.zxe`. The pattern rule compiles `user/<name>.c` with `user/userlib.c`; the entry point is `int user_main(long *stack)`, not `main`. Validate images with `ruby tools/zxe-check.rb out/*.zxe`. Convert with `ruby tools/zxe-pack.rb in.elf out.zxe`.

Tests:

```bash
make host-test                      # or: ruby tests/tools/host_test.rb
ruby tools/ext4-test.rb             # ext4 host tests
ruby tools/smoke.rb <marker> ...    # headless 8sh command smoke
ruby tools/screenshot.rb 3.0 out/shot.png
```

There is no general test suite. After kernel/boot changes do a QEMU smoke: `ruby tools/build.rb --check`, a clean rebuild, then check `nm -u out/kernel.elf` is empty and `nm out/kernel.elf` contains no driver symbols (`ahci_*`, `acpi_init`, `serial_init`, `keyboard_*`, `mouse_*`, `pit_init`, `pci_*`). Full validation steps are in `AGENTS.md` under “验证”.

### Kernel-only builds and the M0 boot test

A normal `make out/kernel.elf` also builds the initramfs, which pulls in `/bin/drivers` and its Ada sources. On hosts without `x86_64-elf-gcc`, link the kernel while treating the existing initramfs as up to date:

```bash
make -o out/initramfs.tar out/kernel.elf
```

`M0_BOOT_TEST=1` builds a kernel that spawns `/data/NET_TEST.ZXE` at boot and halts: it covers `socketpair`, `poll`, `memfd`, `mmap`, `SCM_RIGHTS`, and `O_NONBLOCK`. Because make does not track `CFLAGS` changes, delete `build/kernel/main.o` when toggling the flag.

```bash
rm -f build/kernel/main.o
make -o out/initramfs.tar M0_BOOT_TEST=1 out/kernel.elf
```

If `xorriso`/`mtools` are unavailable, boot the new kernel over OVMF from a `vvfat` ESP instead of building an ISO: stage `BOOTX64.EFI`, `OSLDRX64.efi`, and `KERNEL.ELF` in `esp/EFI/BOOT/`, add `esp/startup.nsh` containing `\EFI\BOOT\OSLDRX64.efi`, and run QEMU with `-drive if=none,id=esp,file=fat:rw:esp -device ide-hd,drive=esp,bus=ide.0`, OVMF pflash, `-display none -serial file:out/serial.log`. Success is `[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS` and `NET_TEST exit 0`.

## Architecture

**Boot chain.** `ZhaoxuBootMgr → osloader.elf (BIOS) / OSLDRX64.ELF (UEFI) → kernel.elf`; the documented chain ends at the `vga.ko` general graphics driver. UEFI reads `install/KERNEL.ELF` from the ISO or `/KERNEL.ELF` from an ext4 install; BIOS reads `install/KERNEL.BIN` or the ext4 kernel. `boot/efi_main.c` and `boot/bios_stage2.asm` must keep `struct boot_info` (magic `0x5A4F5331`) field order in sync; both place the memory map at `+12 MiB` and `boot_info` at `+14 MiB`, and the linker asserts the kernel ends before `+14 MiB`.

**Microkernel boundary.** `kernel/` owns CPU, memory, scheduling, IPC, VFS. Hardware drivers live in Ring3 `/bin/drivers` (Ada + C), sharing four physical pages (`kernel/include/dev.h`) for block I/O, RTC, input rings, and kernel logs. `/bin/ext4fs` holds the trusted block capability. Never link drivers or desktop/app code into `kernel.elf`.

**Addressing and ABI.** Kernel VMA `0xffffffff80000000`, kernel stack top `0xffffffff80f00000`, kernel heap `0xffffffff90000000`; physical memory is identity-mapped up to 64 GiB. User images load in `[1 TiB, 1 TiB + 1 GiB)`; user stack top is `0x10100000000` (32 pages). Syscalls are `int 0x80`, `rax` = number, `rdi/rsi/rdx/r10` = args; numbers live in `kernel/proc/syscall.c` and `user/userlib.h` and must stay identical (currently 0–57).

**New IPC layer (M0).** `kernel/ipc/zfd.c` is a per-process descriptor table starting at 64 (`ZFD_BASE`), separate from legacy VFS descriptors 3–18. `unix_socket.c` implements AF_UNIX stream endpoints (`socket/bind/listen/connect/accept/socketpair`, 64 KiB receive rings, blocking via 1 ms scheduler sleeps). `memfd.c` provides shared-memory files with `mmap/munmap` at `1 TiB + 2 GiB`. `proc/io_syscall.c` carries Linux-compatible `sockaddr_un`/`iovec`/`msghdr`/`cmsghdr` layouts, `SCM_RIGHTS` (memfd only in M0), `poll`, and `fcntl(O_NONBLOCK)`. `SCM_RIGHTS`-transferred descriptors are refcounted, not copied.

**Wayland/GBM/KMS direction.** The mandatory target architecture is documented in `docs/superpowers/specs/2026-11-14-wayland-gbm-kms-design.md`: Wayland 1.x core + xdg-shell + wl_shm/dmabuf, buffers allocated through GBM, GPU render/compose, then KMS atomic commit and page flip. M0 (transport) is implemented and verified; M1 is the Ring3 `/bin/display` KMS service (virtio-gpu 2D resource/backing/set_scanout/flush, double-buffered flip) plus GBM. Full stock-libwayland compatibility still requires epoll/eventfd/timerfd and dma-buf.

**Desktop stack.** `/bin/aqua` is the Ring3 display server. `src/apps/ADE/main.cpp` is entry/registration; `wallpaper.cpp`, `menubar.cpp`, `dock.cpp` own chrome; `winmgr/dwm.cpp` owns z-order, focus, dragging, close, input, damage; `compositor/` owns the system-RAM back buffer, damage-only present, and cursor. Clients use `user/lib/include/ade_protocol.h` + `ade_client.h`; commands are replayed through `kernel/gfx/render.c` (ZOS API 1.0), which is shared with the kernel. Cursor themes are PNG frames plus a text `cursor.theme` descriptor under `/usr/share/cursors/Aqua`; `compositor/cursor.c` parses it and `cursor_stb.c` decodes with the vendored `stb_image.h`.

**Storage and install.** `/` is the initramfs-backed rootfs (initramfs lower + ramfs copy-up); `/mnt` is the FAT32 `ZHAOXUDATA` volume or, without a data disk, the embedded `/data` mirror. The initramfs embedded in `kernel.elf` is a ZIR1-wrapped zlib stream (`tools/initramfs-compress.rb`); `kernel/fs/initramfs.c` inflates it before mounting rootfs, which keeps the legacy-BIOS `install/KERNEL.BIN` under the stage 2 448 KiB buffer. Installation is self-contained in `kernel/fs/install.c` (single ISO, no extra media): it writes MBR or GPT, formats a FAT32 ESP (`ZHAOXUEFI`) and `ZHAOXUDATA`, and copies the UEFI loader plus a synthesized single-segment kernel ELF.

**User executables.** ZXE v1 is a PIE linked at RVA 0 (`-fpie`/`-fPIC`, `user/user.ld`), packed by `tools/zxe-pack.rb`, loaded at a random 64 KiB-aligned base inside `[1 TiB, +1 GiB)` with W^X enforced (CODE=RX, RODATA=R, DATA/ZERO=RW). Dependencies load from `/lib` by plain filename. Kernel and user code are freestanding: no SSE/red zone except the Ring3 font sources; no libc, STL, exceptions, or RTTI in the kernel.

## Repository rules that change how you work

- All repository scripts must be Ruby; code must be generated by AI, never hand-written.
- One file does one job. Do not create catch-all files such as a single `shell.cpp`.
- The kernel must stay driver-free and app-free; Ring3 code goes through the documented syscalls and protocols.
- `version.cfg` is the only place to edit versions (`os_version`, `kernel_version`, and the positive integer `build`); run `make version` in `src/` afterward. Bump `build` for every routine build/revision; major = epic change, minor = 10+ features, patch = routine.
- `README.md` holds only the latest release; all history goes in `CHANGELOG.md`. README must keep Emoji/HTML.
- Sources of truth: `AGENTS.md` for rules and invariants, `docs/superpowers/` for designs, `version.cfg` for versions.
- Push priority: GitHub > GitCode; Gitee is optional and may be skipped. Branches: GitHub `main`, GitCode `main`, Gitee `master`. Pushing from WSL fails without credentials; use Windows Git for `git push github main` and `git push gitcode main`.
