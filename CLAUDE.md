# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Priority when sources conflict: `RULES.local.md` > `~/.claude/CLAUDE.md` > `RULES.md` > this file > `AGENTS.md`. `RULES.local.md` is a local override and does not exist unless someone creates it; when it does, it outranks everything above.

## What this is

Zhaoxu OS is an x86_64 microkernel OS. The kernel is **Yuanshuo** (C), the boot manager is **Limine** (UEFI + legacy BIOS), the shell is **GNU bash 5.3**, the desktop is **Aqua Desktop Environment**, the compositor is **Desktop Window Compositor**, and the window manager is **Desktop Window Manager**. Applications use **Z64 1.0** and the **ZXE v1** executable format. Drivers and desktop code run in Ring 3; `kernel.elf` contains only CPU/memory/scheduling/IPC/VFS core.

Names are contractual: never rename or abbreviate Yuanshuo, OS_5P, IO/287, Limine, Aqua Desktop Environment, Desktop Window Compositor, Desktop Window Manager, ZAF, or Z64. `OS_5P` is the hybrid kernel (Yuanshuo core, FreeBSD personality, IO/287 device framework); `IO/287` is the in-kernel device framework in the IOKit role, written exactly that way. Never write "Yuanshuo Shell". `AGENTS.md` is the authoritative, detailed project guide; read it before non-trivial work. Symbol namespaces: `yuanshuo_` for core globals FreeBSD also defines, `io287_` for every symbol IO/287 exports, FreeBSD keeps its own names; `src/tools/freebsd-link.sh` reports any remaining collision instead of letting the linker choose. `docs/superpowers/specs/` and `docs/superpowers/plans/` hold designs, `docs/DRIVER_MODEL.md` holds the udrv/kext/NKE model; `version.cfg` is the only version source.

Branding: the brand is `Nexsteaduser`, always written as one complete word and never shortened or split. Email `admin@nexsteaduser.com`. `RULES.local.md` rule 1 (higher priority) requires the repository documents to carry the combined footer `Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.`; artifacts that belong to one party keep that party's own line (`Copyright © 2026 Nexlyh. All Rights Reserved.` for team-only work, `Copyright © 2026 Tinmc189623. All Rights Reserved.` for personal work).

## Rules Summary

```markdown
- 命名与架构：Yuanshuo 为微内核，OS_5P 为混合内核（含 IO/287、BSD，参考 XNU）；遵循 Unix 哲学与“一切皆文件”，内核可移植，用户程序运行在 Ring3。
- 引导与安装：Limine 原样使用并深度自定义菜单；支持 UEFI/Legacy BIOS、MBR/GPT；单 ISO 安装；ESP 为 FAT32；内核为 KERNEL.ELF，不写入 BOOTX64.EFI。
- 开发与脚本：WSL2 下使用 C/C++/GAS/Ada；脚本限 Python/sh/ps1；代码必须由 AI 生成，禁止人类编写；AI 不可改动规则文件。
- 模块与驱动：微内核保持干净；kext 为内核扩展，udrv 为用户态驱动包（内含 ZXE）；BSD 核心子系统静态编入内核。
- 文件系统与代码组织：完整支持 FAT32/ext4/btrfs，默认根为 btrfs，遵循 FHS；一文件一功能，禁止功能堆叠，遵循 KISS。
- 用户态组件：csh/bash 禁自研；Terminal、bash、toybox 职责分离；禁用 busybox；需支持 Unix 命令；Voyage/Dock/ADE 等按指定命名。
- 第三方与图形：第三方原样移植，Toybox 不裁剪，FreeBSD 15.1 为有限例外；优先复用 stb/FreeType；Vulkan 默认，OpenGL 兼容；Qt6/ZAF/Skia，Aqua 风格，OpenType 字体，支持 8/10bit 色彩。
- 应用与包管理：图标用 SVG/ICO/PNG；默认壁纸固定；Arbor 包管理，后缀 .arb；应用经缓冲区与合成器显示。
- 文档与版权：README 只写最近版本，历史进 CHANGELOG；版权格式固定；禁止水印式展示、编造中文名、自研冒充第三方。
- 版本管理：内核版本与系统版本分开更新；内核代码变化时更新内核版本。
```

## Build, run, test

All build commands run from `src/`.

