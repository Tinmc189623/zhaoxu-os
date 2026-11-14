# Changelog

Full version history of Zhaoxu OS. See [README.md](README.md) for what the current release does; `version.cfg` is the single source of version numbers.

## 0.7.1 「Yuanshuo」 · build 3

### Kernel
- New `eventfd`, `timerfd`, and `epoll` objects in `kernel/ipc/`, registered as zfd descriptor types above `ZFD_BASE`; duplicate and `SCM_RIGHTS` paths keep the existing refcount rules
- New syscalls 61–67: `eventfd`, `timerfd_create`, `timerfd_settime`, `timerfd_gettime`, `epoll_create1`, `epoll_ctl`, `epoll_wait`; `kernel/proc/syscall.c` and `user/userlib.h` stay in sync
- `eventfd` supports `EFD_SEMAPHORE` and nonblocking reads/writes; `timerfd` uses the scheduler tick timeline for relative and absolute expirations, including repeating intervals; `epoll` is level-triggered, caps registrations at 32 per object, and reports only the events requested for each descriptor
- `/data/M5_TEST.ZXE` and `M5_BOOT_TEST=1` cover the event loop: serial output is `[m5] eventfd, timerfd, epoll PASS` followed by `[m5 ] M5_TEST exit 0`

### Documentation
- All project documentation except `AGENTS.md` is now in English; the GitHub repository description is in English too. Product names, commands, paths, code blocks, and the `Nexsteaduser` copyright lines are unchanged

## 0.7.1 「Yuanshuo」 · build 2

### Display
- `/bin/display` no longer hardcodes the control queue size at 64: `virtio-gpu-gl-pci` uses 256 and 2D devices use 64, so the queue size is now validated against the device value and allocated accordingly, fixing `invalid control queue size` on gl devices
- Control commands now check the response header; `0x12xx` responses count as failures, so a failed resource creation is no longer treated as success and carried on into scanout
- 3D resources are sent as a 72-byte `RESOURCE_CREATE_3D` (the extra 4 bytes of padding are required by the QEMU 11.1 uapi), and `CTX_CREATE`, 3D resource creation, and `CTX_ATTACH_RESOURCE` succeed on `virtio-gpu-gl-pci`; `SET_SCANOUT` still returns `0x1203` because virglrenderer in QEMU on Windows registers neither 2D nor 3D resources, so GPU composition and dmabuf remain pending

### Fixes
- The virtio-gpu command numbers in the legacy `apps/drivers/virtio_gpu.c` are back to the canonical values: `GET_CAPSET_INFO` 0x0108, `RESOURCE_CREATE_3D` 0x0204, `SUBMIT_3D` 0x0207; the 3D resource struct gains `padding`
- The `8sh` target now depends explicitly on `kernel/include/version.h`, so the shell banner no longer shows the old build number after `make version`
- When `tools/version.rb` updates `/etc/version` it now replaces the old `build N` too, so `build 2 build 1` no longer appears
- `virtio-gpu-pci` regression: the 64-entry control queue, 2D resources, Wayland frame submission, and the compositor connection all work with no command errors

## 0.7.1 「Yuanshuo」 · build 1

### Build
- The Makefile is the only build entry point; Ruby keeps only auxiliary scripts (ZXE packing, version sync, ISO metadata, smoke tests/screenshots, host probing)
- Windows runs `make` inside WSL2 by default; the PowerShell entry points now forward to WSL make, and native MSYS2 is used only with `-HostMode native`
- Ada compiler detection now checks that `-print-prog-name=gnat1` really exists; when `gcc-16` has no `gnat-16` it falls back to an available `gcc-14`, so `cannot execute gnat1` no longer appears
- When switching between native and WSL builds, backslashes in Windows-style `.d` files are rewritten to `/` so header dependencies survive; this fixes `aqua`/`8sh` still showing old version numbers after `version.h` changes
- The kernel-embedded initramfs is now ZIR1 + zlib and is inflated at boot; the legacy BIOS `install/KERNEL.BIN` stays within the stage 2 448 KiB window

### Desktop
- New `boot_wallpaper` syscall: the live ISO uses the rule 33 wallpaper directly instead of falling back to a plain gradient
- Window content is inset by 8 px and the title bar gradient avoids the rounded corners, fixing content bleeding over the window radius
- Initial windows now sit side by side, so the calculator no longer covers the welcome window
- The menu bar is translucent, window title bars distinguish focused and unfocused states, and shadows and padding are unified

