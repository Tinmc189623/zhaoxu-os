<div align="center">

# 🖥️ Zhaoxu OS

### Yuanshuo · Aqua Desktop Environment · x86_64

<kbd>🧩 Microkernel</kbd> <kbd>🧵 Ring 3 user mode</kbd> <kbd>💾 FAT32 + ext4</kbd> <kbd>💿 UEFI + Legacy BIOS</kbd> <kbd>🧬 MBR + GPT(ISO)</kbd> <kbd>🎨 Aqua</kbd>

**C** kernel and UEFI boot · **FASM** BIOS boot · **GAS** low-level assembly · **Ada** drivers · **C++20** applications · **Ruby** toolchain

[🌐 OSHUB](https://oshub.org/projects/zhaoxu-os) · [🐙 GitHub](https://github.com/Tinmc189623/zhaoxu-os) · [🦊 Gitee](https://gitee.com/Tinmc189623/zhaoxu-os) · [🦄 GitCode](https://gitcode.com/2503_94276351/zhaoxu-os)

**Current version: 0.7.1 「Yuanshuo」 · kernel 0.7.1 · build 3** · version history in [CHANGELOG.md](CHANGELOG.md)

</div>

---

> 🧭 **In one sentence**: Zhaoxu OS is a microkernel operating system for x86_64. The boot manager **ZhaoxuBootMgr** supports both UEFI and legacy BIOS; **Yuanshuo** is the microkernel written in C; drivers are written in **Ada + C** and run in ring 3; the shell **8sh** and the **Aqua Desktop Environment** desktop also live in ring 3, and the UI is composited by **Desktop Window Compositor**; the VFS spans ext4, UBIFS, tmpfs, initramfs, procfs, sysfs, debugfs, efivarfs, ramfs, rootfs, overlayfs, ecryptfs, vfat, NTFS, ISO 9660/CDFS, UDF, and exFAT; the executable format is **ZXE v1**.

---

## ✨ What's in 0.7.1

### 🚀 0.7.1 fixes and improvements
- The Makefile is the only build entry point and Ruby handles auxiliary scripts only; Windows runs `make` inside WSL2 by default
- Window content is inset and the title bar avoids the rounded corners, fixing content that bled over the window radius; initial windows sit side by side
- The live ISO uses the rule 33 wallpaper directly, and the menu bar is translucent
- `version.cfg` is the single version source and carries a `build` number
- Default QEMU memory is raised to 2 GiB; PMM has been verified managing 8 GiB (64 GiB ceiling)
- The kernel-embedded initramfs is zlib-compressed, bringing the legacy BIOS `KERNEL.BIN` back under the 448 KiB limit
- The ISO layout now ships `boot/zhaoxu/fonts/unicode.pf2`, `casper/vga.ko`, `install/MiSans.ttf`, and `install/Inter.otf`; the UEFI live ISO prefers MiSans
- Fixed `sendmsg` control-message length wraparound; memfd and the kernel heap are capped and grown dynamically at 1/4 of available memory
- M1: new Ring3 `/bin/display` KMS service (modern virtio-pci, 2D scanout, double-buffered flip)
- M2: new `/bin/wayland` and `/bin/wldemo`; Wayland wire/object/wl_shm plus SCM_RIGHTS frame submission, composited onto the KMS scanout, verified in QEMU
- M3: xdg-shell configure/ack, wl_seat/wl_pointer, wl_keyboard keymap/enter, and server-side title bar/close/drag are implemented; in M4, virgl contexts, 3D resource creation, and `CTX_ATTACH_RESOURCE` work, but virglrenderer in QEMU on Windows does not register scanout resources, so GPU composition and dmabuf remain pending
- M5: kernel `eventfd`, `timerfd`, and `epoll` (syscalls 61–67) provide the event-loop primitives stock libwayland needs; `/data/M5_TEST.ZXE` verifies counters, expirations, readiness, and timeouts on real hardware emulation

<table>
<tr>
<td width="50%" valign="top">

### 🗂️ File system matrix
- New read-only **NTFS** (MFT, runlist, $INDEX_ROOT/$INDEX_ALLOCATION), **exFAT** (NoFatChain/FAT, ValidDataLength), **ISO 9660/CDFS** (Joliet + Rock Ridge + multi-extent), **UDF** (AVDP/VDS/ICB/FID), and **UBIFS** (UBI attach, node CRC, zlib decompression)
- The standalone **vfat** driver covers FAT12/16/32 and VFAT long names
- **rootfs** is now a writable root with an initramfs lower layer and ramfs copy-up; **overlayfs** supports writes and `.wh.` whiteouts; new **ramfs** and **ecryptfs** (AES-256-CBC)
- After boot, MBR/GPT/EBR are scanned and recognizable volumes are mounted under `/media/<n>`

</td>
<td width="50%" valign="top">

### 💽 Storage and bus drivers
- Ring3 **NVMe** (admin/I/O queues, PRP, identify) and **SDHCI** (CMD0/8/ACMD41/2/3/9/7/16, PIO reads and writes)
- The **IDE** PIIX4 driver joins `/bin/drivers` and forms the block device pool together with AHCI and USB MSC
- **DesignWare I2C**, **PCH SPI**, and **LM75/TMP102** temperature sensors; `/sys/class/thermal/thermal_zone0/temp`
- USB **OHCI/EHCI** root port reset and controller initialization, alongside the existing **xHCI** storage stack

</td>
</tr>
<tr>
<td width="50%" valign="top">

### 🧠 Kernel platform layer
- Explicit **MMU/page-table driver**: four-level walk, 1 GiB/2 MiB huge-page splitting, map/unmap/protect/translate
- **HPET** ACPI table parsing and 64-bit counter; APIC Timer, PIT, and ARM **SysTick** backends
- A unified interrupt controller frontend for APIC/PIC; the ARM **GICv3** driver lives in `kernel/arch/arm64`
- The device header grows to 16 disks and shares temperature sensor state

</td>
<td width="50%" valign="top">

### 🎨 Desktop and multimedia
- **Desktop Window Compositor** owns the back buffer, damage submission, and hardware cursor on its own; DWM handles window policy only
- **Intel HDA** audio output path (CORB/RIRB, DAC→pin, 48 kHz/16-bit stereo)
- **Intel 8254x/82574L** NIC driver (DMA rings, EEPROM MAC, transmit and receive)
- Legacy virtio-gpu **3D** path: virgl context, 3D resources, and SUBMIT_3D

</td>
</tr>
</table>

---

## 📸 Screenshots

> The ten images below were captured from the framebuffer by `tools/screenshot.rb` after a real boot under QEMU + OVMF at **1920×1080**; they are not drawn mock-ups. Some screens come from 0.5.0 and earlier (kept to show hardware, font, and desktop behavior), and command names follow 8sh in the current release.

<table>
<tr>
<td width="50%" align="center">
<img src="docs/shot-shell.png" width="100%" alt="8sh neofetch">
<br><b>🖥️ 8sh</b><br>
<sub>neofetch: kernel summary and language breakdown</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-files.png" width="100%" alt="Reading files from FAT32">
<br><b>💾 FAT32 data disk</b><br>
<sub><code>ls /mnt</code> and <code>cat</code>; AHCI → FAT32 → VFS → syscall</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-run.png" width="100%" alt="Running .zxe and ELF">
<br><b>🧵 Ring 3 programs</b><br>
<sub>The same hello program run as <code>.zxe</code> and as ELF64, exit code 7</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-editor.png" width="100%" alt="In-system compiler">
<br><b>🛠️ In-system development</b><br>
<sub>The in-system editor from an earlier release: edit, then run the .zxe in ring 3</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-disks.png" width="100%" alt="Disk roles">
<br><b>🗄️ Disks and roles</b><br>
<sub><code>disks</code>: ZHAOXUDATA data partition / ZHAOXUEFI ESP / blank target disk</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-install.png" width="100%" alt="Installer">
<br><b>📥 Installer</b><br>
<sub><code>install</code>: writes MBR/GPT + FAT32 ESP + ZHAOXUDATA to the target disk and verifies it</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-desktop.png" width="100%" alt="Aqua desktop and terminal">
<br><b>🎨 Aqua Desktop Environment · Aqua</b><br>
<sub>Menu bar, windows, Dock, software cursor; a real neofetch running in the terminal</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-desktop-apps.png" width="100%" alt="Desktop applications">
<br><b>🧮 Native desktop applications</b><br>
<sub>Welcome, notes, monitor, calculator; TrueType Chinese typesetting</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-desktop-install.png" width="100%" alt="Graphical installer">
<br><b>💿 Graphical installer</b><br>
<sub>Detects the install source and target disk, writes after confirmation, boots under UEFI/BIOS</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-fonttest.png" width="100%" alt="TrueType glyphs">
<br><b>🔤 TrueType glyphs</b><br>
<sub>TrueType glyph rasterization: 20×23 bitmap, 24 px em (command output from an earlier release)</sub>
</td>
</tr>
</table>

---

## 🧠 Architecture

**Microkernel structure**: `kernel.elf` contains only the CPU/memory/scheduling/IPC/VFS core, with no drivers or applications; AHCI, IDE, NVMe, SDHCI, USB MSC, PS/2, mouse, RTC, serial, HDA, e1000, and the I2C/SPI/temperature sensors are owned exclusively by Ring 3 `/bin/drivers`, virtio-gpu by `/bin/display`; Aqua Desktop Environment, 8sh, `/bin/wayland`, and every application are Ring 3 programs as well (rule 9).

<table>
<tr><th>Layer</th><th>Language</th><th>Implementation</th></tr>
<tr><td>UEFI boot</td><td>🧩 C + GAS</td><td><code>efi_bootmgr.c</code> (ZhaoxuBootMgr) → <code>efi_main.c</code> (OSLDRX64): GOP, RSDP, memory map, page tables, <code>ExitBootServices</code></td></tr>
<tr><td>Legacy BIOS boot</td><td>⚙️ FASM</td><td><code>bios_stage1.asm</code> → <code>bios_stage2.asm</code>: VBE, E820, long mode</td></tr>
<tr><td>Kernel</td><td>🔧 C</td><td><code>kernel/</code>: GDT/TSS, IDT, PMM/VMM, heap, processes, VFS; no drivers</td></tr>
<tr><td>File systems</td><td>💾 C (kernel + Ring3)</td><td>initramfs/rootfs/overlayfs/ramfs/ecryptfs, FAT32/vfat, ext4 (<code>fs/ext4</code> + <code>/bin/ext4fs</code>), read-only NTFS/exFAT/ISO 9660/CDFS/UDF/UBIFS</td></tr>
<tr><td>Low-level assembly</td><td>🧱 GAS</td><td>Entry, ISR stubs, user-mode switch, kernel image embedding</td></tr>
<tr><td>Drivers</td><td>✨ Ada + C, Ring3</td><td><code>/bin/drivers</code>: AHCI/IDE/NVMe/SDHCI/USB MSC, PS/2/mouse, RTC, UART, HDA, e1000, I2C/SPI, temperature; <code>/bin/display</code>: virtio-gpu 2D scanout</td></tr>
<tr><td>Shell / desktop / applications</td><td>🚀 C + C++20</td><td><code>apps/csh/</code> (8sh), <code>apps/ADE/</code> (C++20), <code>apps/display/</code> (KMS), <code>apps/wayland/</code> (Wayland core), <code>apps/ext4fs/</code>, and standalone clients under <code>user/</code></td></tr>
<tr><td>Graphics framework</td><td>🎨 C + C++20</td><td>Desktop Window Compositor + Desktop Window Manager; M1 KMS, M2 wl_shm compositing, M3 xdg-shell/seat; <code>zaf/</code> is still the Z0 rasterizer</td></tr>
<tr><td>User programs</td><td>🧵 C</td><td><code>user/</code>: `init`, `hello`, `wldemo`, and other ZXE images; linked at <code>0x10000000000</code> (1 TiB), running in ring 3</td></tr>
<tr><td>Toolchain</td><td>💎 Make + Ruby</td><td>The Makefile is the build entry point; the Ruby under <code>tools/</code> only handles ELF→.zxe, BIOS images, smoke tests, screenshots, version sync, and ISO metadata</td></tr>
</table>

**Memory layout**

```
0xffffffff80000000  kernel virtual base (higher half)
0xffffffff80f00000  kernel stack top
0xffffffff90000000  kernel heap
0x00010000000000    ring 3 program load base (1 TiB)
0x00010100000000    ring 3 user stack top (32 pages)
```

**Syscall ABI**: `int 0x80`, `rax` = call number, `rdi/rsi/rdx/r10` = arguments; user pointers are validated page by page against the process address space.

| # | Call | # | Call |
| :---: | --- | :---: | --- |
| 0 | `exit` | 1 | `write` |
| 2 | `getpid` | 3 | `open` |
| 4 | `read` | 5 | `close` |
| 6 | `uptime` | 7 | `spawn` |
| 8 | `wait` | 9–14 | named port `listen/connect/accept/send/recv/close` |
| 15–18 | shared memory `create/map/unmap/close` | 19 | `fb_map` |
| 20 | `mouse` | 21 | `key` |
| 22 | `sleep` | 23 | `cpus` |
| 24 | `rtc` | 25 | `read_path` |
| 26–27 | `port_try_accept/recv` | 28 | `serial_write` |
| 29 | `readdir` | 30 | `disks` |
| 31 | `install` | 32 | `write_file` |
| 33 | `unlink` | 34 | `data_volume` |
| 35 | `dev_alloc_page` | 36 | `boot_font` |
| 37 | `spawn_arg` | 38–42 | block I/O `blk_count/info/read/write/flush` |
| 43 | `socket` | 44 | `bind` |
| 45 | `listen` | 46 | `accept` |
| 47 | `connect` | 48 | `sendmsg` |
| 49 | `recvmsg` | 50 | `poll` |
| 51 | `memfd_create` | 52 | `ftruncate` |
| 53 | `lseek` | 54 | `mmap` |
| 55 | `munmap` | 56 | `fcntl` |
| 57 | `socketpair` | 58 | `boot_wallpaper` |
| 59 | `memfd_phys` | 60 | `map_phys` |
| 61 | `eventfd` | 62 | `timerfd_create` |
| 63 | `timerfd_settime` | 64 | `timerfd_gettime` |
| 65 | `epoll_create1` | 66 | `epoll_ctl` |
| 67 | `epoll_wait` | | |

The full definitions live in `kernel/proc/syscall.c` and `user/userlib.h`, and the two must stay in sync. 59/60 are granted only to the kernel-started `/bin/display` (`display_cap`); user processes cannot read the physical address of a scanout page or map a device BAR.

---

## 🚀 Boot flow

**UEFI path**

1. Firmware loads `EFI/BOOT/BOOTX64.EFI` (**ZhaoxuBootMgr**, a PE32+ application built from C with `lld-link`).
2. The boot manager loads `OSLDRX64.ELF`, which fetches the GOP framebuffer, the ACPI RSDP, and the UEFI memory map.
3. The kernel is read from `install/KERNEL.ELF` at the disc root (ISO) or `KERNEL.ELF` under an installed ext4 root, copied into page-aligned physical memory below 4 GiB, and low identity mappings plus higher-half page tables are built.
4. `ExitBootServices` → switch CR3 → jump to `0xffffffff80000000`.

**Legacy BIOS path**

1. SeaBIOS loads the 512-byte stage 1 from El Torito (or the MBR of an install disk).
2. Stage 1 reads stage 2 with INT 13h (on the ISO, stage 2 already sits inside the boot image, so it far jumps to `0x8000` directly).
3. Stage 2 picks a 32bpp VBE LFB, collects E820, scans for the RSDP, and reads the kernel with EDD LBA48.
4. Page tables are built, the 16 MiB kernel window is zeroed, `boot_info` is written, and the CPU enters long mode.

**Common kernel path**

5. GDT/IDT/PIC → PMM/VMM/heap → CPUID/ACPI/RTC → VFS → fonts; block devices come from Ring3 driver services.
6. Mount the initramfs (`/`) and the data volume (`/mnt`): FAT32 volume label `ZHAOXUDATA`; after installation the ext4 root is forwarded through the VFS to the `/bin/ext4fs` service.
7. `/bin/init` runs automatically in ring 3; with a GOP framebuffer the system starts **Aqua Desktop Environment**, without one it enters **8sh**.

---

## 💻 8sh commands

<table>
<tr><th>Command</th><th>Description</th><th>Command</th><th>Description</th></tr>
<tr><td><code>help</code></td><td>Command list</td><td><code>neofetch</code></td><td>System summary</td></tr>
<tr><td><code>version</code></td><td>OS and kernel versions</td><td><code>uname</code></td><td>System identity</td></tr>
<tr><td><code>echo</code></td><td>Print text</td><td><code>uptime</code></td><td>Running time</td></tr>
<tr><td><code>ls [path]</code></td><td>List a directory</td><td><code>cat &lt;path&gt;</code></td><td>Print file contents</td></tr>
<tr><td><code>write &lt;path&gt; &lt;text&gt;</code></td><td>Create/overwrite an /mnt file</td><td><code>rm &lt;path&gt;</code></td><td>Delete an /mnt file</td></tr>
<tr><td><code>run &lt;path&gt;</code></td><td>Run a .zxe / ELF64 in ring 3</td><td><code>cpus</code></td><td>Online CPU count</td></tr>
<tr><td><code>pid</code></td><td>8sh process ID</td><td><code>disks</code></td><td>Disks and install roles</td></tr>
<tr><td><code>install</code></td><td>Install to a blank target disk (MBR/GPT)</td><td><code>exit</code></td><td>Leave the shell</td></tr>
</table>

Line editing supports backspace and `Ctrl+C` to cancel the current line.

---

## 🧬 Executable format: ZXE v1

```text
zhaoxu> run /data/HELLO.ZXE
hello from ring 3
[zxe] exit code 7
```

- 32-byte header + 40-byte segment table + raw data; segment permissions enforce W^X (CODE=RX, RODATA=R, DATA/ZERO=RW)
- The load base is chosen randomly, 64 KiB-aligned, inside `[0x10000000000, +1 GiB)` (ASLR, up to 64 attempts)
- Dependencies load companion libraries from `/lib` by plain filename; `tools/zxe-fixture-lib.rb` generates test shared libraries
- `tools/zxe-pack.rb` converts ELF64 into `.zxe`; `tools/zxe-check.rb` validates the structure
- The entry point is `int user_main(long *stack)`; the user stack is fixed at `0x10100000000` (32 pages)
- The legacy ELF64 compatibility path remains, and the loader dispatches on the magic number

---

## 📦 Build and run

The build must support four hosts: **native Linux, native macOS, native Windows (MSYS2/MinGW), and WSL2**. **The build system is the Makefile**; Ruby handles only auxiliary scripts (ELF→ZXE, version sync, ISO metadata, smoke tests/screenshots) and optional host probing. On Windows, prefer WSL2 and run `make` inside it.

```bash
cd src
make -j4                         # build everything (Linux/macOS/WSL)
make iso                         # produce out/zhaoxu_{os_version}_installer.iso (UEFI + BIOS)
make run-iso                     # UEFI CD boot (no data disk attached)
make host-test                   # host-detection unit test
ruby tools/build.rb --check      # auxiliary: tool matrix and install hints
```

```powershell
# Windows (PowerShell 7+): forwards to WSL2 and runs make there by default; native MSYS2 only with -HostMode native
./src/build.ps1                  # WSL2 make all
./src/build.ps1 -Check           # tool matrix (Ruby-assisted probing)
./src/run.ps1 -Display gtk       # WSL2 QEMU graphical window
```

For installation on real hardware and in virtual machines, see [docs/INSTALL.md](docs/INSTALL.md) ([Word version](docs/INSTALL.docx)).

**Dependencies**: run `ruby tools/build.rb --check` for an item-by-item check. Linux/WSL needs `gcc`, `g++`, `gcc-14`+`gnat-14`, `ld.lld`, `objcopy`, `clang`, `lld-link`, `qemu-system-x86_64`, OVMF, `mkfs.vfat`, `mtools`, `tar`, `xorriso`, and `squashfs-tools`; macOS uses `llvm`, `lld`, `qemu`, `xorriso`, `mtools`, `dosfstools`, and `squashfs` from Homebrew; native Windows uses the matching MSYS2 packages plus Ruby. Ada drivers need an x86_64-elf GNAT (set with `ADA=`), and FASM must be the host build (set with `FASM=`; on Windows you can also drop it at `src/tools/fasm.exe`).

**Verification tools** (Ruby):

```bash
ruby tools/smoke.rb <marker> [commands]     # headless smoke test that sends 8sh commands one by one
ruby tools/screenshot.rb 3.0 out/shot.png   # boot for real and capture the framebuffer
SMOKE_BOOT=iso-install ruby tools/smoke.rb ... # ISO + blank target disk install test
SMOKE_BOOT=installed ruby tools/smoke.rb ...   # boot from the installed target disk
```

---

## 🗂️ Repository layout

```
Zhaoxu OS/
├── README.md                 current release notes (history in CHANGELOG.md)
├── CHANGELOG.md              full version history
├── LICENSE                   Apache License 2.0
├── THIRD_PARTY_NOTICES.md    third-party code and font notices
├── version.cfg               single source of versions (os_version + kernel_version + build, synced by make version)
├── docs/                     screenshots and architecture/ZAF design docs
└── src/
    ├── Makefile              four-host build rules (Linux/macOS/Windows/WSL)
    ├── boot/
    │   ├── efi_bootmgr.c     UEFI boot manager ZhaoxuBootMgr (C)
    │   ├── efi_main.c        UEFI OS loader OSLDRX64 (C)
    │   ├── bios_stage1.asm   MBR boot sector (FASM)
    │   ├── bios_stage2.asm   real mode → long mode (FASM)
    │   └── fonts/            boot-time unicode.pf2 (Inter + grub-mkfont)
    ├── kernel/
    │   ├── arch/x86_64/      entry, GDT/TSS, ISRs, PIC, ring 3 switch
    │   ├── int/              IDT and exceptions/IRQs/syscalls
    │   ├── drivers/          timing and ACPI/AHCI glue (hardware drivers live in Ring3)
    │   ├── fs/               initramfs, FAT32, VFS, installer
    │   ├── proc/             ZXE/ELF64 loading and syscalls, fd table
    │   ├── ipc/              named ports, shared memory, AF_UNIX, memfd, eventfd, timerfd, epoll
    │   ├── crypto/           AES, SHA-256
    │   ├── mem/              PMM, VMM, MMU, growable heap
    │   ├── lib/              strings, printf, UTF-8 console, inflate
    │   ├── font/             8x8 bitmap + TrueType glyf rasterizer
    │   └── include/          shared kernel headers
    ├── fs/ext4/              OS-independent ext4 core (mount/directory/read/write/allocate/format)
    ├── zaf/                  Zhaoxu Aqua Framework (Z0 raster interface)
    ├── drivers/              Ada/SPARK drivers (AHCI, IDE, NVMe, SDHCI, USB MSC/xHCI, PS/2, mouse, serial)
    ├── apps/                 8sh, Aqua Desktop Environment, display KMS, wayland core, ext4fs, Ring3 driver C glue
    ├── user/                 ring 3 programs, Wayland/ADE clients, wl_demo, user library
    ├── initramfs/            ramfs initial contents, data disk assets and cursor themes
    ├── tests/                host tests (ext4, build host detection)
    ├── tools/                Makefile build entry; Ruby helpers: zxe-pack, iso-*, smoke, screenshot, version
    └── out/                  build products (kernel.elf, ISO)
```

---

## ⚠️ Current limits

- 🧵 Processes and threads: at most 16 processes and 32 threads; preemptive SMP scheduling and `spawn/wait` exist, but there is no `fork/exec`, signals, COW, or swapping
- 🧠 Memory: no demand paging, user pages are mapped all at once at load time; the user stack is fixed at 32 pages; the kernel heap grows on demand up to 1/4 of available memory, memfd is capped at 64 MiB per object and 1/4 in total, and the PMM bitmap tops out at 64 GiB
- 💾 File systems: FAT32/vfat is writable (8.3 short names, 16 MiB per file) and ext4 is writable (Ring3 service, 8 MiB per RPC); NTFS/exFAT/ISO 9660/CDFS/UDF/UBIFS are read-only; initramfs/rootfs/overlayfs/ramfs/ecryptfs are also included
- 🔐 Permissions: no multi-user support, file ownership, or Unix permission bits; all user processes run with the same privileges
- 🧱 Compatibility: no POSIX/libc compatibility layer, the syscall interface is ZOS API 1.0; ZXE v1 loads dependencies from `/lib` by filename and has no ELF dynamic linker; Wayland is a custom wire/object core, so stock libwayland clients cannot run directly yet, although the kernel now provides eventfd, timerfd, epoll, poll, AF_UNIX, SCM_RIGHTS, and memfd
- 🌐 Networking: no TCP/IP stack; AF_UNIX stream sockets, poll, eventfd, timerfd, epoll, SCM_RIGHTS, and memfd exist for Wayland and the M0/M5 transport
- 🔌 Peripherals: input is a PS/2 keyboard/mouse and COM1; storage covers AHCI/IDE/NVMe/SDHCI/USB MSC; HDA, e1000, I2C/SPI, and temperature sensors are present; no USB HID and no full network stack
- 🖼️ Graphics: M1 virtio-gpu 2D KMS scanout and double-buffered flip; composition is still software rasterization; the M2/M3 Wayland core and xdg-shell/seat work; M4 virgl contexts and 3D resources can be created and attached, but virglrenderer in QEMU on Windows does not register 2D/3D scanout resources (`SET_SCANOUT` returns `0x1203`), so GPU composition and linux-dmabuf remain pending; no multi-monitor
- ⚡ Power: ACPI is used for shutdown/reboot only; no sleep or hibernate
- 🛡️ Boot: the boot loader is unsigned, so machines with Secure Boot enabled must turn Secure Boot off first or sign the loader themselves
- ⏱️ Time: RTC local time only; no NTP or time zone handling

---

## 📜 License

This project is licensed under the [Apache License 2.0](LICENSE). Third-party component and font notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

<div align="center">

**Zhaoxu OS · Yuanshuo** · built with C / FASM / GAS / Ada / C++

Copyright © 2026 Nexsteaduser. All Rights Reserved. · admin@nexsteaduser.com

</div>
