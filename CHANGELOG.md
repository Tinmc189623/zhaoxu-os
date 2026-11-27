# Changelog

Full version history of Zhaoxu OS. See [README.md](README.md) for what the current release does; `version.cfg` is the single source of version numbers.

## 0.8.0 「Cedar」

### 0.8.0 交付范围（与镜像一致）

0.8.0 的默认 ISO 是一张**文本安装盘**：Limine 只有 `Install` 一项，启动后显示无名称动画，随后在 80×25 帧缓冲控制台上运行安装器，把 ext4 根、FAT32 ESP 和可选数据分区写盘，随后停在完成页（ENTER 重启，ESC 留在文本控制台）。Aqua 在 initramfs 里，但只由**安装后的 ESP**（菜单项 `Start`）启动，ISO 本身不进入桌面。

以下名称在本版**不是**镜像的组成部分，不能据源码树推断为已可用：

- **FreeBSD personality / OS_5P 运行时**：只接了 M2 的缝；`FREEBSD_PERSONALITY` 默认关闭，打开也只是弱符号与 `personality_start()` 记录 boot info，到不了 `mi_startup`；VFS/TCP/IP/cdev/pf/NFS 运行时不是 FreeBSD 实现。
- **IO/287**：树里没有目录与符号，只有名称和 `version.cfg` 里的一行版本号。
- **btrfs**：规则 45 的默认 btrfs 根未满足，安装器格式化的是 ext4；树里没有 btrfs 实现，也没有 btrfs-progs 程序（Fedora 45 源码已拉取登记，但未构建接入）。
- **glibc 2.44**：只有端口骨架与工具链脚本，glibc 源码不在树里，静态构建未完成，镜像里没有 glibc。
- **交叉工具链**：`tools/binutils.py`、`tools/gcc.py` 是可用的树外构建脚本，但默认 `make` 不使用它们，内核仍由宿主的 freestanding clang/`ld.lld` 链接。
- **网络与音频**：`/bin/drivers.udrv` 含 e1000 与 HDA 初始化，但内核套接字只接受 `AF_UNIX`，没有协议栈收发；`hda_play()` 没有调用方；输入仍是 PS/2。

与上表相对，本版镜像还包含 `/bin/csh`（tcsh 6.24.16，`tools/csh.py` 构建，默认 shell）；bash 5.3 保留为 `/bin/bash`。csh 已构建入镜像，但树里还没有它的启动验证日志。

### ISO 引导不再黑屏
- 安装盘以前跳过开机画面，文本安装器又只写内核日志环、没有屏幕出口，所以从 Limine 退出到安装界面之间整段是黑屏。现在 `splash_begin()` 在安装模式也运行（画面本身不含任何名称），启动阶段照常在屏上推进。
- 新增 `kernel/lib/fbcon.c`：80x25 的帧缓冲文本控制台，16x32 字符格居中显示在 Limine 帧缓冲上，VGA 16 色、滚动、退格和制表符；`console` 增加颜色出口，安装器的 `sys_console_color` 能真正落到屏幕。安装模式的安装器改为在这块控制台上运行，蓝色背景、浅灰文字。
- 内核诊断（`kprintf`）在 fbcon 接管后不再写屏幕，只进日志环和串口（`console_write_kernel` + `console_set_kernel_mute`），生成进程时的 `[proc] pid ...` 之类不再覆盖安装界面；安装器自己的 fd 1 输出仍照常显示。
- 按规则 57，安装器文案与 Limine 菜单去掉了系统名：菜单只剩 `Install`（`timeout: 1`，删除 `interface_branding`/`interface_help`），安装界面写 `Setup`、`your computer`、`the operating system`，装好的 ESP 菜单项改名为 `Start`。
- 验证：`SPLASH_BOOT_TEST=1` 的 ISO 截图 `out/iso-splash.png` 显示开机动画；正常 ISO 截图 `out/iso-setup.png` 显示无系统名的蓝色安装界面且无内核日志覆盖；`iso-check` PASS（23 文件、9 目录）。Limine 之前的 OVMF 固件 logo 由固件绘制，不属于内核可控范围。

### 开机动画
- `kernel/lib/fbdraw.c` 是内核自己的帧缓冲绘制层：像素、矩形、双色渐变和 8x8 字体文本，只操作 Limine 传来的映射，不碰硬件。`kernel/lib/splash.c` 用它画开机画面：深色渐变背景、半径按屏幕短边 1/9 的圆环徽标（12 个点、其中 4 个高亮并按帧绕圈）、当前启动阶段文字和带移动高光的进度条。
- 按规则 57，开机画面不出现操作系统名、内核名、开发团队、品牌作者或品牌名；桌面菜单栏去掉系统名与字母图标，只留无文字的强调色圆角块；自动打开的欢迎窗口只讲操作，不显示系统名或版本；`/etc/motd` 也不再显示系统名与版本（`tools/version.py` 不再改写 motd）。
- 进度由启动阶段驱动：内存与 CPU、中断与 SMP、VFS 核心、驱动服务、ext4 服务、引导卷、显示服务，最后在 Aqua 第一次呈现前 `splash_finish()`，最后一帧留在屏上直到合成器接管。安装盘（`cmdline: install`）也显示动画，随后由帧缓冲文本控制台接管。
- 两处体验修正：圆环和背景在启动时截图保存到内存，之后每帧只把这块 260×260 的图拷回再画 12 个圆点，不再整块重绘和逐像素重画圆环（之前在慢速帧缓冲上会闪）；进度条在阻塞式启动调用前就切到对应阶段，先快速追上目标，再按「阶段宣布后经过的定时器秒数」每秒推进 1%（上限 99%），与动画帧率无关，驱动服务、ext4 服务这类长等待不再停在 45% 不动。帧间隔从 50 ms 放宽到 80 ms，进度条高光也放慢。`SPLASH_BOOT_TEST=1` 现在专门模拟 8 秒长等待，冻结帧实测从 52% 推进到 59%。
- 动画帧由定时器中断推进（每 50 ms 一帧，只在 BSP 上绘制），绘制期间用 `g_drawing` 挡住中断里的重绘，避免两条路径交错写同一扫描线。坐标计算使用有符号整数，负的正弦值不会因为无符号乘法变成巨大坐标。
- `SPLASH_BOOT_TEST=1` 把进度条完整跑一遍、冻结最后一帧并停机，供 `tools/screenshot.py` 截图验证；实测 1920x1080 冻结帧为 100% 进度与完整圆环。

### 交叉工具链：binutils 2.47 与 GCC 16.2.1 阶段 1
- `tools/binutils.py` 从 Fedora 45 的原版 `binutils-2.47.tar.xz` 构建 `x86_64-zhaoxu-{as,ld,ar,ranlib,objcopy,objdump,strip,nm}` 2.47.20260726；源树只加四处目标表补丁（`config.sub` 的 zhaoxu OS 名、`bfd/config.bfd`、`ld/configure.tgt`、`gas/configure.tgt`）。
- `tools/gcc.py` 构建 GCC 16.2.1 阶段 1（C + libgcc，`--without-headers --with-newlib`）：GCC 自己的 `config.sub`、`gcc/config.gcc`、`libgcc/config.host` 加 zhaoxu 目标（复用 x86_64 ELF 的自由运行支持）；GMP/MPFR/MPC/ISL/gettext 从清华镜像取并缓存，仍由 GCC 的 `contrib/prerequisites.sha512` 校验；宿主机没有 `bzip2` 时用 Python 的 bz2 替身；交叉 binutils 目录加入 PATH 供 libgcc 调用 `x86_64-zhaoxu-ar`。
- 验证：`x86_64-zhaoxu-gcc (GCC) 16.2.1 20260819`，`-print-libgcc-file-name` 指向安装目录下的 `libgcc.a`。

### glibc ABI: anonymous memory, TLS, futex, signals and auxv

以下是内核侧的系统调用实现；glibc 本体不在树里，镜像也不带它，默认构建不运行这些回归程序。