### Display
- New M2 Wayland core: the wl_display/wl_registry/wl_compositor/wl_surface/wl_shm/wl_shm_pool/wl_buffer objects and wire encoding in `/bin/wayland`, with `/bin/wldemo` submitting memfd frames over AF_UNIX + SCM_RIGHTS; the server composites the 320x240 frame into the 1280x800 KMS scanout buffer of `/bin/display` and flips
- New M3 core: `xdg_wm_base`/`xdg_surface`/`xdg_toplevel` configure/ack, `wl_seat`, `wl_pointer` enter/motion/button, `wl_keyboard` keymap/enter; server-side title bar, close button, drag hit testing, and demo toplevel compositing
- M4 groundwork: modern virtio negotiates VIRGL and issues `CTX_CREATE`; QEMU `virtio-gpu-gl-pci` still reports `ctrl 0x103 error 0x1203`, so GPU composition and dmabuf are unfinished
- New Ring3 `/bin/display` KMS service: modern virtio-pci control queue, 2D resource/backing, memfd scanout buffer, double-buffered page flip; the kernel adds restricted `memfd_phys`/`map_phys` (gated on `display_cap`) for scanout pages and PCI BARs
- `apps/drivers` no longer initializes virtio-gpu, and the control queue belongs solely to `/bin/display`; two scanout frames were verified to differ in QEMU at 1280x800

### Versions and memory
- `version.cfg` gains a required `build` number, synced by `make version` into `version.h`, the motd, `/etc/version`, README, and the ISO metadata
- Default QEMU memory is raised from 512 MiB to 2 GiB; PMM has been verified managing 8 GiB (64 GiB ceiling)

### Fixes
- The bounce buffer in `blockfs.c` is allocated as `sectors * 512`, fixing 2352-byte logical blocks running out of bounds and corrupting the kernel heap header, which left every user process unable to start
- `apps/drivers/glue.c` now provides `inw_port/inl_port/outw_port/outl_port`, fixing the missing ZXE imports in `drivers.zxe`
- Fixed integer wraparound in the `sendmsg` control-message length: `cmsg_len` is now compared against the remaining control buffer length, so it can no longer read past the kernel stack
- memfd is capped at 1/4 of available memory, and the kernel heap grows dynamically at 1/4 of available memory instead of a fixed 16 MiB, so a single process cannot exhaust every physical page
- The ISO now ships the files the rules require: `boot/zhaoxu/fonts/unicode.pf2`, `casper/vga.ko`, `install/MiSans.ttf`, and `install/Inter.otf`; the `dists`/`pool` directories are kept
- The UEFI live ISO loads `/install/MiSans.ttf` (7.9 MB) first instead of relying only on the WQY fallback; `boot/zhaoxu/fonts/unicode.pf2` is generated from Inter (OFL) with grub-mkfont
- New M0 transport layer: AF_UNIX stream sockets, a per-process descriptor table, memfd, mmap, poll, and SCM_RIGHTS (memfd), with `/data/NET_TEST.ZXE` passing again

## 0.7.0 「Yuanshuo」

### File systems
- New read-only NTFS: boot sector, MFT records with update-sequence fixup, resident/non-resident attributes, runlists (including sparse), $FILE_NAME, and $INDEX_ROOT/$INDEX_ALLOCATION B+ tree directories
- New read-only exFAT: both FAT and NoFatChain allocation, directory entry sets, ValidDataLength zero fill, UTF-16 names
- New ISO 9660/CDFS: primary volume and Joliet descriptors, Rock Ridge NM, version suffix stripping, multi-extent files; CDFS probes four CD sector layouts at 2048/2336/2352
- New read-only UDF: VRS, AVDP, VDS (PD/LVD), FSD, ICB file entries, short_ad/long_ad, FID directories, and OSTA-compressed Unicode names
- New read-only UBIFS: UBI PEB probing, EC/VID parsing, LEB→PEB mapping, UBIFS superblock and node CRC, flat node index, zlib decompression of data nodes
- New standalone vfat driver: FAT12/16/32, VFAT long names, fixed root directory and cluster chains
- New ramfs and a writable rootfs: initramfs as the lower layer, ramfs for copy-up, tombstones for deletes
- overlayfs is now writable: upper-layer commit, `.wh.` whiteouts, and de-duplicated merging
- New ecryptfs stacked encryption: SHA-256 file key derivation, AES-256-CBC/PKCS#7, per-file salt and IV
- New kernel implementations of AES/SHA-256/CRC32/zlib-inflate
- On boot, MBR/GPT/EBR are scanned and NTFS/exFAT/VFAT/ISO/CDFS/UDF/UBIFS volumes are mounted under `/media/<n>`