```bash
# The Makefile is the build system. Windows builds through WSL2:
wsl -d Ubuntu-26.04 -- bash -lc 'cd "/mnt/g/Apps/OS/x86/AMD64/Zhaoxu OS/src" && make -j4'
make -j4                            # Linux/macOS/WSL with the toolchain
make iso                            # out/zhaoxu-os_x64_{os_version}_{build}_ld.iso
make run-iso                        # UEFI QEMU boot from the ISO
make host-test                      # pure-Python host-detection test
make TOYBOX=1 iso                   # add /bin/toybox (WSL clang+lld, ~/toybox)
make TOYBOX=1 TOYBOX_BOOT_TEST=1 ISO_CMDLINE= iso   # toybox boot test

python3 tools/build.py --check         # auxiliary host tool matrix
python3 tools/zxe-pack.py in.elf out.zxe
```

Python is auxiliary only: `tools/*.py` handles ELF→ZXE packing, version syncing, ISO metadata, smoke/screenshot, and optional host probing. `tools/build.py` is a convenience wrapper around make, not the build system.

A clean ISO build also needs `ffmpeg` for `tools/wallpaper-raw.py`; the Makefile reuses `build/ade/wallpaper.rgb` whenever it is newer than the JPG. toybox 0.8.12 is built untrimmed (rule 39) by `tools/toybox-build.sh`; it needs clang/lld, the newlib install from `tools/newlib.py`, and the sources at `$HOME/toybox` — see `docs/TOYBOX.md`. WSL2 is the preferred Windows host. Ada auto-detects `gcc-16`/`gcc-15`/`gcc-14` (Ubuntu 26.04 ships `gcc-16` + `gnat-16`). Native and WSL builds share `build/`; a `make clean` is the safe reset when switching compilers.

Single user program: `make out/hello.zxe` or `make out/net_test.zxe`. The pattern rule compiles `user/<name>.c` with `user/userlib.c`; the entry point is `int user_main(long *stack)`, not `main`. Validate images with `python3 tools/zxe-check.py out/*.zxe`. Convert with `python3 tools/zxe-pack.py in.elf out.zxe`.

Tests:

```bash
make host-test                      # or: python3 tests/tools/host_test.py
python3 tools/ext4-test.py             # ext4 host tests
python3 tools/smoke.py <marker>        # headless serial marker smoke
python3 tools/screenshot.py 3.0 out/shot.png
```

There is no general test suite. After kernel/boot changes do a QEMU smoke: `python3 tools/build.py --check`, a clean rebuild, then check `nm -u out/kernel.elf` is empty and `nm out/kernel.elf` contains no driver symbols (`ahci_*`, `acpi_init`, `serial_init`, `keyboard_*`, `mouse_*`, `pit_init`, `pci_*`). Full validation steps are in `AGENTS.md` under “验证”.

### Kernel-only builds and the M0 boot test

A normal `make out/kernel.elf` also builds the initramfs, which pulls in `/bin/drivers.udrv` and its Ada sources. On hosts without `x86_64-elf-gcc`, link the kernel while treating the existing initramfs as up to date:

```bash
make -o out/initramfs.tar out/kernel.elf
```

`M0_BOOT_TEST=1` builds a kernel that spawns `/data/NET_TEST.ZXE` at boot and halts: it covers `socketpair`, `poll`, `memfd`, `mmap`, `SCM_RIGHTS`, and `O_NONBLOCK`. `M5_BOOT_TEST=1` does the same for `/data/M5_TEST.ZXE`, covering `eventfd`, `timerfd`, and `epoll`. The Makefile stamps keyed by both flags rebuild `kernel/main.o` when they toggle.

```bash
make -o out/initramfs.tar M0_BOOT_TEST=1 out/kernel.elf
make -o out/initramfs.tar M5_BOOT_TEST=1 out/kernel.elf
make out/wlstock.zxe
make WLSTOCK_BOOT_TEST=1 iso
```

The ISO is built with Limine's hybrid BIOS/UEFI El Torito images (`third_party/limine/bin`), `build/limine-host bios-install`, and `boot/limine.conf`; the kernel implements the Limine boot protocol. If `xorriso` is unavailable, stage Limine's `BOOTX64.EFI`, `limine.conf`, `kernel.elf`, and the font/wallpaper modules on a FAT image and boot it over OVMF or SeaBIOS. Success is `[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS` and `NET_TEST exit 0`.