- `SYS_MMAP` (54) now serves two backends: memfd mappings unchanged, and `fd == -1` with `MAP_ANONYMOUS` (0x20) routed to the per-process allocator in `kernel/proc/anon.c`. It keeps up to 128 regions in a 48 GiB window at `1 TiB + 16 GiB`, does first-fit or `MAP_FIXED`, refuses more than 512 MiB per call, rolls a failed grow back page by page, and implements `munmap`, `mprotect` (via the new `vmm_space_protect_page`, which rewrites W/NX and keeps the frame and USER bit) and `madvise(DONTNEED/FREE)` by zeroing in place. fork copies the region table; exec and release reset it.
- `SYS_ARCH_PRCTL` (88): `ARCH_SET_FS`/`ARCH_GET_FS` move a per-thread TLS base; the value is written to MSR `0xC0000100` immediately for the calling thread and on every context switch, so a preempted thread resumes with its own base. fork inherits the parent's base, exec clears it; GS reports `ENOSYS`.
- `SYS_FUTEX` (92): WAIT/WAKE and the BITSET variants, keyed by the word's physical address so a shared page mapped at two virtual addresses is one futex. The compare-and-block happens under the scheduler lock, which is what rules out a lost wakeup; a woken thread is flagged separately from a timed-out one.
- Signals (93–97): 32-byte `rt_sigaction` records, process-wide blocked/pending masks, `rt_sigprocmask`, `kill`, `tgkill`, and `rt_sigreturn`. Delivery builds a Linux x86_64 `rt_sigframe` below the interrupted stack — pretcode, a 968-byte `ucontext_t` (mcontext at 40, sigmask at 296), 128-byte siginfo, FXSAVE at 1104 — and requires `SA_RESTORER`. Pending signals are delivered on the way out of every syscall and on timer return; `#PF/#GP/#UD/#BP/#MF/#XM` map to SIGSEGV/SIGILL/SIGTRAP/SIGFPE, and an unhandled fault still reaches the existing reporter. The restorer reaches the gate with `ret`, so `rt_sigreturn` reads the frame at `rsp - 8`.
- Identity, clocks and entropy: `set_tid_address` (zeroed at exit), `gettid`, `exit_group`, `set_robust_list`, `prlimit64`/`getrlimit` (static limits: 8 MiB stack, 1024 files), `getrandom` (RDRAND with a TSC-seeded fallback), `clock_gettime` (RTC in UTC for REALTIME, `timer_now_ns()` for MONOTONIC/BOOTTIME, EINVAL for CPU clocks), `uname` from `version.h`, `access` by VFS existence, `readlink` as ENOSYS, and `last_error` (112) which returns the EIO recorded when a legacy 0–85 call returned -1.
- `kernel/proc/elf_loader.c` now owns ZXE/ELF loading and the initial stack: ELF ET_EXEC segments map at their fixed addresses with W^X, `brk` starts on the page after the highest segment (256 MiB headroom inside the image window), and the stack carries argv, a default envp and an auxv with PHDR/PHENT/PHNUM/PAGESZ/BASE/ENTRY/UID/EUID/GID/EGID/RANDOM/EXECFN. AT_PHDR is 0 when the linker leaves the program headers outside every LOAD segment.
- ABI regression programs run under `ABI_BOOT_TEST=1` on the installed `out/target.img`: `user/anon_test.c`, `user/tls_test.c`, `user/futex_test.c` (fork + shared memory), `user/signal_test.c` (handler/restorer, block/unblock, SIG_IGN, SIGKILL status 137, SIGSEGV handler that steps over the faulting instruction), `user/info_test.c` and `user/elf_test.c` (static ET_EXEC at 1 TiB via `user/user-exec.ld`). All six report exit 0 in the boot log.
- The file-descriptor calls (openat/fstat/pread/pwrite/fsync/dup) land with the block-device path in the btrfs phase, where the VFS gains flags, positional I/O and sync.

## 0.7.1 「Yuanshuo」 build 21

### FreeBSD owns VFS, TCP/IP, cdev, pf and NFS
- Owner directive (2026-11-25): these five subsystems must be the FreeBSD 15.1 implementations, never a Zhaoxu replacement. The design note is in `docs/superpowers/specs/2026-11-20-xnu-freebsd-design.md` §4 and `CLAUDE.md`.
- The `ZHAOXU` kernel configuration now includes MINIMAL plus `FFS`, `TMPFS`, `PSEUDOFS`, `PROCFS`, `GEOM_PART_GPT`/`GEOM_PART_MBR`, `device pf`, `device pflog`, `device crypto` (pf's TCP ISN uses SHA-512, which only compiles with the crypto device), `NFSCL`, `NFSD` and `NFSLOCKD`; INET/INET6, UFS and the cdevsw layer come from MINIMAL and the shared kern sources.
- The expanded `ZHAOXU` kernel builds and links inside the FreeBSD VM: `/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU/kernel` is 15,834,552 bytes (2026-11-26 12:53 UTC), with buildkernel run `NO_MODULES=1` because OS_5P is a single static image. The object set contains all ten checked subsystem objects — `vfs_subr.o`, `kern_conf.o` (the cdevsw layer), `tcp_input.o`, `udp_usrreq.o`, `pf.o`, `if_pflog.o`, `tmpfs_vfsops.o`, `ffs_vfsops.o`, `devfs_vfsops.o`, `nfs_commonkrpc.o` — and the combined link of the FreeBSD object set with the 80 core objects reports `19 collision(s)`, `renamed 80 yuanshuo object(s)`, `LINK=0`, `/tmp/os5p.elf` 73,189,160 bytes. Two real defects surfaced and were fixed on the way: pf needs `device crypto` (its TCP ISN calls SHA-512 from `sys/crypto/sha2`, whose files are conditional on the crypto device), and the first build failed with `Missing KERNCONF` because the config upload was silently unchecked.
- `tools/freebsd-vm.py` hardening found while building the larger kernel: the overlay configuration is served over HTTP and the upload is **verified** (`CONFIG_RC=0` plus a byte count, one retry; a pasted heredoc lost characters, and an unchecked failed fetch left the build reporting `Missing KERNCONF`), the HTTP server readiness is probed before the first fetch, source `fetch`/`tar` return codes must be 0 (a truncated `/usr/src` later failed as a missing mtree file), serial writes are chunked (a long command burst shifted a character and gave make a stray argument), the build runs from `tools/freebsd-build.sh` with its transcript in `/tmp/build.log` instead of on the 115200-baud console, `make -j8 NO_MODULES=1` (OS_5P is one static image; the module tree added unrelated `cc_hd`/`cfiscsi` failures and time), the serial connect retries while QEMU binds the port, QEMU starts in its own session so `--start` can leave a headless VM running for `--build-resume`/`--link`, every build re-sends the current overlay (resumes included), failures print `config(8)`/compiler diagnostics and the last 40 lines, and `--link` first certifies that the `vfs_subr`, `subr_cdev`, `tcp_input`, `udp_usrreq`, `pf`, `pflog`, `tmpfs_vfsops`, `ffs_vfsops`, `devfs_vfsops` and `nfs_commonkrpc` objects are present.

### FreeBSD VFS provider (handover slice 1)
- `kernel/personality/freebsd/vfs.c` implements the core's `zx_vfs_provider` by calling FreeBSD's own kernel helpers — `kern_statat`, `kern_openat`, `kern_pread`, `kern_pwrite`, `kern_mkdirat`, `kern_frmdirat`, `kern_renameat`, `kern_funlinkat` and `kern_getdirentries` — so path operations are executed by the vnode layer, not reimplemented. `kernel/personality/freebsd/vfs.h` exposes it.
- The handover is timed correctly: every SYSINIT runs before the init kthread calls `vfs_mountroot()`, and 15.1 has no `SI_SUB_MOUNTROOT`, so the provider registers from the `vfs_mounted` event when `MNT_ROOTFS` is set, exactly once. `port.c` hands the validated service table to `freebsd_vfs_set_services()`.
- `tools/freebsd-vfs-check.sh` fetches `vfs.c`, `vfs.h`, `port.c` and `core_abi.h` into a booted VM and compiles them against the real 15.1 headers and `ZHAOXU` config; the run reports `VFS_CC=0` for both sources. The runtime handover still waits for the machine port to reach `mountroot`.

### SYS_BRK (85): the first glibc prerequisite
- The program break is real: `struct process` carries `brk_start/brk_current/brk_limit`, the ZXE loader places the heap after the highest ASLR-placed module (256 MiB ceiling, bounded by the 1 GiB image window), `process_brk()` grows and shrinks it page by page, rolls back a failed grow, and frees pages on shrink; pages are mapped `WRITABLE|EXECUTE_DISABLE`; fork inherits the fields and the copied pages.
- `user/userlib.h` gains `SYS_BRK 85` and `sys_brk()`; `user/brk_test.c` covers query, grow, write/read, shrink, fresh zero pages after regrow and the limit rejection; `BRK_BOOT_TEST=1` runs it from `/data/BRK_TEST.ZXE` with an install-mode gate so the same ISO still installs.
- Verified: kernel links with zero undefined symbols and the boot test reaches `[brk ] BRK_TEST exit 0`. The shipped ISO is rebuilt without the test flag afterwards.

### Rule-compliance fixes: ISO/ESP/rootfs directories
- ISO: the `amd64/` directory from the RULES.md ISO layout was missing; the ISO recipe now creates `amd64`, `preseed` and `.disk` explicitly, and `tools/iso-check.py` requires `amd64`. The rebuilt image reports `PASS (23 files, 9 dirs, no extras)` and still boots to `Zhaoxu OS Setup`.
- KeXT name: every `vga15h.kext` reference (ISO staging, `boot/limine.conf`, `tools/iso-check.py`, installer text, README/CLAUDE/AGENTS/CHANGELOG/DRIVER_MODEL/INSTALL/rules exam) is now `vga10h.kext`, matching rule 31 and the media layout.
- ESP: the installer wrote `KERNEL.ELF`, `MiSans.ttf` and `WALL.RGB` at the ESP root, which contradicts the ESP layout in RULES.md. `kernel/fs/install.c` now writes them under `/boot/limine` and points `limine.conf` at `boot(n):/boot/limine/KERNEL.ELF` and the matching module paths; the live docs were updated to the same layout.
- Rootfs: the staged FHS tree lacked `home/user` from the disk layout; `FHS_DIRS` now creates it. The full FHS 3.0+ tree (`bin boot dev etc home/user lib lib64 media mnt opt proc root run sbin srv sys tmp usr/{bin,sbin,lib,lib64,share,include,local} var/{log,cache,spool,tmp}`) is present in every image.
- Still open, recorded rather than papered over: rule 1's `csh` default shell is not ported yet; rule 45's btrfs default root is in progress (Fedora 45 sources verified); the installed ext4 root has no FHS skeleton because the ext4 service exposes no mkdir RPC yet (stage 2 work); the embedded data mirror keeps the non-FHS `/data` path.

### Fedora 45 sources for btrfs and the toolchain
- Fedora 45 is published on the kernel.org mirror family (the Fedora archives were stale): release 45's source tree carries `btrfs-progs-7.1-1.fc45.src.rpm`, `glibc-2.44-1.fc45.src.rpm` and the rest of the toolchain.
- `tools/fedora-fetch.py` reads the release's real `repodata/primary.xml.zst`, verifies the repodata checksum, then each SRPM's published SHA-256; interrupted downloads stay as `.part` files. Kernel.org throttled to near zero while `mirrors.edge.kernel.org` and Tsinghua served 0.7-2 MB/s, so the tool picks the first mirror that answers (`FEDORA_MIRROR` overrides).
- Verified and recorded in `third_party/fedora/45/SOURCES.md`: `btrfs-progs 7.1-1.fc45`, `glibc 2.44-1.fc45`, `binutils 2.47-1.fc45`, `gcc 16.2.1-2.fc45.1`, `gdb 17.2-6.fc45`, `llvm 23.1.2-2.fc45`, `lua 5.5.1-2.fc45` and `python3.14 3.14.7-1.fc45`, each with its published SHA-256. The SRPM containers are build inputs and stay under the git-ignored `build/fedora/45/SRPMS/` (about 410 MB); only the ledger is committed.
- `tools/srpm-extract.py` unpacks SRPMs without rpm2cpio/cpio: it parses the RPM signature and main headers, inflates the zstd/xz/gzip payload with the host tool that matches the magic, and expands the cpio newc archive with path confinement. The btrfs-progs and glibc source trees are extracted under `build/fedora/45/`.

### Red panic screen draws from ascii.md
- `kernel/lib/panic.c` now paints the display itself: the Limine framebuffer is filled red and the `KERNEL` / `PANIC` banner published in `ascii.md` is rendered with the public-domain 8x8 font, followed by the real exception fields (`EXCEPTION`, `ERROR CODE`, `RIP`, `CR2`). The earlier screen wrote through the kernel console, which has no framebuffer sink before the Ring3 display service runs, so nothing was ever visible.
- The panic path also writes a one-line report straight to COM1 (`[panic] vector=… name=… error=… rip=… cr2=…`); the log ring is not drained at that point.
- `PANIC_BOOT_TEST=1` exercises the screen at boot; the verified capture shows the red screen with `EXCEPTION : 14 Page Fault`, `RIP 0xffffffff80029c50`, `CR2 0x10000000000`.
- `tools/screenshot.py` globbed the retired `zhaoxu-os_x86-64_*_ld.iso` name and silently booted the old 0.7.1 installer ISO; it now matches `zhaoxu-os_x64_*_ld.iso`.

### OS_5P runs FreeBSD personality code (runtime handshake)
- The combined image now boots through Limine and executes the FreeBSD personality: the core hands over the v4 service table, proves the pmap window, and `port.c` (compiled inside the FreeBSD 15.1 tree) runs and writes through the core console. Serial log from the test ISO:
  `[bsd ] core services v4`, `[bsd ] pmap window 0xffffffff91000000, 256 pages, identity 64 GiB`, `[bsd ] pmap window PASS`, `[bsd ] handing off to the FreeBSD personality`, `OS_5P: FreeBSD personality executing`, `OS_5P: console table bound`, then the core continues into the installer.
- Boot test found a real linker-script defect: lld placed `.rela.dyn` at the exact end of `.text`, and as a read-only orphan it dragged the R segment back into the last text page (`0xffffffff80556bc0`), so Limine refused with "PHDRs with different permissions sharing the same memory page". `kernel/linker.ld` now pins `.rela.dyn`/`.rela.*` to a fresh page; the segments are `E` at `0xffffffff80000000`, `R` at `0xffffffff80557000`, `RW` at `0xffffffff807e0000`, all page-aligned, and the image boots.
- To build and run it: `make -j4 FREEBSD_PERSONALITY=1 out/kernel.elf`, `tar -cf build/core-obj.tar -C build kernel initramfs.o`, serve `src/` on 8004, run `tools/freebsd-core-sync.sh` in the VM (refreshes core objects, regenerates the object list, rebuilds `port.o`/`vfs.o`), `tools/freebsd-vm.py --link`, pull `/tmp/os5p.elf` (nc), then `tools/os5p-boot-test.sh`, which stages a copy of the ISO tree, swaps in the combined image, boots UEFI headless and asserts the handshake markers (`OS5P_BOOT=PASS`). `tools/freebsd-core-sync.sh` also fixed a stale-list failure (`klog_clear` undefined because the old object list predated `klog.o`).

### toybox 0.8.12 links with its full toy set
- `src/tools/toybox-build.sh` builds the untrimmed default configuration against newlib and the FreeBSD regex sources, links with lld as a PIE (`src/user/user.ld`), and satisfies the `FBSD_1.0` symbol versions with a small version script. The result has **zero undefined dynamic symbols** and packs to `out/toybox.zxe` (3 segments, 2,209 relocations).
- The POSIX surface toybox needs is now split by concern under `src/user/lib/posix/`: network (`posix_net.c`, AF_UNIX real, numeric name resolution real, DNS/AF_INET refused), kqueue over epoll (`posix_kqueue.c`), poll, clock/RTC, mapping and sizing, path-and-metadata, process and limits, accounts and groups, mount table, xattrs, syslog, utmp, inotify, iconv (UTF-8/ASCII/Latin-1), termios helpers, fnmatch, getline/getdelim, exec wrappers and scatter/gather I/O. Calls without a kernel counterpart return `ENOSYS`/`ENOTSUP`; the file that implements each one states why.
- `src/Makefile` gains `TOYBOX=1` (stage `/bin/toybox`), `TOYBOX_BOOT_TEST=1` (at boot run `mkpasswd`, `dmesg`, `mkdir`/`rmdir` and `mv` through toybox, then halt) and `ISO_CMDLINE=` (stage a test ISO without the `install` cmdline). The build detail is `docs/TOYBOX.md`.
- `crypt()` now comes from FreeBSD 15.1's libcrypt, vendored unmodified as `src/third_party/freebsd-libcrypt/` (libcrypt + libmd MD4/MD5 + `sys/crypto/sha2`), with a small port header layer. `libfbcrypt.a` is linked into toybox. Known-answer tests match `openssl passwd` for `$6$`, `$5$` and `$1$`; the QEMU boot test prints the exact SHA-512 hash on the serial line. DES and Blowfish stay out (they live in FreeBSD's `secure/lib`, not vendored).
- The kernel log ring is exported: `kernel/lib/klog.c` keeps the last 64 KiB of console output, `SYS_KLOG` (81) reads it with the Linux `klogctl` types, and toybox's `dmesg` uses it.
- `SYS_MKDIR` (82), `SYS_RMDIR` (83) and `SYS_RENAME` (84) make directory and rename operations real: `vfs_mkdir`/`vfs_rmdir`/`vfs_rename` route to the rootfs copy-up layer (upper ramfs over the read-only initramfs), with empty-directory and tombstone handling and file copy-up before a rename of a lower-only entry; the BSD provider ABI gains matching entries and moves to version 4. The boot test runs `toybox mkdir /data/NEWDIR`, confirms the directory through `vfs_stat`, removes it with `toybox rmdir`, then renames `/data/HELLO.TXT` with `toybox mv` and confirms the old path is gone.