### Storage and device drivers
- New Ring3 NVMe driver: controller reset/enable, admin and I/O queues, identify, PRP1 transfers, flush
- New SDHCI SD/MMC driver: card initialization, CSD capacity parsing (byte/block addressing), single-block PIO reads and writes
- The IDE PIIX4 driver joins the `/bin/drivers` build and is enumerated from the device service
- The device header expands to 16 disks, with disk order AHCI/NVMe/SD/IDE/USB
- New DesignWare I2C, Intel PCH SPI, and LM75/TMP102 temperature sensors
- New Intel 8254x/82574L NIC driver: RX/TX rings, EEPROM MAC, link status
- New Intel HDA: CORB/RIRB, codec enumeration, DAC→pin output path, 48 kHz/16-bit playback
- New OHCI/EHCI USB host controller initialization and root port reset
- New virtio-gpu 3D: virgl context, 3D resources, and SUBMIT_3D

### Kernel platform
- New MMU/page-table driver `kernel/mem/mmu.c`: four-level walk, huge-page splitting, map/unmap/protect/translate
- New HPET driver and ACPI XSDT/RSDT parsing
- New APIC/PIC interrupt controller frontend `intc`; ARM GICv3 and SysTick live in `kernel/arch/arm64`
- `/sys/class/thermal/thermal_zone0/temp` exposes the sensor temperature

### Desktop
- Following rule 1, composition was split out of Desktop Window Manager into the new **Desktop Window Compositor** (`apps/ADE/compositor/`), which owns the back buffer, damage submission, and cursor state

### Fixes
- Boundary calculations for 2048/2352-byte block volume logical sectors now use the device byte count
- Removed unused function and symbol warnings from the new drivers

## 0.6.0 「Yuanshuo」

### Build and versions
- The build supports native Linux, native macOS, native Windows (MSYS2/MinGW), and WSL2; the Ruby entry points `tools/host.rb` / `build.rb` / `run.rb` detect the host, toolchain, OVMF, and QEMU in one place
- Cross-platform Makefile: `ZOS_HOST` detection, clang `--target=x86_64-elf` + lld + llvm-objcopy on macOS/Windows, portable dd/tar/stat, host-specific OVMF and image tools, and a `--check` tool matrix
- `version.cfg` becomes the single version source, with `os_version` and `kernel_version` separated; `version.h` gains `ZHAOXU_KERNEL_VERSION`, and the ISO metadata records both the OS and kernel versions
- New build host detection unit test `tests/tools/host_test.rb` and `make host-test`

### Storage and file systems
- The ext4 core is split out of the kernel into the OS-independent `src/fs/ext4`, with host tests and a Ruby format matrix
- New Ring3 `/bin/ext4fs` service: block RPC, read/write/allocation/format, timeouts, and request hardening
- `/mnt` is forwarded by the VFS to the Ring3 ext4 service; the installer builds the ext4 root on the spot, and the kernel can boot from an installed ext4 root
- Install target disks support MBR and GPT, keeping the FAT32 ESP and the `ZHAOXUDATA` data volume

### Boot and install
- UEFI is split into two stages: `ZhaoxuBootMgr` (BOOTX64.EFI) → `OSLDRX64.ELF` → `kernel.elf`
- Legacy BIOS moves to a no-emulation El Torito ISO, with the stage2 parameter block backfilled by `tools/iso-bios.rb`
- ISO media is generated per the docs with casper/dists/pool/.disk/preseed and md5sum metadata
- Self-contained install from a single ISO (rule 27), with no extra media required

### Graphics and desktop
- ZAF Z0: a backend-neutral `zaf_gfx` raster interface, a software backend, and a Ring3 fps demo; the ZAF architecture and Z1 design docs are complete
- Aqua Desktop Environment composites into a back buffer and presents only damaged regions; menu bar, About panel, and real SVG icons
- The default wallpaper (rule 33) is converted into raw frames at build time and loaded from the data volume or the ISO at boot, falling back to the Aqua gradient when there is no disk
- Ring3 Aqua Desktop Environment display server; the calculator moves out into a standalone client; the screenshot tool can click through HMP

### Runtime and executable format
- ZXE v1 native format: loader, packer, W^X segment permissions, ASLR image base, and `/lib` dynamic dependencies
- stb_truetype shares one adapter between the kernel and Ring3
- SMP AP startup synchronization and LAPIC delay fix; all hardware drivers move down to Ring3