## Architecture

**Boot chain.** Rule 31 target: `firmware → Limine → OS_5P + kext loading + vga10h.kext → Orchestrd (PID1, launchd role) → compositor window service + Thresh (loginwindow role) → login → Voyage desktop`. Implemented today: `Limine → kernel.elf`, then the kernel starts `/bin/display.udrv` (KMS) and `/bin/orchestrd`, which owns the desktop service: it starts `/bin/aqua`, waits, and restarts it. The kext loader, Thresh (loginwindow) and Voyage (desktop shell) do not exist yet, and Aqua still spawns its clients; a direct aqua spawn remains as the fallback when orchestrd is absent. The ISO install entry bypasses the desktop entirely; the ISO's default Limine entry passes `cmdline: install`, and the kernel then starts `/bin/apps/install.zxe` instead of the live desktop (a setup disc, not a live ISO with a hidden installer); setup runs on an 80x25 framebuffer text console (`kernel/lib/fbcon.c`, VGA colours, scrolling) so the text UI is visible with no display service, the boot animation plays on setup media too, and kernel diagnostics are kept off that screen while the installer owns it; `kernel/boot/limine_boot.c` implements the Limine protocol, installs a 64 GiB identity map over Limine's HHDM page tables, maps the reserved kernel stack window, and translates Limine's memory map, framebuffer, RSDP, executable address and modules into `struct boot_info` (magic `0x5A4F5331`). Limine loads `casper/vga10h.kext`, `MiSans.ttf` and `WALL.RGB` as modules; the installed ESP carries `KERNEL.ELF`, `MiSans.ttf` and `WALL.RGB` next to `limine.conf` because Limine 12 has no ext4 driver. The old in-tree boot manager sources live in `archive/boot-legacy/` and are not part of the build chain. A boot animation paints the Limine framebuffer from early boot: `kernel/lib/fbdraw.c` provides the primitives and `kernel/lib/splash.c` draws the gradient, the rotating twelve-dot emblem, the stage label and the progress bar, advancing on the timer interrupt and stopping before Aqua's first present (`SPLASH_BOOT_TEST=1` animates a full bar, freezes the frame and halts for screenshots); setup discs skip it.

**Microkernel boundary.** `kernel/` owns CPU, memory, scheduling and IPC; VFS belongs to the BSD layer (rule 51). Until the personality's vnode layer runs, the core keeps a transitional provider behind `vfs_set_provider()` — the personality registers its own `struct zx_vfs_provider` through the service table (`core_abi.h` v4) and every path operation then routes to it. FreeBSD 15.1 is the required implementation for **VFS, TCP/IP, cdev, pf and NFS**: those subsystems come from the vendored FreeBSD kernel (in-kernel for cdev/pf/NFS/TCP, through the BSD vnode layer for VFS), never from a replacement written in the core. Self-written code in those areas is limited to the transitional boot path and the IPC/descriptor plumbing that already exists (AF_UNIX sockets, zfd), which does not implement IP. Hardware drivers live in Ring3 `/bin/drivers.udrv` (Ada + C), sharing four physical pages (`kernel/include/dev.h`) for block I/O, RTC, input rings, and kernel logs. `/bin/ext4fs` holds the trusted block capability. Never link drivers or desktop/app code into `kernel.elf`.