### FreeBSD 15.1 personality replaces the Linux plan
- The XNU-style auxiliary personality is now **FreeBSD 15.1-RELEASE** instead of Linux 7.4-rc3. The design is `docs/superpowers/specs/2026-11-20-xnu-freebsd-design.md`; the 2026-11-18 Linux design document is removed (it stayed at M0 and never had an implementation).
- FreeBSD is BSD-2-Clause, so the GPL-2.0 relicense is no longer legally required for the combined image; the project stays GPL-2.0 by owner decision. CDDL-only parts of the tree (`sys/contrib/openzfs`, `sys/cddl`) are excluded from the linked image.
- FreeBSD builds cross-platform through its own `tools/build/make.py` (bmake bootstrap plus an external clang/lld), so WSL2 remains the Windows build host; a FreeBSD VM is the documented fallback.
- The hybrid kernel is named **OS_5P**, and its device framework is **IO/287**, filling the IOKit role in the XNU model. IO/287 is an in-kernel freestanding C++ framework — matching, registry, driver families — that owns device attachment; the FreeBSD personality consumes its nodes instead of attaching hardware through newbus. The existing Ring3 `/bin/drivers` set stays and migrates per-device.
- Nothing is vendored or implemented yet. M0 remains a design milestone; M1 (core personality API) is unchanged in shape.

### M1 — core personality API
- `src/kernel/include/personality.h` defines the kernel-internal contract; `src/kernel/personality/personality.c` implements it: task threads backed by core kthreads with join timeouts, a 256-page reserved VM region granted unmapped with map/unmap/reprotect, trap forwarding out of `interrupt_dispatch`, and a periodic timer service on the boot CPU.
- `src/kernel/ipc/kport.c` adds kernel-space IPC endpoint pairs (typed messages up to `IPC_MSG_MAX`, 32-deep per-direction queues, close semantics) for a personality that has no process id.
- `src/kernel/personality/selftest.c` runs with `PERSONALITY_SELFTEST=1` and prints `[per ]` markers. Verified on QEMU: VM map/write/unmap, task thread, kernel IPC, trap forwarding and timer service all PASS.
- The shipped kernel is unchanged: the self-test body and its object follow the flag through a build stamp, so a test kernel cannot leak into an ISO.

### M3 — boot handoff and console (core side)
- `personality_start()` records the core `boot_info` for the personality and `personality_boot_info()` reads it back; `personality_console_write()` routes personality output through the core console (`console_write_n`, VGA plus the log ring), which is the call FreeBSD's `cn(4)` backend will make.
- Verified with `PERSONALITY_SELFTEST=1`: `[per ] boot handoff PASS` and `[per ] console service PASS` appear alongside the M1 checks.
- This is not M3 itself: the personality still has no machine port, is not linked into `KERNEL.ELF`, and never reaches `mi_startup`. That gate is M2.