### Fixes
- ext4 validation of corrupt metadata, stale or forged RPC, and unaligned directory entries
- Install failure cleanup, GPT LastUsable LBA, and BIOS El Torito directory validation
- The installer progress bar shows 0% before it starts

## 0.5.0 「Yuanshuo」

### Desktop and install
- Aqua Desktop Environment graphical desktop (Aqua style): gradient menu bar, RTC clock, Dock, software cursor
- Desktop Window Manager window manager: drag, close, stacking, focus, and keyboard routing
- Native desktop applications: terminal (runs 8sh commands directly), files, calculator, notes, monitor
- Graphical installer: detects the `ZHAOXUEFI` install source and a blank target disk, writes MBR + FAT32 ESP over AHCI, and shows progress and results
- Shell `install [disk]` and `disks`: role detection for install media, data disk, and target disk
- PS/2 mouse IRQ12 three-byte packet parsing, so windows and installer buttons are clickable

### In-system development
- `edit`: in-system C editor supporting sample programs, `.list`, `.run`, `.clear`, `.end`, `.quit`
- `cc`: compiles the editor buffer and runs it in ring 3 right away
- `zcc`: C subset compiler that generates `.zxe` v1 machine-code images directly (int variables, print/printnum, if/else, while, comparisons and the four arithmetic operations, return)
- The shell prompt switches to `edit>` in edit mode

### Drivers and platform
- The ACPI driver moves to Ada: RSDP validation, RSDT/XSDT traversal, FADT parsing, DSDT `_S5` AML parsing, and the shutdown/reboot fallback chain; the C side keeps only port I/O, logging, and CPU control glue
- PCI/PCIe enumeration, AHCI, IDE (PIO), SATA (link status), and SCSI (ATAPI) drivers implemented in Ada
- New shell commands `ata`, `sata`, `scsi`, `ps`

### Fixes
- The notes window draws text by UTF-8 code point, so Chinese is no longer split into single bytes and garbled; backspace deletes a whole character
- Window layout and font sizes adapt at high resolution (verified on real hardware at 1920×1080)

## 0.4.0

- TrueType Chinese rendering: TTC/sfnt parsing, cmap format 4/12, 26.6 fixed-point scanline rasterization, 256-slot glyph cache
- Console UTF-8 decoding, 8×8 bitmap glyphs for Western text and vector glyphs for Chinese; WenQuanYi Micro Hei is distributed on the data disk
- Custom `.zxe` executable format: 32-byte header + 40-byte segment table + raw data, page-aligned, with R/W/X segment flags
- Pure-Ruby converter `tools/zxe-pack.rb`: ELF64 → `.zxe`
- The loader dispatches `.zxe` and ELF64 by magic number, and the ELF compatibility path is kept
- New shell command `fonttest <hex>`: ASCII density plot of the rasterized result for any code point

## 0.3.0

- ring 3 user processes: ELF64 loading, user page mapping, per-process kernel stack and TSS `rsp0`
- `int 0x80` syscall ABI: exit/write/getpid/open/read/close/uptime
- ustar initramfs embedded in the kernel as a read-only ramfs root file system
- PCI → AHCI (SATA) block device driver, IDENTIFY / READ DMA EXT
- Read-only FAT32 driver mounted at `/mnt`, with LFN, subdirectories, and cluster chains; the disk is selected by the `ZHAOXUDATA` volume label
- VFS unifies `/` (ramfs) and `/mnt` (FAT32), with read-only file descriptors
- `/bin/init` runs automatically in ring 3 at boot; new shell commands `ls`, `cat`, `run`, `mounts`
- User-mode exceptions kill only the process and return to the shell; kernel exceptions panic

## 0.2.0

- First bootable release: UEFI boot into a higher-half C kernel
- `boot_info` contract: framebuffer, memory map, RSDP
- GDT/TSS (separate IST stacks for double fault and NMI), IDT, 8259 PIC
- PIT tick, PS/2 keyboard IRQ, COM1 serial input and output
- Physical page bitmap allocator, 4 KiB virtual mappings, kernel heap
- `kprintf` / `ksnprintf`, string library, dual graphics + serial console
- 8sh basic commands, exception state dumps, CPUID/RTC/PCI information

## 0.1.0

- Repository initialization and toolchain conventions (WSL2, language split, Ruby scripts)
- Boot layer and kernel skeleton, linker scripts, build system
- Console, string, and print infrastructure