**Addressing and ABI.** Kernel VMA `0xffffffff80000000`, kernel stack top `0xffffffff87f00000` (a 128 MiB window: the FreeBSD personality's bss needs ~88 MiB), kernel heap `0xffffffff90000000`; physical memory is identity-mapped up to 64 GiB. User images load in `[1 TiB, 1 TiB + 1 GiB)`; user stack top is `0x10100000000` (32 pages). The initial stack is `argc`, `argv`, a default `envp` (`PATH=/bin:/usr/bin`, `HOME=/root`, `USER=root`, `SHELL=/bin/csh`, `TERM=zhaoxu`) and an auxv carrying `AT_PHDR/PHENT/PHNUM/PAGESZ/BASE/ENTRY/UID/EUID/GID/EGID/RANDOM/EXECFN`, so a C library can start without help; `kernel/proc/elf_loader.c` maps ET_EXEC segments, sets `brk` after the highest segment, and ZXE images ignore everything past `argv`. Syscalls are `int 0x80`, `rax` = number, `rdi/rsi/rdx/r10` = args; numbers live in `kernel/proc/syscall.c` and `user/userlib.h` and must stay identical (currently 0–112; `SYS_INSTALL_ERROR` is 68, `SYS_REBOOT` 69, `SYS_INSTALL_PROGRESS` 70, `SYS_INSTALL_CONFIG` 71, `SYS_FORK` 72, `SYS_EXEC` 73, `SYS_PIPE` 74, `SYS_DUP2` 75, `SYS_IOCTL` 76, `SYS_STAT` 77, `SYS_GETPPID` 78, `SYS_CONSOLE_COLOR` 79, `SYS_PTY` 80, `SYS_KLOG` 81, `SYS_MKDIR` 82, `SYS_RMDIR` 83, `SYS_RENAME` 84, then the glibc-facing block: `SYS_BRK` 85, `SYS_MPROTECT` 86, `SYS_MADVISE` 87, `SYS_ARCH_PRCTL` 88, `SYS_SET_TID_ADDRESS` 89, `SYS_GETTID` 90, `SYS_EXIT_GROUP` 91, `SYS_FUTEX` 92, `SYS_RT_SIGACTION` 93, `SYS_RT_SIGPROCMASK` 94, `SYS_RT_SIGRETURN` 95, `SYS_TGKILL` 96, `SYS_KILL` 97, `SYS_GETRANDOM` 98, `SYS_CLOCK_GETTIME` 99, `SYS_SET_ROBUST_LIST` 100, `SYS_PRLIMIT64` 101, `SYS_GETRLIMIT` 102, `SYS_UNAME` 103, `SYS_ACCESS` 104, `SYS_READLINK` 105, `SYS_LAST_ERROR` 112). Calls 0–85 keep the legacy `-1` failure convention with `SYS_LAST_ERROR` returning the recorded errno; 86 and up return `-errno` directly. `mmap` 54 serves memfd mappings unless `fd == -1` with `MAP_ANONYMOUS` (0x20), which selects the per-process anonymous allocator in `kernel/proc/anon.c` (48 GiB window at `1 TiB + 16 GiB`, 128 regions, 512 MiB per mapping). `SYS_KLOG` reads the 64 KiB kernel log ring (`kernel/lib/klog.c`) with the Linux klogctl types; `dmesg` uses it. `SYS_MKDIR`/`SYS_RMDIR`/`SYS_RENAME` route through `vfs_mkdir`/`vfs_rmdir`/`vfs_rename` to the rootfs copy-up layer (ramfs `wramfs_*`; a lower-only file is copied into the upper layer before a rename) or the registered BSD provider. `fork()` copies the caller's pages and descriptor table and resumes the child after the syscall with `rax=0`; `exec()` loads a new image into a fresh address space and rewrites the interrupt frame. `pipe()` is a 64 KiB ring object in `kernel/ipc/pipe.c` shared by two zfd descriptors; `dup2()` installs a descriptor at an exact zfd number or redirects fd 0/1/2. `kernel/proc/tty.c` gives fd 0/1/2 canonical/raw termios, echo, window size 80x25 and FIONREAD; `ioctl(TCGETS/TCSETS/TIOCGWINSZ)` mirrors `kernel/include/tty.h` in `user/userlib.h`. newlib 4.6 is the C library: `tools/newlib.py` builds a PIC x86_64-elf libc with clang, and `user/lib/posix/libgloss.c` plus `crt0.S` implement the syscall stubs and `main(argc, argv, envp)` startup. `NLIBC_BOOT_TEST=1` runs a printf/malloc program and an ncurses screen test. ncurses 6.5 is built out of tree against newlib with `user/lib/posix/include` supplying dirent/glob/termios/ioctl/resource headers; the tty terminfo entry is `initramfs/usr/share/terminfo/z/zhaoxu`. nano is not compiled yet: gnulib's portability headers still conflict with newlib's signal surface.

**New IPC layer (M0).** `kernel/ipc/zfd.c` is a per-process descriptor table starting at 64 (`ZFD_BASE`), separate from legacy VFS descriptors 3–18. `unix_socket.c` implements AF_UNIX stream endpoints (`socket/bind/listen/connect/accept/socketpair`, 64 KiB receive rings, blocking via 1 ms scheduler sleeps). `memfd.c` provides shared-memory files with `mmap/munmap` at `1 TiB + 2 GiB`. `proc/io_syscall.c` carries Linux-compatible `sockaddr_un`/`iovec`/`msghdr`/`cmsghdr` layouts, `SCM_RIGHTS` (memfd only), `poll`, and `fcntl(O_NONBLOCK)`. `SCM_RIGHTS`-transferred descriptors are refcounted, not copied. `eventfd.c`, `timerfd.c`, and `epoll.c` add the event-loop objects behind syscalls 61–67; epoll registrations are level-triggered and capped at 32 per object. Named-port control messages are capped at `IPC_MSG_MAX` (256 bytes) and the ADE desktop protocol carries up to `ADE_TEXT_MAX` (160 bytes, including the terminator) per text command; `ade_text` truncates on a UTF-8 boundary.

**Wayland/GBM/KMS direction.** The mandatory target architecture is documented in `docs/superpowers/specs/2026-11-14-wayland-gbm-kms-design.md`: Wayland 1.x core + xdg-shell + wl_shm/dmabuf, buffers allocated through GBM, GPU render/compose, then KMS atomic commit and page flip. M0 (transport) is implemented and verified; M1 is the Ring3 `/bin/display` KMS service (virtio-gpu 2D resource/backing/set_scanout/flush, double-buffered flip) plus GBM. The kernel now provides epoll, eventfd, and timerfd. `src/third_party/wayland` vendors libwayland 1.23.1's client core plus generated core/xdg-shell bindings; `src/third_party/libffi` carries the x86_64 SysV `ffi_call` subset; `src/user/lib/wayland/compat.c` supplies libc, errno, pthread and socket entry points, and `wayland-os-zos.c` replaces the Linux descriptor-flag helpers. `make out/wlstock.zxe` builds `/data/WLSTOCK.ZXE`; `WLSTOCK_BOOT_TEST=1` starts `/bin/wayland` before it and the serial log must end with `[wlstock] WLSTOCK exit 0` and a server-side `commit 320x240` line. Unmodified stock applications (`weston-terminal`) still need the remaining libc/`shm_open` surface and application migration; dma-buf remains open with M4.

**Desktop stack.** `/bin/aqua` is the Ring3 display server. `src/apps/ADE/main.cpp` is entry/registration; `wallpaper.cpp`, `menubar.cpp`, `dock.cpp` own chrome; `winmgr/dwm.cpp` owns z-order, focus, dragging, close, input, damage; `compositor/` owns the system-RAM back buffer, damage-only present, and cursor. Clients use `user/lib/include/ade_protocol.h` + `ade_client.h`; commands are replayed through `kernel/gfx/render.c` (Z64 1.0), which is shared with the kernel. Cursor themes are PNG frames plus a text `cursor.theme` descriptor under `/usr/share/cursors/Aqua`; `compositor/cursor.c` parses it and `cursor_stb.c` decodes with the vendored `stb_image.h`.

**Installer and graphics APIs.** `/bin/apps/install.zxe` is a text-mode setup program in the Windows XP i386 tradition: it reads keys from the tty in raw mode and walks welcome, licence, disk selection, GPT/MBR and data-partition choice, format warning, progress and restart. It runs on the kernel console without the compositor. It spawns a `worker` copy of itself; the worker blocks in `SYS_INSTALL_PROGRESS` while the parent reads `done/total` from a shared-memory page and prints progress. The custom-partition screen sends ESP/data sizes through `SYS_INSTALL_CONFIG`; the kernel writes the optional third FAT32 `ZHAOXUDATA` partition and formats it. `SYS_INSTALL` writes the MBR/GPT, Limine's BIOS boot sector/stage 2, the ESP files (`EFI/BOOT/BOOTX64.EFI`, `EFI/BOOT/limine.conf`, `boot/limine/limine-bios.sys`, plus `KERNEL.ELF`, `MiSans.ttf` and `WALL.RGB` under `/boot/limine` on the ESP) and formats the ext4 `ZHAOXUROOT` volume; the ext4 root holds no boot files because Limine 12 reads FAT and ISO9660 only. Failures arrive through `SYS_INSTALL_ERROR`, and `SYS_REBOOT` resets the machine from the done screen. `src/user/lib/opengl` is a software OpenGL 1.1 immediate-mode implementation (matrix stacks, ortho/frustum, primitives, depth, culling, RGBA textures, `GL_LIGHT0..7` lighting/materials, blending, alpha test, scissor, `glReadPixels`), built with XMM and covered by `GL_BOOT_TEST=1`. `src/user/lib/vulkan` is a software Vulkan 1.0 ICD with a SPIR-V interpreter and CPU rasterizer, covered by `VK_BOOT_TEST=1`; it has no WSI, descriptors/UBOs, compute or depth, and the desktop still uses the ZOS raster renderer, so rules 37/38 are not met.

**Storage and install.** `/` is the initramfs-backed rootfs (initramfs lower + ramfs copy-up); `/mnt` is the FAT32 `ZHAOXUDATA` volume or, without a data disk, the embedded `/data` mirror. The initramfs embedded in `kernel.elf` is a ZIR1-wrapped zlib stream (`tools/initramfs-compress.py`); `kernel/fs/initramfs.c` inflates it before mounting rootfs, which keeps the image small enough for the legacy-BIOS stage 2 buffer. Installation is self-contained in `kernel/fs/install.c` (single ISO, no extra media): it writes MBR or GPT, formats a FAT32 ESP (`ZHAOXUEFI`) and `ZHAOXUDATA`, and copies the UEFI loader plus a synthesized single-segment kernel ELF.

**User executables.** ZXE v1 is a PIE linked at RVA 0 (`-fpie`/`-fPIC`, `user/user.ld`), packed by `tools/zxe-pack.py`, loaded at a random 64 KiB-aligned base inside `[1 TiB, +1 GiB)` with W^X enforced (CODE=RX, RODATA=R, DATA/ZERO=RW). Dependencies load from `/lib` by plain filename. Kernel and user code are freestanding: no SSE/red zone except the Ring3 font sources; no libc, STL, exceptions, or RTTI in the kernel.

**Fatal screen.** A kernel exception paints the Limine framebuffer red and renders the `KERNEL PANIC` banner from `ascii.md` with the public-domain 8x8 font (`kernel/lib/panic.c`), then halts; the kernel console has no framebuffer sink before the Ring3 display service runs, so the panic path draws directly and also writes one report line to COM1. `PANIC_BOOT_TEST=1` boots into that screen; `tools/screenshot.py` captures it (the red screen is verified).

**Fedora 45 ports.** Owner directive 2026-11-26: btrfs comes from Fedora 45 and glibc, gcc, gdb, binutils, llvm, lua and python are ported from the same tree. `tools/fedora-fetch.py` reads the release's real `repodata`, downloads each source RPM and verifies its published SHA-256 before use; the provenance ledger is `third_party/fedora/45/SOURCES.md`, the plan is `docs/superpowers/plans/2026-11-26-fedora45-toolchain.md`. btrfs-progs provides `mkfs.btrfs`/`btrfs`/`fsck.btrfs`; the kernel filesystem is ported from Fedora's `fs/btrfs` behind the rule-51 VFS provider so the on-disk format matches Fedora's tools. Porting order: the glibc-facing kernel syscall surface, binutils, glibc (`sysdeps/x86_64/zhaoxu`), gcc, gdb, llvm, lua, python.

## Repository rules that change how you work

- All repository scripts must be Python, sh or ps1 (rule 6); code must be generated by AI, never hand-written.
- One file does one job. Do not create catch-all files such as a single `shell.cpp`.
- The kernel must stay driver-free and app-free; Ring3 code goes through the documented syscalls and protocols.
- `version.cfg` is the only place to edit versions (`os_version`, `kernel_version`, and the `YYYY-MM-DD` `build_date`); run `make version` in `src/` afterward. Update `build_date` for every routine build/revision; major = epic change, minor = 10+ features, patch = routine.
- `README.md` holds only the latest release; all history goes in `CHANGELOG.md`. README must keep Emoji/HTML.
- Sources of truth: `AGENTS.md` for rules and invariants, `docs/superpowers/` for designs, `version.cfg` for versions.
- Push priority: GitHub is the primary remote; GitCode is synced only for releases; Gitee is ignored (do not commit or push). Branches: GitHub `main`, GitCode `main`, Gitee `master`. Pushing from WSL fails without credentials; use Windows Git for `git push github main` and only `git push gitcode main` on release.