### M2 — FreeBSD build input
- `src/tools/freebsd-personality.py` fetches the pinned 15.1-RELEASE `src.txz` (official URL or `FREEBSD_SRC_URL` mirror), verifies the SHA-256 published in the release MANIFEST (`cf5762da…89e1`), extracts it under `build/freebsd/`, and applies the `ZHAOXU` kernel configuration from `kernel/personality/overlay/` without touching the vendored tree.
- Verified end to end: 241.3 MB downloaded, checksum matched, tree extracted to `usr/src`, `KERNCONF=ZHAOXU` present after the overlay.
- The cross build on the WSL2 host is still blocked: `kernel-toolchain` stops on missing host packages (`flex`, `libssl-dev`) and on a conflict between FreeBSD's `__unused` macro and glibc's `bits/struct_mutex.h` in the bootstrap tools (`dtc`, `libclangminimal`).
- The native build inside the FreeBSD 15.1 VM works: `make -j16 buildkernel KERNCONF=ZHAOXU` completed in 162 s and produced `/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU/kernel` (13,319,904 bytes, ELF64 FreeBSD executable) plus `kernel.full` (52,856,968 bytes). Two VM prerequisites were fixed: the 6 GiB image was grown to 16 GiB (`qemu-img resize`, `gpart resize -i 4`, `growfs`), and the source tree was re-extracted after an earlier disk-full run left it incomplete.
- Slice 1 landed: `FREEBSD_PERSONALITY=0/1` build gate (stamp keyed like `M0_BOOT_TEST`), `kernel/personality/freebsd/boot.c` hand-off seam with a weak `freebsd_personality_main`, and the FreeBSD BSD-2-Clause entry in `THIRD_PARTY_NOTICES.md`. The gated ISO links with no undefined symbols (`freebsd_personality_main` `W`, `freebsd_personality_start` `T`) and boots to `[bsd ] personality entry not linked`; the default build is unchanged and boots to `Boot complete`.
- Slice 2 (pmap linkage study): `pmap.o` leaves 171 undefined symbols; 154 resolve inside the FreeBSD kernel object set, and the other 17 are linker-generated — 15 `__start_set_*`/`__stop_set_*` (sysinit, sysuninit, sysctl, pcpu, sdt_tracepoint), `_end`, and `brsection`. `kernel/linker.ld` now provides `_end`, `brsection` and the five `set_*` output sections, so the auto set symbols materialize once the personality objects are linked. The default kernel still links with no undefined symbols.
- Slice 3: `kernel/include/personality_freebsd.h` defines the versioned `freebsd_core_services` table (page size, kernel window, identity limit, console, VM window, tasks, traps, timers, IPC) so the FreeBSD side binds through a table instead of clashing with core symbols. `freebsd_pmap_init()` maps, writes and reads back a probe word in the granted VM window before the port runs. Verified with `FREEBSD_PERSONALITY=1 PERSONALITY_SELFTEST=1`: `[bsd ] core services v1`, `[bsd ] pmap window 0xffffffff91000000, 256 pages, identity 64 GiB`, `[bsd ] pmap window PASS`, and every M1/M3 core marker still passes.
- Slice 4: the ABI moved to `kernel/personality/freebsd/core_abi.h` with portable `zx_u8/32/64` types and `struct zx_core_services`, so the same header compiles in both kernels. `probe.c` builds inside the FreeBSD 15.1 tree (`cc -D_KERNEL -I/usr/src/sys -I/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU -I<header dir>`) with `kernel_pmap`, `pmap_t` and `vm_page_t` visible: `PROBE=0`, 1,296-byte object. The core builds with no undefined symbols.
- Slice 5: `kernel/personality/freebsd/pmap_glue.c` is the first port-side source compiled inside the FreeBSD tree. It claims pmap ownership through the service table (version and page-size checks) and relocates cleanly against the real `pmap.o`: `GLUE=0`, `ld.lld -r pmap.o pmap_glue.o` `RELOC=0`, and the merged object still reports 171 undefined symbols — the glue adds no external surface.
- Slice 6: the whole FreeBSD kernel object set plus `pmap_glue.o` relocates into a single object (111,442,320 bytes). After the merge only 64 symbols stay undefined: 27 `__start_set_*` and their stop counterparts, `__build_id_*`, `_DYNAMIC`, `_end`, `_etext`, `brsection`, and four undefined-weak ZSTD trace hooks. `kernel/linker.ld` now provides `_etext` and `_DYNAMIC` alongside `_end`, `brsection` and the `set_*` sections, so every remaining symbol is linker-generated.
- Slice 7: symbol isolation measured and solved. The FreeBSD object set exports 13,287 globals against Yuanshuo's 546, and exactly 19 collide — `memcpy`, `memmove`, `memset`, `shm_map`, `shm_unmap`, `strcat`, `strchr`, `strcmp`, `strcpy`, `strlen`, `strncmp`, `strncpy`, `strstr`, `tty_ioctl` and their relatives. `llvm-objcopy --prefix-symbols=bsd_` succeeds, but it also prefixes references to linker-provided names (`bsd_etext`, `bsd___start_set_*`), which the linker cannot synthesize; the accepted strategy is therefore `--redefine-syms` on those 19 names only.
- Slice 8: first real link attempt. With the 19 renames applied, the whole FreeBSD object set plus the Yuanshuo kernel objects drops to three unresolved names; `__build_id_start`/`__build_id_end` are satisfied by `--defsym`, and the `set_*` symbols resolve themselves once the explicit `.set_*` output sections are removed, because orphan sections named `set_sysinit` and friends make the linker synthesize `__start_set_*`/`__stop_set_*`. `brsection` stays unresolved even with a script assignment, a `--defsym`, and a real BSS definition; the reference needs a FreeBSD-side or linker-side fix before the image can link. Size is not a blocker: the merged object is 6,236,425 bytes of text plus 8,927,522 of data (about 15 MiB) and 7,890,970 of bss, under the 64 MiB flat-image cap.
- Slice 9: the `brsection` diagnosis. `pmap.c` uses `(vm_offset_t)brsection` as the end-of-text address, which FreeBSD's own linker script provides. In the merged object the name is **absent from the symbol table** (`readelf -sW` finds no entry, while `etext` is present as `GLOBAL DEFAULT UND`), yet lld reports it as an undefined reference from `pmap_bootstrap`. A script `PROVIDE`, `--defsym`, an object that defines it, and an objcopy rename to `bsd_brsection` all leave the diagnostic unchanged. `llvm-objcopy --prefix-symbols`/`--redefine-syms` on the 111 MB merged object also manufactures phantom undefined names, so the port must not transform that object. Everything else is settled: 19 duplicate globals, `--allow-multiple-definition` links them, sets resolve as orphans, `etext`/`edata`/`end`/build-id resolve from the script.
- Slice 10: linking the 1,069 original objects directly (modules and dot-dependency files excluded) — no intermediate `ld.lld -r` merge — leaves **exactly one** unresolved symbol, reported from `pmap.o:(pmap_bootstrap)`. `readelf -sW pmap.o` shows `etext` as `GLOBAL DEFAULT UND` but no entry for the reported name, and neither `grep -a` nor `strings` finds it in the object.
- Slice 11: root cause found — the symbol is **`brwsection`**, not `brsection`; `od -c` of `pmap.c:1834` shows `b r w s e c t i o n`, and the terminal rendering had made the `w` look like part of `s`. Every previous script `PROVIDE`, `--defsym` and defining object used the misspelled name. With `PROVIDE(brwsection = .)` the direct link of 1,069 FreeBSD objects plus the 80 Yuanshuo objects completes: `LINK=0`, zero undefined symbols, `/tmp/test_kernel.elf` 73,184,352 bytes including debug info. The 19 duplicate globals are resolved with `--allow-multiple-definition` for now. The image exceeds the 15 MiB kernel window, so booting it needs the split layout described in the design.
- Slice 12: the image is sized and the kernel window grows to hold it. The combined layout is text 5,588,168 B, rodata 802,104 B, data 702,024 B and bss 84,161,920 B, ending at `__kernel_end` `0xffffffff85885580`; the BSS is spread across the whole FreeBSD object set (largest single objects: `limine_boot.o` 2.4 MB, `pmm.o` 2.0 MB, `vt_core.o` 1.5 MB). `KERNEL_STACK_TOP` moves from `0xffffffff83f00000` to `0xffffffff87f00000` in `linker.ld`, `entry.S`, `limine_boot.c` and `thread.h`, and the ASSERT keeps the image 2 MiB clear of the boot-stack mapping. A first attempt with an exactly 2 MiB-aligned top (`0xffffffff88000000`) stopped the ISO at Limine: `limine_boot` mapped the 2 MiB page above the top, the first push faulted, and `-no-reboot` left it there; the mapping now derives the window from the stack top's last byte, so the top's alignment no longer matters. With the shipped script the 1,069 FreeBSD objects plus the 80 Yuanshuo objects link with `LINK=0` and no warnings (73,184,352 bytes), and `__start_set_sysinit_set`/`__stop_set_sysinit_set` bracket the FreeBSD sysinit table (xpt, xen, zlib entries). The default ISO boots to `Boot complete` with the new window.
- Slice 12 tooling: `tools/freebsd-vm.py` serial login woke getty with a newline, types the user name slowly and answers the empty root password, so `root` over ttyu0 no longer drops characters or fails after the first attempt. `--serial-cmd` waited for `CMD_DONE=\d+` while the guest echoed `CMD_DONE_$?`, so every command timed out no matter what it printed; it now emits `CMD_DONE=$?`. One caveat found while testing: `tools/smoke.py` finishes with a global `pkill -f qemu-system-x86_64`, which also takes down an unrelated FreeBSD VM running on the same host.
- Slice 13: the 19 collisions are separated by renaming instead of `--allow-multiple-definition`. `src/tools/freebsd-link.sh` (new) derives the globals both kernels define from one `llvm-nm -A -g` pass, writes a `name core_name` map, mirrors the core objects through `llvm-objcopy --redefine-syms`, and links the whole set with the shipped script. Renaming the core side touches 80 objects and finishes in about two minutes in the guest; the FreeBSD side has 1,069 objects and did not finish inside 40 minutes, so `--rename-side bsd` exists but is not the default. The run reports `19 collision(s)`, `renamed 80 core object(s)`, `LINK=0`, 73,185,056 bytes, with only the four weak ZSTD trace hooks undefined. FreeBSD keeps its own `memcpy`/`memset` (IFUNCs), `strcmp` and `strlen`; the core keeps `core_memcpy`, `core_memset`, `core_tty_ioctl`, `core_shm_map` and the rest. The collision list is derived from the objects, not hardcoded. `tools/freebsd-vm.py` gains `--link`, which serves `tools/freebsd-link.sh` and `kernel/linker.ld` to the guest, runs the tool and reports its markers.
- Slice 14: the collision fix moved out of the linker and into the source. The core's 14 C string functions plus `lapic_eoi`, `kthread_exit`, `shm_map`, `shm_unmap` and `tty_ioctl` are now `yuanshuo_*`. `kernel/include/string.h` maps the plain names to the prefixed ones only for kernel translation units (`-DZHAOXU_KERNEL_BUILD=1`), so Ring3 code that links the same `kernel/lib/string.c` (for example `out/ext4fs.zxe`) keeps `memcmp` and friends. `nm out/kernel.elf` shows `yuanshuo_memcpy`, `yuanshuo_strlen`, `yuanshuo_tty_ioctl`, `yuanshuo_kthread_exit`, `yuanshuo_lapic_eoi` and no plain copies, with no undefined symbols; the ISO still boots to `Boot complete`.
- Slice 14 also links with nothing to rename: `tools/freebsd-link.sh` reports `0 collision(s)` and `LINK=0` (73,186,264 bytes) with no objcopy pass, and the image holds FreeBSD's IFUNC `memcpy` next to `yuanshuo_memcpy`. The first port-side console backend, `kernel/personality/freebsd/port.c`, registers a cn(4) device that forwards `printf` to the core console; it compiles inside the FreeBSD tree (3,568 bytes) and `freebsd_personality_main` is a strong symbol at `0xffffffff805548d0` in the image. Naming convention: `yuanshuo_` for core globals FreeBSD also defines, `io287_` for IO/287 exports, FreeBSD keeps its own names.
- Rule compliance (ISO media, RULES.md v1.14.3): the layout now matches the rule's tree exactly. The Makefile stages only `boot/limine/`, `EFI/BOOT/`, `casper/`, the `dists/jammy` and `pool` directories, `.disk/`, `preseed/`, `README.html`, `autorun.inf` and `md5sum.txt`; `install/` is gone, and `boot/limine/kernel.elf` is present as the rule requires. Limine loads `casper/vga.ko`, `MiSans.ttf` and `WALL.RGB` as modules — no flat kernel image and no `KERNEL.BIN` module anywhere. The installer no longer reads a boot module: it snapshots its own mapped image (`KERNEL_VMA`..`__kernel_end`) into the single-segment `KERNEL.ELF` it writes to the target ESP, which now holds `KERNEL.ELF`, `MiSans.ttf` and `WALL.RGB` only. `boot_info` lost the `boot_image_phys`/`boot_image_size` fields, and `tools/iso-check.py` (new) encodes the v1.14.3 file and directory list, rejects extra files and dangling `limine.conf` references. `README.html` describes the real Limine chain. Two stale Ruby scripts left in `src/out` were removed; `archive/boot-legacy/tools/*.rb` stays as archived history and is not part of any build.
- Install medium boots into setup, like a Windows XP disc: the default (and only) Limine entry `Zhaoxu OS (Install)` passes `cmdline: install`; `limine_boot.c` reads the Limine executable cmdline into `boot_info.install_mode`, and `kernel_main` then starts `/bin/drivers` for keyboard input and `/bin/apps/install.zxe` — no display service, no compositor, no live desktop.
- `/bin/apps/install.zxe` is now a text-mode setup program in the XP i386 tradition instead of an Aqua client. It switches the tty to raw mode and walks welcome (ENTER/Q), licence (Y/N), disk selection (number keys, R rescan), layout (T GPT/MBR, D data partition, ENTER install), a format warning (F), a progress screen fed by `SYS_INSTALL_PROGRESS`, a failure screen and a restart screen (`SYS_REBOOT`). It links only `userlib.c` (13,212-byte ZXE, 0 imports).
- Rules 48–53: the live root now carries the FHS 3.0+ layout including the `/usr` hierarchy (`bin`, `sbin`, `lib`, `lib64`, `share`, `include`, `local`) and `/var/{log,cache,spool,tmp}`, plus the empty top-level directories. Userland drivers are `.udrv` bundles containing a ZXE executable: `tools/udrv-pack.py` builds them, `tools/udrv-check.py` validates them, `process_spawn()` resolves a bundle by reading `path/executable`, and the Ring3 services ship as `/bin/drivers.udrv`, `/bin/ext4fs.udrv` and `/bin/display.udrv`; the trusted block/display capabilities match the bundle path or its `executable`, and stale bare binaries are removed when the initramfs is packed. The ISO media carries the same image as `casper/vga10h.kext`, the name the v1.14.3 media layout uses. `docs/DRIVER_MODEL.md` defines kext as an OS_5P-only in-kernel module format (explicitly not compatible with macOS XNU), NKE as a kext subtype for the BSD network stack, and records that the BSD subsystems are statically linked into the single image; kext and NKE loaders are not implemented yet and are listed as such.
- Rule 39: the third-party trees stay as shipped. FreeBSD 15.1 is fetched from the release tarball, verified by SHA-256, and only its kernel configuration is overlaid as a separate file; rule 39's FreeBSD exception permits direct modification but the tree does not need it. The vendored bash, wayland, libffi, freetype, stb, vulkan-headers and limine trees are compiled from their own sources by `tools/*.py` without in-tree patches.
- Rule 51, VFS ownership: the namespace belongs to the BSD layer, not the core. `core_abi.h` moves to ABI version 2 and carries `struct zx_vfs_provider` plus a `vfs_register` slot in the service table; `kernel/fs/vfs.c` keeps its mount providers only as a transitional fallback, and `vfs_stat`, `vfs_read_at`, `vfs_read_into`, `vfs_read_all`, `vfs_list`, `vfs_write_file` and `vfs_unlink` route to the registered BSD provider. `personality/freebsd/boot.c` fills the slot with `vfs_set_provider()`. The BSD vnode side itself still waits for the machine port, so the core provider serves the live root today — `vfs_provider_name()` reports which side is active.
- Install stages, stage 1: setup now paints the XP-style blue screen through the new
  syscall 79 (`SYS_CONSOLE_COLOR`) and uses the full keyboard — the PS/2 driver maps
  F1–F12 to `0xF1`–`0xFC` and arrows/Home/End/Page keys to `0x81`–`0x88`, so the
  screens bind welcome to ENTER/F3, the licence to F8/ESC, and the disk list to
  Up/Down plus ENTER. `docs/INSTALL_STAGES.md` specifies all three XP-style stages;
  stage 1 is implemented, stages 2 (GUI phase from the installed disk, reading the
  CD) and 3 (OOBE before the first desktop) are specified and marked unbuilt.
- Terminal foundation: pseudo-terminal pairs. `SYS_PTY` (80) creates a pair of pipe rings plus terminal state; `kernel/proc/pty.c` keeps eight pairs and answers `TCGETS`/`TCSETS`/`TIOCGWINSZ` on a redirected stdio through `zfd`, so a shell started with `dup2()` on the slave pair sees a terminal. There is no line discipline yet: echo and line editing belong to the terminal application. The desktop Terminal itself is the next step.
- Rule 55, Arbor: `docs/ARBOR.md` defines the package format (`.arb` tar with `pkg.toml` and `payload/`, installed under `/usr/arbor/<name>/<version>/`), and the host tooling exists — `tools/arb-pack.py` builds deterministic archives, `tools/arb-check.py` validates them and prints the `arbor.toml` sha256. A real package was produced from the Terminal client: `out/terminal-0.1.0.arb`, 10,240 bytes, sha256 `5084b540…a977`, `arb-check` OK. The C++ `arbor` CLI itself is not written yet.
- Still open: the Zhaoxu machine-dependent port and linking the personality into `KERNEL.ELF`. No FreeBSD object is linked into `KERNEL.ELF` yet; M2 stays open.

### Rule 6 — scripts move to Python
- Every repository script is now Python 3 (`src/tools/*.py`, `src/tests/tools/host_test.py`); the 28 Ruby originals are removed. `RULES.md` rule 6 allows Python / sh / ps1, the Makefile invokes `$(PYTHON)` and depends on the `.py` files, and `build.ps1` / `run.ps1` locate `python` / `py`.
- Equivalence was checked against the Ruby tools, not assumed: `zxe-pack.py` is byte-identical on `aqua.uelf` (3 segments, 561 relocations) and `display.uelf`; `initramfs-compress.py`, `kernel-flat.py`, `find-ovmf.py`, `zxe-fixture-lib.py` and `spirv-test-gen.py` produce identical bytes; `zxe-check.py` matches the Ruby verdict on all 28 images in `out/`; `ext4-test.py` prints the same 23 lines; the host-detection suite passes 13/13.
- End to end: `make iso` builds `out/zhaoxu-os_x64_0.7.21_2026-11-21_ld.iso` (39,563,264 bytes) and the ported `smoke.py` boots it to `Boot complete`.
- `archive/boot-legacy/tools/*.rb` stays as archived history and is not part of any build.

## 0.7.1 「Yuanshuo」 build 20

### Licensing — GPL-2.0
- The project is relicensed from Apache License 2.0 to the GNU General Public License v2.0 (GPL-2.0-only). `LICENSE` now carries the canonical GPL-2.0 text; the Apache `NOTICE` file is removed. The README and CONTRIBUTING license sections are updated.
- GPL-2.0-only matched the Linux kernel (`GPL-2.0 WITH Linux-syscall-note`) planned for the XNU-style hybrid kernel: Linux 7.4-rc3 as an in-kernel auxiliary personality (Yuanshuo as the Mach-like core, Linux as the BSD-like personality) in a single image. That personality was replaced by FreeBSD 15.1 in build 21; see `docs/superpowers/specs/2026-11-20-xnu-freebsd-design.md`.
- Third-party components keep their own licenses (`THIRD_PARTY_NOTICES.md`); the project license does not relicense them. The rule-35 copyright footers are retained. `RULES.md` is unchanged (rule 41).
- The Linux kernel was never vendored; its `THIRD_PARTY_NOTICES.md` entry was deferred to the M2 integration. The XNU-style integration is staged (M1+); this build is the licensing + design milestone (M0).

## 0.7.1 「Yuanshuo」 build 19

### Color channel fix
- Limine's framebuffer pixel_format was inverted: the common GOP/BGRX layout (red_mask_shift > blue_mask_shift) was tagged as "swap", so render_pack exchanged R and B and blue chrome came out orange. The conversion now keeps BGRX native and only swaps for RGB framebuffers.
- The format convention is documented at the assignment: 0 = swap R/B, 1 = native. render_pack, unpack, blending and the compositor's logical_color all share it.

## 0.7.1 「Yuanshuo」 build 18

### RULES.md v1.96 compliance pass
- Rule 24: the system API banner is now `Z64` (`kernel/include/zos_api.h`); the old long product-name banner is gone.
- Rule 14/42: removed the in-tree `glob()`/`fnmatch()` implementation. nano now links gnulib's upstream glob/fnmatch, with `-DFNM_EXTMATCH=32` and `-D_GNU_SOURCE` in the Ruby driver; no self-developed "third party" is presented as upstream.
- Rule 6: the compiler shim is Ruby (`tools/x86_64-elf-gcc`), generated by `tools/posix-ports.rb`; no repository shell build script remains.
- Rule 3: Limine menu is branded, colored, remembers the last entry and offers Aqua/serial entries; boot-tested after the change.
- nano 8.4 still compiles with upstream gnulib glob; the remaining gap is the ZXE PIE relink and runtime test.

## 0.7.1 「Yuanshuo」 build 17

### Rule-compliance fixes
- The x86_64-elf compiler shim is now a Ruby script (`tools/x86_64-elf-gcc`) generated by the Ruby driver; no shell build script is part of the repository. `tools/posix-ports.rb` gained `--wrapper-only` for refreshing the shim without rebuilding the ports.
- `tools/posix-ports.rb` is the single Ruby entry point for the ncurses/nano port (download, configure, patch, build); the ad-hoc bash steps are gone.
- Limine's menu is now deeply customized per rule 3: 3 s timeout, remembered last entry, branding "Zhaoxu OS", Catppuccin-style terminal palette, and two entries (Aqua 1920x1080 and serial 1024x768). The ISO was booted after the change and still reaches Aqua.

## 0.7.1 「Yuanshuo」 build 16

### nano 8.4
- nano 8.4 now compiles for x86_64-elf against newlib and ncurses (binary at $HOME/nano-build/src/nano). The POSIX overlay gained pwd, utime, signal-set/sigaction shims, unlink/chmod/chdir/getcwd/getwd/dup2/getuid/getpwnam, dirname/basename and a real readdir-backed glob.
- `tools/posix-ports.rb` is the Ruby build driver for ncurses + nano, replacing the earlier ad-hoc shell steps; it writes the clang wrapper, builds the support archive, applies the ncurses config fixes and runs both configure/make passes.
- nano is not yet relaunched through the ZXE PIE linker script, so the binary is the upstream ELF link; wiring it into the initramfs and running it in a curses session is the next step.

## 0.7.1 「Yuanshuo」 build 15

### ncurses and terminfo
- ncurses 6.5 is built for x86_64-elf against newlib with the same clang wrapper, `-fPIC`, `ARFLAGS=rv`, and a small set of config fixes (newlib already supplies ssize_t/pid_t/intptr_t). The ZOS overlay headers supply dirent, glob, termios, ioctl and rlimit surfaces that newlib lacks for x86_64-elf
- `initramfs/usr/share/terminfo/z/zhaoxu` is a real compiled terminfo entry (80x25, ANSI cursor/color). `NLIBC_BOOT_TEST=1` now also runs a curses program: `ncurses hello 80x25` on screen and `ncurses hello: 80x25 ok`, exit 0
- Kernel additions needed by the port: VFS `lseek` (same SYS_LSEEK 53 for fds below 64) and `SYS_STAT` (77) returning is-dir/size, so the terminfo tree is discoverable
- nano 8.4 is downloaded and configured, but its gnulib portability headers still conflict with newlib's signal surface; it is not built yet

## 0.7.1 「Yuanshuo」 build 14

### newlib C library
- `tools/newlib.rb` downloads the pinned Ubuntu `newlib-source` 4.6 package, builds it for x86_64-elf with clang wrappers and `-fPIC`, and installs it under `$HOME/newlib/install/x86_64-elf`. The PIC requirement comes from the ZXE PIE loader; a non-PIC build carried 61k absolute relocations and could not be based anywhere.
- `user/lib/posix/libgloss.c` implements the newlib syscall layer: `_read/_write/_open/_close/_lseek/_fstat/_isatty/_sbrk/_exit/_getpid/_fork/_execve/_pipe/_dup2/_ioctl`, plus the public aliases newlib calls internally. `user/lib/posix/crt0.S` reads `[argc][argv][NULL]` from the ZXE entry stack, calls `main` and exits with its status.
- `make NLIBC_BOOT_TEST=1 iso` builds `user/newlib_hello.c` against libc.a/libm.a and runs it: `newlib hello: argc=1 argv0=/data/NLIB_HELLO.ZXE heap=ok`, exit 3
- Still open: ncurses and terminfo, then nano. The tty/termios ABI from build 13 is the base those ports will use.

## 0.7.1 「Yuanshuo」 build 13

### stdio redirection
- Each process carries `stdio[3]`. `dup2(zfd, 0/1/2)` duplicates the source into a private zfd and points the stdio slot at it, so `read`, `write` and `close` on 0/1/2 follow the redirect while the original descriptor stays independently closable. `close` on a redirected stdio slot restores the console default
- `fork` copies the stdio table and its descriptor references; `exec` preserves them
- Installing a descriptor at 3..63 is still rejected: VFS descriptors are a process-global table and are the next item on the POSIX list

### tty and termios
- `kernel/proc/tty.c` gives the default fd 0/1/2 terminal semantics: canonical line buffering with erase and echo, raw byte mode, and blocking reads that sleep while the driver key ring is empty
- `SYS_IOCTL` (76) implements TCGETS, TCSETS, TIOCGWINSZ (80x25) and FIONREAD. The termios and winsize layouts are identical in `kernel/include/tty.h` and `user/userlib.h`
- `POSIX_BOOT_TEST=1` now also forks a child that redirects fd 1 to a pipe, checks that `u_puts` lands in the pipe, then verifies `isatty`, TCGETS/TCSETS and the 80x25 window size

### Still open
- libc and ncurses are not ported yet, so nano cannot be compiled as upstream source. The next step is choosing the C library base (musl, newlib/libgloss, or a compact in-tree libc) and then building ncurses on top of the tty/termios ABI from this build

## 0.7.1 「Yuanshuo」 build 12

### POSIX pipes and descriptors
- `SYS_PIPE` (74): a 64 KiB ring buffer object in `kernel/ipc/pipe.c`, shared by a read end and a write end in the zfd table. Reads block until data or EOF, writes block while the ring is full; nonblocking mode returns immediately. `poll` reports POLLIN/POLLOUT/POLLHUP
- `SYS_DUP2` (75): installs a zfd descriptor at an exact number, closing the previous occupant and balancing pipe reader/writer counts. `fork` and `dup` copy pipe end counts and object references
- `pipe()` and `dup2()` join `fork()`/`exec()` in `user/userlib.h`
- `POSIX_BOOT_TEST=1` runs `/data/POSIX_TEST.ZXE`: a pipe written in the child and read in the parent, EOF after the writer closes, and dup2 to descriptor 90
- Still needed before nasm/nano/vim: stdio dup2 onto 0/1/2, termios/ioctl, signals, and a libc; zfd numbers stay 64..95 and VFS descriptors remain a process-global table

## 0.7.1 「Yuanshuo」 build 11

### Boot code archive
- Moved the pre-Limine boot manager (`efi_main.c`, `efi_bootmgr.c`, `bios_stage1.asm`, `bios_stage2.asm`, `uefi.h`, `zhaoxu.cfg`, `zhaoxuenv`, the old menu font and `tools/bios-img.rb`/`tools/iso-bios.rb`) to `archive/boot-legacy/` with a README that records what each file did. Nothing in the archive is compiled, packed into the ISO or installed
- Removed the dead Makefile targets for `BOOTX64.EFI`, `OSLDRX64.efi`, `bios.img` and the blob objects, and taught `tools/native.rb` to build only the Limine kernel/initramfs path

### POSIX fork/exec
- `SYS_FORK` (72): copies every user mapping with its page flags, duplicates the zfd descriptor table with object refcounts, and starts a child kernel thread that enters Ring3 through `user_enter_context` at the parent's post-syscall RIP/RSP with `rax=0`. The parent receives the child pid; `wait` reaps it normally
- `SYS_EXEC` (73): copies the path and argv into kernel memory, loads the ZXE/ELF image into a brand-new address space, and only after a successful load switches CR3, releases the old pages and page tables, and rewrites the interrupt frame to the new entry/stack. A bad image leaves the caller running its old program
- `user/userlib.h` gained `sys_fork()` and `sys_execv()`; `user/forkexec.c` exercises fork, wait and exec
- `FE_BOOT_TEST=1` runs `/data/FORKEXEC.ZXE`: the child exits 7, the parent reaps it, then execs `/data/HELLO.ZXE`, which prints its argv and exits 7 from the same pid

### Still open
- Native ports of fasm, nasm, nano, vi/vim and neofetch are not included in this build. They need a POSIX runtime (libc, termios/termcap, pipes/job control for an editor, and a shell for upstream neofetch); fork/exec is the first layer of that work

## 0.7.1 「Yuanshuo」 build 10

### Limine boot manager
- Limine 12.10.0 replaces the in-tree boot manager for both UEFI and legacy BIOS. `third_party/limine` vendors the binaries, the Limine Boot Protocol header and the host deploy tool; `boot/limine.conf` is the ISO menu
- `kernel/boot/limine_boot.c` implements the Limine protocol: it converts the memmap, framebuffer, RSDP, executable address and modules (KERNEL.BIN, MiSans.ttf, WALL.RGB) into `struct boot_info`, installs the 64 GiB identity map over Limine's HHDM tables and maps the BSS boot stack
- `make iso` now produces a hybrid Limine ISO: BIOS El Torito entry, UEFI El Torito entry and `limine bios-install`; both firmware paths boot to Aqua Desktop Environment
- The installer writes Limine's BIOS boot sector and stage 2, the ESP loader, both `limine.conf` copies and a `limine-bios.sys`, then verifies the boot sector by its `EB 3C 90` + `LIMINE` signature
- Limine 12 has no ext4 driver, so the installed ESP carries `KERNEL.ELF`, `KERNEL.BIN`, `MiSans.ttf` and `WALL.RGB`; the ext4 `ZHAOXUROOT` volume holds no boot files. `limine.conf` addresses the ESP as `boot(2)` on GPT and `boot(1)` on MBR
- An installed 256 MiB GPT disk was booted to Aqua under both OVMF and SeaBIOS, with the boot font and wallpaper loaded from the ESP

### Installer reliability
- Rewrote `append_long_entry()` to walk the directory chain with the write-side FAT cache and to reuse a free slot before chaining a cluster. The old walk treated end-of-chain as a cluster number and performed a failed out-of-range read per guard iteration, stalling the ESP stage for minutes
- VFAT long names are written for `limine.conf`, `limine-bios.sys`, `MiSans.ttf` and `WALL.RGB`; the FAT sector cache, batched contiguous cluster writes and cheap FSInfo invalidation cut ESP writes
- `fat32_make_esp()` reports granular failure codes (-2..-11) and the installer maps them to concrete messages
- The target scan recognizes a disk carrying Limine's boot sector as a previous Zhaoxu install, so reinstall over an installed disk works
- Install stages are now 7 (8 with a data partition): root format, MBR/GPT, Limine BIOS, gap, ESP, verify, flush. The ext4 format is synced before the partition table is written
- The kernel and modules are written once, to the ESP, instead of being duplicated into the ext4 root

### Scheduler and diagnostics
- Per-thread FXSAVE/FXRSTOR saves x87 and SSE state across preemption, so a Ring3 program using SSE cannot be resumed with another thread's XMM registers
- User exceptions now print the full register file; `FT_Done_Face` faults became diagnosable from serial alone
- Kernel install stages, ESP payload writes and user font loading log their tick counts and pointers

## 0.7.1 「Yuanshuo」 build 9

### Custom partitioning and formatting
- The installer gained a "自定义分区" screen. ESP size cycles through 64/128/256 MiB; an optional FAT32 `ZHAOXUDATA` data partition cycles through none/64/128/256 MiB; the ext4 system partition takes the remaining space and the screen rejects layouts that leave it under 64 MiB
- `SYS_INSTALL_CONFIG` (71) carries the chosen sizes to the kernel. `install_run()` builds MBR/GPT entries for a third data partition, formats it as FAT32 with the `ZHAOXUDATA` label, verifies the boot sector, and reports it as an extra progress stage
- Custom layouts were installed end to end in QEMU: a 256 MiB GPT target produced a 64 MiB ESP, a 127 MiB ext4 root and a 64 MiB FAT32 data partition; the installed system mounted the root on `/mnt` and the data partition on `/media/1`
- Fixed `blockfs.c` dropping every partition whose start sector was larger than its own length. GPT volumes beyond the first partition (including a late data partition) were never probed; all scanned partitions now validate against the disk size instead
- The confirmation screen shows the exact ESP/root/data sizes; the select screen shows the current layout summary and has a separate refresh button

### Cursor, flicker and input latency
- The compositor no longer draws the pointer into the system-RAM back buffer. The back buffer stays cursor-free; erasing the old pointer is one clean blit of the previous rectangle, and the new pointer is composited directly onto the framebuffer. Window repaints can no longer flash or ghost the pointer
- Idle pointer movement now costs one small copy instead of restore+redraw in the shared back buffer. The DWM loop sleeps 4 ms (250 wakeups/s) instead of 1 ms, cutting idle CPU load while keeping the pointer responsive
- Button labels use server-side UTF-8 text measurement (`ADE_TEXT_CENTER`) instead of counting bytes, so CJK labels stay centred and no longer spill out of their buttons
- The custom-partition screen sizes its header, rows and buttons for the real ADE client rectangle; the data row no longer overlaps its label

### Screenshots and docs
- Regenerated the desktop, application, installer and custom-partition screenshots from this build and updated their README captions; the older shell/editor/disk/font screens are explicitly marked as earlier-release captures
- INSTALL.md documents the layout editor, the FAT32/ext4 formatting of each partition, the data partition's `/media/<n>` mount and the confirmation/progress/reboot flow

## 0.7.1 「Yuanshuo」 build 8

### Installer progress and post-install actions
- The installer now runs the blocking disk write in a `worker` child process. The UI process maps a shared page, passes its shm id to the worker, and paints a real stage counter from `SYS_INSTALL_PROGRESS` (70) while the child blocks in the kernel
- `install_run()` reports nine stages: ext4 format, kernel image, assets, MBR, stage 2, gap clear, ESP, verify and flush. The former 0/1 indeterminate display is gone; long formats now show the denominator before the first write
- The done screen offers "立即重启" (`SYS_REBOOT`, 69) next to "关闭安装程序". Reboot pulses the keyboard-controller reset line and falls back to a triple fault
- The select screen has a "刷新磁盘" button; failures rescan the block devices before returning to selection
- New syscalls: `SYS_REBOOT` (69) and `SYS_INSTALL_PROGRESS` (70). `install_run()` takes an opaque progress context so concurrent SMP installs cannot share a global pointer
- Verified in QEMU: a real click-through install showed `写入阶段 2 / 9` mid-write, completed to the green screen, and the reboot button path was compiled and wired

### OpenGL
- Texture mapping: `glGenTextures`/`glBindTexture`/`glDeleteTextures`, `glTexImage2D` (RGB/RGBA unsigned byte, up to 512x512), nearest and bilinear filtering, repeat/clamp wrapping, `glTexCoord2f`, and MODULATE/DECAL/REPLACE texture environments. Texture coordinates are interpolated perspective-correctly
- Lighting: `glNormal3f`, `glLightfv` for `GL_LIGHT0..7`, `glLightModelfv`, `glMaterialfv` (ambient/diffuse/specular/emission/shininess). Supports directional and positional lights, ambient/diffuse/specular terms and per-vertex normal transformation; attenuation and spot cones remain open
- Blending and fragment tests: `glBlendFunc` for ZERO/ONE/SRC_ALPHA/ONE_MINUS_SRC_ALPHA, `glAlphaFunc`, and `glScissor` with bottom-left OpenGL coordinates
- The regression gained `[gl ] texture PASS`, `lighting PASS`, `blend PASS` and `scissor PASS`; the full suite passes on the host and in QEMU

### Vulkan
- Vendored the Khronos Vulkan-Headers core C headers and added a software Vulkan 1.0 ICD under `src/user/lib/vulkan`
- Implemented instances, one CPU physical device, devices and queues, host-visible coherent memory, buffers, RGBA8 images, image views, render passes, framebuffers, shader modules, graphics pipelines, command pools/buffers, fences and synchronous queue submission
- Added a real SPIR-V 1.0 interpreter (`spirv.c`) for straight-line float/vector shaders: types, constants, location/builtin decorations, global variables, loads/stores, composite construct/extract, add/sub/mul/div, vector-times-scalar and dot. Unsupported instructions fail instead of guessing
- The CPU triangle rasterizer runs the vertex stage per vertex and the fragment stage per covered pixel, writes RGBA8 and supports simple pipeline blending
- `make out/vk_test.zxe` creates a real instance/device/image/render pass/pipeline/command buffer, draws an RGB triangle, submits it, maps the image and checks the centre/corner pixels. `VK_BOOT_TEST=1` prints `[vk ] Vulkan PASS` and exits 0 in QEMU
- Not complete: there is no WSI/swapchain, descriptor sets, uniform buffers, compute, depth/stencil, multisampling, semaphores/timeline fences, tiling or GPU acceleration. The desktop still renders through `kernel/gfx/render.c`, so rules 37/38 remain open

### Tooling and docs
- `tools/spirv-test-gen.rb` generates the two regression SPIR-V modules from explicit instruction streams
- THIRD_PARTY_NOTICES records Vulkan-Headers (Apache 2.0)
- README, AGENTS, CLAUDE, INSTALL and the ZAF graphics status describe the actual OpenGL/Vulkan subset instead of claiming full compliance

## 0.7.1 「Yuanshuo」 build 7

### Installer
- The graphical installer is now a short Windows-style flow: it scans the block devices, keeps only blank or previously installed Zhaoxu targets, auto-selects a single disk, and asks for one confirmation before writing. GPT is the default; MBR is behind the advanced toggle
- Target parsing now matches the real `sys_disks()` column layout (`Idx Port Size MiB Label Role Model`). The previous parser read the label as the role, so a valid blank disk was reported as missing
- The window is tall enough for the select screen, so the install button is no longer clipped by the title bar, and the completed/failed screens show the full UTF-8 warning text
- Installation errors reach the UI through `SYS_INSTALL_ERROR` (68); the failed screen prints the kernel's actual reason instead of a generic message
- Verification: the ISO was booted with a blank 256 MiB disk, installed through real mouse clicks (select, confirm, start), and the target disk then booted to the Aqua Desktop Environment. GPT was checked end to end

### Installer correctness and hardening
- A UEFI install now writes both `EFI/BOOT/BOOTX64.EFI` and `EFI/BOOT/OSLDRX64.EFI` to the ESP. Without the second-stage loader, ZhaoxuBootMgr stopped at `cannot read OSLDRX64.efi` on the installed disk
- `fat32_make_esp()` takes the osloader blob, verifies both files after writing, and installs now fail early if either embedded loader is missing
- The kernel rejects target disks above 2 TiB, root partitions outside the 32-bit ext4 range, missing/oversized kernel, loader, font or wallpaper blobs, and a kernel flat image over `0x8F000` bytes
- An MBR install clears the old backup GPT at the end of the disk, so stale GPT headers cannot make firmware ignore the new MBR
- The embedded initramfs now carries `/boot/OSLDRX64.EFI`; the kernel flat image remains below the stage-2 staging cap at 563200 bytes

### Desktop protocol
- `ADE_TEXT_MAX` grows from 32 to 160 bytes. CJK glyphs take three bytes, so ordinary installer warnings were cut mid-codepoint and rendered as replacement boxes
- `ade_text` truncates on a UTF-8 boundary; `IPC_MSG_MAX` grows from 64 to 256 so the larger text command still fits one port message

### OpenGL
- Ported a software OpenGL 1.1 immediate-mode subset under `src/user/lib/opengl`: column-major matrix stacks, translate/rotate/scale, ortho/frustum projection, `glBegin/glEnd` for points, lines, triangles, strips, fans and quads, near-plane clipping, perspective divide, flat and Gouraud shading, 16-bit depth test (`LESS`/`LEQUAL`/`ALWAYS`/`NEVER`), front-face culling and `glReadPixels`
- `make out/gl_test.zxe` builds an off-screen regression that checks clear, quad and triangle rasterization, Gouraud interpolation, depth ordering in both draw orders, CCW culling, matrix push/pop, perspective projection and RGBA readback
- `GL_BOOT_TEST=1` (with `out/data.img` attached) prints `[gl ] quad PASS` … `[gl ] OpenGL PASS` and `[gl ] GL_TEST exit 0`; the same regression passes natively on the host and in QEMU
- This is a GL 1.1 command subset, not full OpenGL: lighting, textures, display lists, evaluators, extensions and shaders are not implemented, and the desktop still renders through the ZOS raster path. Rules 37/38 remain release blockers for a fully compliant build

### Tooling
- `tools/screenshot.rb` gains `SMOKE_BOOT=iso-install` (CD plus one blank target), `SHOT_NO_SHELL` for graphical boots, `SHOT_SERIAL` to persist the serial log, and byte-wise PPM header parsing that survives whitespace bytes inside pixel data and Windows text-mode pipes

## 0.7.1 「Yuanshuo」 build 6

### Fixes
- The cursor stays visible while the pointer is stationary. DWM now remembers that a pointer has been seen and keeps compositing it at its last position instead of hiding it whenever a damage repaint finds no fresh mouse report
- FreeType glyphs are no longer flipped or vertically offset. `ttf_freetype.c` converts `bitmap_top` into the line-top coordinate expected by `render.c` (`ascender - bitmap_top`) and rounds the advance consistently, so glyphs sit on the baseline instead of bobbing per glyph
- Verified with a real OVMF desktop run: the welcome window, calculator, menu bar and Dock render upright aligned text; six framebuffer captures taken 450 ms apart with a stationary pointer differ only in the menu-bar clock area

## 0.7.1 「Yuanshuo」 build 5

### Fonts and rendering
- Rule 17: removed the non-OpenType WenQuanYi fallback from the boot loader, Aqua Desktop Environment, installer, ISO and FAT data image. Only MiSans, MiSans Global and Inter (OpenType) are accepted. The ISO ships `install/MiSans.ttf` and `install/Inter.otf`; the installer writes `/MiSans.ttf` to the ext4 root and the boot loader reads it from there
- Rule 18: vendored FreeType 2.13.3 (TrueType, CFF, sfnt, psnames, smooth rasterizer) with a freestanding `ftoption`/`ftmodule` configuration, an x86_64 setjmp/longjmp shim and the required libc helpers. The Ring3 Aqua Desktop Environment font adapter now uses FreeType; the kernel boot font path is unchanged for now
- `make out/ft_test.zxe` builds a FreeType regression client that loads the boot volume font and rasterizes U+4E2D. `FT_BOOT_TEST=1` (with `out/data.img` attached) prints `[ft] face MiSans`, `[ft] glyph 26x31 covered 334`, `[ft] FreeType PASS` and exits 0
- FreeType's smooth rasterizer is compiled at `-O0` because GCC miscompiles it at `-O1`/`-O2` with this configuration; the remaining modules use `-O2`. The optional autofit, pshinter and raster1 modules are omitted to keep the image small
- The legacy BIOS stage 2 staging cap is raised from 448 KiB to 0x8F000 bytes (ends at 0x9F000, below the EBDA at 0x9FC00) so the FreeType-enabled initramfs still fits below 1 MiB

### Build output
- The installer ISO follows rule 11: `out/zhaoxu-os_x64_{version}_{build}_ld.iso`; `smoke.rb`, `screenshot.rb` and the documentation use the same pattern

### Known open items
- Rules 37/38 are not implemented: there is no Vulkan loader/ICD and no OpenGL port. The desktop still renders through the ZOS raster path. A compliant build needs a working GPU backend and the Vulkan/OpenGL ports; this release does not claim those rules

## 0.7.1 「Yuanshuo」 build 4

### Wayland
- Ported the stock libwayland 1.23.1 client core (`wayland-client.c`, `wayland-util.c`, `connection.c`) with generated core and xdg-shell bindings; libffi 3.4.6 supplies the x86_64 SysV `ffi_call` path used to invoke listeners
- `user/lib/wayland/compat.c` implements malloc/free, string functions, errno, pthread mutex/condition variables, `socket`/`connect`/`fcntl`/`sendmsg`/`recvmsg`/`poll` and `clock_gettime` over ZOS syscalls; `wayland-os-zos.c` replaces the Linux descriptor-flag helpers
- `make out/wlstock.zxe` builds `/data/WLSTOCK.ZXE`, which binds `wl_compositor`/`wl_shm`, submits a 320x240 shm frame through SCM_RIGHTS and exits 0; `WLSTOCK_BOOT_TEST=1` starts `/bin/wayland` before it
- The kernel `sendmsg`/`recvmsg` paths honor `MSG_DONTWAIT` per call; stock libwayland drains the display socket this way even when the descriptor itself is blocking

### Documentation
- `THIRD_PARTY_NOTICES.md` records libwayland (MIT) and libffi (MIT), including the generated xdg-shell bindings

## 0.7.1 「Yuanshuo」 build 3

### Kernel
- New `eventfd`, `timerfd`, and `epoll` objects in `kernel/ipc/`, registered as zfd descriptor types above `ZFD_BASE`; duplicate and `SCM_RIGHTS` paths keep the existing refcount rules
- New syscalls 61–67: `eventfd`, `timerfd_create`, `timerfd_settime`, `timerfd_gettime`, `epoll_create1`, `epoll_ctl`, `epoll_wait`; `kernel/proc/syscall.c` and `user/userlib.h` stay in sync
- `eventfd` supports `EFD_SEMAPHORE` and nonblocking reads/writes; `timerfd` uses the scheduler tick timeline for relative and absolute expirations, including repeating intervals; `epoll` is level-triggered, caps registrations at 32 per object, and reports only the events requested for each descriptor
- `/data/M5_TEST.ZXE` and `M5_BOOT_TEST=1` cover the event loop: serial output is `[m5] eventfd, timerfd, epoll PASS` followed by `[m5 ] M5_TEST exit 0`

### Documentation
- All project documentation except `AGENTS.md` is now in English; the GitHub repository description is in English too. Product names, commands, paths, code blocks, and the `Nexsteaduser` copyright lines are unchanged

## 0.7.1 「Yuanshuo」 build 2

### Display
- `/bin/display` no longer hardcodes the control queue size at 64: `virtio-gpu-gl-pci` uses 256 and 2D devices use 64, so the queue size is now validated against the device value and allocated accordingly, fixing `invalid control queue size` on gl devices
- Control commands now check the response header; `0x12xx` responses count as failures, so a failed resource creation is no longer treated as success and carried on into scanout
- 3D resources are sent as a 72-byte `RESOURCE_CREATE_3D` (the extra 4 bytes of padding are required by the QEMU 11.1 uapi), and `CTX_CREATE`, 3D resource creation, and `CTX_ATTACH_RESOURCE` succeed on `virtio-gpu-gl-pci`; `SET_SCANOUT` still returns `0x1203` because virglrenderer in QEMU on Windows registers neither 2D nor 3D resources, so GPU composition and dmabuf remain pending

### Fixes
- The virtio-gpu command numbers in the legacy `apps/drivers/virtio_gpu.c` are back to the canonical values: `GET_CAPSET_INFO` 0x0108, `RESOURCE_CREATE_3D` 0x0204, `SUBMIT_3D` 0x0207; the 3D resource struct gains `padding`
- The `8sh` target now depends explicitly on `kernel/include/version.h`, so the shell banner no longer shows the old build number after `make version`
- When `tools/version.rb` updates `/etc/version` it now replaces the old `build N` too, so `build 2 build 1` no longer appears
- `virtio-gpu-pci` regression: the 64-entry control queue, 2D resources, Wayland frame submission, and the compositor connection all work with no command errors

## 0.7.1 「Yuanshuo」 build 1

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
