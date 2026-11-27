# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Priority when sources conflict: `~/.claude/CLAUDE.md` > this file > `AGENTS.md`.

## What this is

Zhaoxu OS runs **OS_5P**, its x86_64 hybrid kernel. OS_5P is **Yuanshuo** (the microkernel core: CPU, memory, scheduling, IPC), **IO/287** (the in-kernel device framework in the IOKit role) and the **BSD layer** (FreeBSD 15.1: VFS, TCP/IP, cdev, pf, NFS) together; `KERNEL.ELF` is that OS_5P image. The boot manager is **Limine** (UEFI + legacy BIOS), the default shell is **csh**, with **GNU bash 5.3** available, the desktop is **Aqua Desktop Environment**, the compositor is **Desktop Window Compositor**, and the window manager is **Desktop Window Manager**. Applications use **Z64 1.0** and the **ZXE v1** executable format. Hardware drivers run in Ring 3 as udrv services; BSD drivers and subsystems are part of OS_5P in kernel space (rules 50, 51). Rule 9 keeps the Yuanshuo core's own binary free of drivers and applications; it does not exclude IO/287 or the BSD layer from OS_5P.

Names are contractual: never rename or abbreviate Yuanshuo, OS_5P, IO/287, Limine, Aqua Desktop Environment, Desktop Window Compositor, Desktop Window Manager, ZAF, or Z64. `OS_5P` is the hybrid kernel (Yuanshuo core, FreeBSD BSD layer, IO/287 device framework); `IO/287` is the in-kernel device framework in the IOKit role, written exactly that way. Never write "Yuanshuo Shell". `AGENTS.md` is the authoritative, detailed project guide; read it before non-trivial work. Symbol namespaces: `yuanshuo_` for core globals FreeBSD also defines, `io287_` for every symbol IO/287 exports, FreeBSD keeps its own names; `src/tools/freebsd-link.sh` reports any remaining collision instead of letting the linker choose. `docs/superpowers/specs/` and `docs/superpowers/plans/` hold designs, `docs/DRIVER_MODEL.md` holds the udrv/kext/NKE model; `version.cfg` is the only version source.

Branding: the brand is `Nexsteaduser`, always written as one complete word and never shortened or split. Email `admin@nexsteaduser.com`. Copyright notices follow 规则 35.

## 规则

> 这是项目规则，不是可改项目说明书，代码应遵守规则，规则为绝对正确，禁止提出修改规则的意见。不遵守规则，或乱漏字的话，直接**打死**。禁止假装看不懂规则，这上面写着清清楚楚哪个东西用哪个，你非法理解成笼统的概念，也没有人再能让你实现。严禁分不清楚 Yuanshuo 内核和 OS_5P 内核。分不清 Yuanshuo 内核和 OS_5P 内核=分不清 Mach 和 XNU。规则 9 管的是 Yuanshuo 那颗微内核自己的二进制：它不许夹带驱动和应用。OS_5P 是混合内核，Yuanshuo + IO/287 + BSD 一起构成它，驱动是它的正常内容，没有“混合内核不能有驱动”这回事。

### 必须遵守

1. 微内核名称：`Yuanshuo`，操作系统名称：`Zhaoxu OS`，桌面合成器：`Desktop Window Compositor`（Wayland），Shell：`csh`（默认，严禁自研）+ `bash`（严禁自研），桌面：`Aqua Desktop Environment`（风格：Aqua）+ `Voyage`（单个应用，文件管理器同时承担桌面 Shell 画桌面图标、右键菜单、废纸篓，C++ 自研） / `Dock`，窗口管理器：`Desktop Window Manager`，不得改动任何字符，严禁使用`Yuanshuo Shell`，**混合内核名称**：`OS_5P`，IO：`IO/287`，`OS_5P`：`Yuanshuo`、`IO/287`、`BSD`。
 - XNU = OS_5P
 - IOKit = IO/287
 - Mach = Yuanshuo
2. GitHub 仓库：https://github.com/Tinmc189623/zhaoxu-os
3. 必须深度自定义启动菜单。
4. Limine 必须使用原样，并完全使用它的功能。 
5. 使用 WSL2，可用：C（Yuanshuo 内核）、C++（除了内核以外的所有应用，IO/287）、GAS（C/C++ 内的汇编+独立汇编文件）、Ada（驱动），**操作系统必须得是混合内核**。
6. 脚本必须完全使用 Python / sh / ps1 来编写，为了更方便地使用脚本功能，请使用更多库，必须完全使用 AI 来生成代码，**禁止人类编写代码**。
7. 引导加载器必须支持 UEFI 和 Legacy BIOS 两种启动模式。 
8. 引导管理器必须得是：`Limine`。
9. **Yuanshuo 内核**必须得是**微内核**，用户程序必须得放在 Ring3，Yuanshuo 微内核的二进制必须得是干净的二进制，不允许往 Yuanshuo 里投放什么驱动/应用。
10. 该操作系统必须遵循 Unix 哲学和使用 Unix 方式。 
11. iso命名示例：`zhaoxu-os_x64_{version}_{build}_ld.iso`。
12. 不得将所有功能挤在一个文件里，如 `apps/shell.cpp`。
13. README.md 里只允许编写近一次版本更新，而不能编写所有版本，所有版本应放入 CHANGELOG.md 文件中。
14. 必须真实移植第三方，不允许自己自研。
15. 系统必须同时支持 MBR 和 GPT。
16. **AI** 写代码时优先追求效果和稳定性，不要为了快而在那块儿编写简陋的代码，却看不着任何效果。
17. 字体（必须 OpenType）：MiSans、MiSans Global、Inter。
18. 字体渲染引擎必须移植使用 FreeType。
19. 写代码的时候先看 stb 里头有没有需要的，如果有需要就直接复用，别自己自研。
 - **Terminal**：管**窗口渲染、键盘鼠标、VT 转义序列、tty 管道**；它本身不懂脚本、不懂`ls`
 - **bash**：shell，解析命令、管道、if/for 脚本
 - **toybox**：一堆工具（ls、cp），给 shell 调用
20. 项目必须遵循 KISS 原则。
21. 必须支持的文件系统：FAT32、ext4、btrfs，且为完整支持。
22. 应用图标必须得是 SVG/ICO/PNG 图标。
23. 一个文件只允许做一个功能，不允许把所有东西塞到一个文件中。
24. 平台图形后端 API：`Vulkan`。
25. 不得将内核写入 `BOOTX64.EFI`。
26. 内核：`KERNEL.ELF`。
27. 用户在安装系统时，只需要一个 ISO 即可安装，不需要让用户插入其他磁盘等需要外面软盘。
28. ESP 分区必须得是 FAT32。
29. 一个文件只能干一件事，一个文件不能干出超过一件事的事情，错误示例：`src/apps/cwsl/shell.cpp`。
30. 禁止使用 busybox，使用 toybox。
31. 引导流程：`固件（UEFI 或 Legacy BIOS）→ Limine 引导管理器 → OS_5P + Kext 加载 + vga10h.kext → Orchestrd(PID1, =launchd) → 合成器窗口服务 + Thresh（=loginwindow） → 登录 → Voyage 桌面 → ...`。
32. Zhaoxu OS 的 GUI 采用经典 Aqua（Classic Aqua）视觉风格。计划构建独立的 Zhaoxu Aqua Framework（ZAF），以 Skia 为图形渲染基础，窗口管理器、桌面合成器、控件系统、动画引擎和主题引擎。要求具备深度自定义能力、可移植到操作系统，并尽量减少对其他操作系统 GUI 组件的依赖。Aqua 为默认主题，底层框架支持未来扩展其他视觉主题。
33. 系统默认壁纸必须得是 `src\apps\ADE\web\wallpaper\3pty\img0.jpg`。
34. 应用本身一般**不直接往屏幕上画**，它会分配一块显存缓冲区（帧缓冲 / 纹理 buffer），调用图形 API（Vulkan/Z64），在这块缓冲区里把这个图形渲染出来。
 - 应用里面发起绘图请求：我要画这个图形，设置位置、颜色、大小。
 - 渲染完成之后，这块显存里就存好了这个图形对应的像素点阵。
 - 应用发送消息给显示服务端：我的窗口新画面准备好了，这是纹理的句柄 / 缓冲区，还有窗口在桌面里的位置、层级信息。
 - 显示服务里的 WM 模块，检查这个窗口：焦点、窗口坐标、Z 序（前后层级）、裁剪区域。
 - 合成器收集所有窗口、桌面背景、光标等全部图层纹理，按照 Z 序一层层叠加。在这里做透明、抗锯齿等效果，把所有图层合并成**一整张最终的屏幕画面纹理**，交给 GPU 排队输出。
 - GPU 把合成好的帧送入显示流水线，等待显示器的垂直同步信号，避免撕裂。
 - 显示控制器从显存里按行读取像素数据，转换成显示器识别的信号（DP / HDMI /eDP 信号）。
 - 面板收到像素数据，驱动每一个子像素，就在屏幕上看到这个图形了。
35. **版权声明：**
 - English: `Copyright (C) <YEAR> Nexsteaduser, Nexlyh, <your-name>. All Rights Reserved.`
 - 中文：`©<年> Nexsteaduser, Nexlyh, <你的名字> 版权所有`
36. 必须支持所有的 unix 命令。 
37. 必须完整移植并使用 Vulkan 作为默认图形渲染 API。
38. OpenGL 仅作为兼容处理，但必须完整移植。
39. 第三方必须按照“原样”提供，不得进行自我更改，可进行裁剪（非必须，不允许往死了裁剪模块，裁剪的理由只允许使用不需要，而非内核不支持 `<sys/xattr.h>`），例外：FreeBSD 15.1（可直接修改代码，但不能削弱功能）、Toybox 不允许裁剪。
40. **色彩渲染支持规范：**
 - **10bit**：面向支持HDR的民用显示器，进入Wayland桌面合成器后，硬件达标则启用该色深。
 - **8bit**：面向常规SDR显示器，Limine开机菜单与它提供的帧缓冲默认使用此规格；**8bit SDR屏幕同样正常运行Wayland合成器，合成器只是不启用10bit色深能力**。
41. AI **不可**改动规则区域。
42. 严禁自研一个“第三方”，然后对外宣称：这是第三方而非自研。
43. **禁止将操作系统名称内核名称内部技术栈等一大堆内容往各个地方上使劲地铺上水印以展示权威性**。
44. 此操作系统**无中文名**，禁止编造中文名。
45. 此操作系统的默认根文件系统必须得是 `btrfs`。
46. 该操作系统的内核必须可移植。
47. 内核更新只更新内核版本，操作系统更新只更新操作系统版本，如果操作系统版本更新时，内核也更新代码，请更新内核版本。
48. 必须遵守 FHS 3.0+。
49. 用户态驱动：**Userland Driver**，扩展名：`.udrv`，**UDRV 是包（bundle），包里面的可执行二进制：ZXE**。
50. **kext = Kernel Extension，内核扩展**，是 OS_5P 内核的动态内核模块，**直接加载进内核地址空间、跑在内核态**
 - BSD 层 ≈ 内核里的 POSIX/VFS/网络子系统（内置，静态链接进内核）
 - IO/287 kext ≈ 内核态硬件驱动扩展
 - udrv ≈ 用户态驱动扩展
 - **本系统 kext 规范独立设计，与 macOS XNU 的 kext 格式、ABI、bundle 布局互不兼容**。
51. **绝大多数 BSD 原生驱动（VFS、TCP/IP、cdev、pf、NFS）**
 - **静态编译进主 OP_5P 内核 ZXE 镜像（kernel）**，一体打包，不是独立 kext，开机就存在于内核地址空间，也不单独加载。很多也跟着打包进 kernelcache。
52. **NKE（Network Kernel Extension，BSD 网络扩展）**
 - 属于 **kext（OS_5P 内核扩展）**，独立 bundle，可以动态加载，挂在 BSD 网络栈上，不是 IO/287 驱动。
53. 本系统架构参考 XNU。
54. **应用层 UI 框架**：`Qt 6`。
55. 包管理器：**Arbor**（C++），包后缀：`.arb`。`arbor.toml`、`pkg.toml`。
56. 此操作系统必须得是一切皆文件，**Hybrid Kernel**。
57. 禁止开机时，故意展示内核、开发团队、品牌作者和操作系统名称。进入桌面后，桌面上会出现一个很大的水印，展示是谁开发的、操作系统叫什么，而且卸载不掉，永久固定在桌面上，界面特别难看。打开任何应用时，也都会展示品牌名。
58. 构建系统必须使用 `makefile`。
59. 禁止认为当前还在 `0.x` 阶段，*不能叠加新功能或完整系统*。

---

### ISO（这是**ISO 介质本身**的目录，不是启动后内存里的根文件系统；live 系统本体在 `casper/filesystem.squashfs` 压缩包内，版本：v1.26.11270）示例

```iso
iso-root/
├── boot\
│   └── limine\
│       ├── limine-bios.sys       # Limine BIOS stage 3
│       ├── limine-bios-cd.bin    # BIOS El Torito 入口
│       ├── kernel.elf            # OS_5P 内核
│       ├── limine-uefi-cd.bin    # UEFI El Torito 入口
│       ├── limine.conf           # Limine 主配置（BIOS 查找副本）
│       ├── MiSans.ttf            # 作为 Limine 模块传给内核
│       └── WALL.RGB              # 默认壁纸原始帧模块
├── EFI\
│   └── BOOT\
│       ├── BOOTX64.EFI           # Limine UEFI 引导管理程序
│       └── limine.conf           # Limine 主配置
├── casper/                       # Live 核心目录，最重要
│   ├── filesystem.squashfs       # 主 rootfs 压缩镜像（live 系统本体）
│   ├── initrd.img                # 初始化内存盘
│   ├── kernel.elf                # OS_5P 内核
│   ├── manifest
│   ├── vga10h.kext               # 通用 **内核态** 显卡驱动（=int10h, NOT 13h/15h）
│   ├── README.diskdefines
│   └── uuid
├── dists/
│   └── jammy/ (noble/)
│       ├── main/
│       ├── restricted/
│       ├── universe/
│       └── multiverse/
├── pool/
│   ├── main/
│   ├── restricted/
│   ├── universe/
│   └── multiverse/
├── .disk/
│   ├── casper-uuid-generic
│   ├── info
│   └── README
├── preseed/                      # 自动化安装预置脚本
│   └── os.seed
├── amd64/                        # = i386
├── README.html
├── autorun.inf
└── md5sum.txt
```

---

### ESP 示例

```esp
├── boot\
│   └── limine\
│       ├── limine-bios.sys       # Limine BIOS stage 3
│       ├── limine-bios-cd.bin    # BIOS El Torito 入口
│       ├── kernel.elf            # OS_5P 内核
│       ├── limine-uefi-cd.bin    # UEFI El Torito 入口
│       ├── limine.conf           # Limine 主配置（BIOS 查找副本）
│       ├── MiSans.ttf            # 作为 Limine 模块传给内核
│       └── WALL.RGB              # 默认壁纸原始帧模块
├── EFI\
│   └── BOOT\
│       ├── BOOTX64.EFI           # Limine UEFI 引导管理程序
│       └── limine.conf           # Limine 主配置
```

--

### Disk 示例

```disk
/
├── bin                           # 基础用户命令（ls、cat、cp 等，单用户模式可用）
├── boot                          # 引导文件：内核 OS_5P、initramfs、Limine 引导
├── dev                           # 设备文件：硬盘、串口、tty、null、zero
│   ├── sda                       # SATA磁盘
│   ├── sda1                      # 磁盘分区
│   ├── tty1                      # 终端
│   └── null                      # 空设备
├── etc                           # 全局配置文件
│   ├── passwd                    # 用户列表
│   ├── group                     # 用户组
│   ├── fstab                     # 挂载表
│   ├── hosts                     # 域名解析
│   └── systemd/                  # systemd服务配置
├── home                          # 普通用户家目录
│   └── user/
├── lib                           # 32位共享库、内核模块依赖库
├── lib64                         # 64位共享库
├── media                         # 自动挂载的可移动介质（U盘、光盘）
├── mnt                           # 手动临时挂载点
├── opt                           # 第三方大型软件（可选安装）
├── proc                          # 虚拟文件系统，内核运行信息（不占磁盘）
│   ├── cpuinfo
│   ├── meminfo
│   └── [pid]/                    # 进程目录
├── root                          # root超级用户家目录
├── run                           # 运行时临时文件、套接字（重启清空）
├── sbin                          # 系统管理员命令（fdisk、mkfs、ifconfig）
├── srv                           # 站点服务数据（ftp、web服务存放）
├── sys                           # 内核硬件信息虚拟文件系统（sysfs）
├── tmp                           # 临时文件，系统定期清理
├── usr                           # 用户程序资源（Unix System Resources）
│   ├── bin
│   ├── sbin
│   ├── lib
│   ├── lib64
│   ├── share                     # 文档、man手册、图标
│   ├── include                   # C 头文件
│   └── local                     # 管理员自行编译安装的软件
└── var                           # 动态可变数据
    ├── log                       # 系统日志
    ├── cache                     # 软件缓存
    ├── spool                     # 打印、邮件队列
    └── tmp                       # 持久一点的临时文件
```

---

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

There is no general test suite. After kernel/boot changes do a QEMU smoke: `python3 tools/build.py --check`, a clean rebuild, then check `nm -u out/kernel.elf` is empty. The default image is OS_5P and contains `io287_*` and the BSD objects by design; to check the rule-9 clean core, build `make IO287=0` and confirm that artifact has no driver symbols (`ahci_*`, `acpi_init`, `serial_init`, `keyboard_*`, `mouse_*`, `pit_init`, `pci_*`). Full validation steps are in `AGENTS.md` under “验证”.

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

**Boot chain.** Rule 31 target: `firmware → Limine → OS_5P + kext loading + vga10h.kext → Orchestrd (PID1, launchd role) → compositor window service + Thresh (loginwindow role) → login → Voyage desktop`. Implemented today: `Limine → kernel.elf`, then the kernel starts `/bin/display.udrv` (KMS) and `/bin/orchestrd`, which owns the desktop service: it starts `/bin/aqua`, waits, and restarts it. The kext loader, Thresh (loginwindow) and Voyage (desktop shell) do not exist yet, and Aqua still spawns its clients; a direct aqua spawn remains as the fallback when orchestrd is absent. The ISO install entry bypasses the desktop entirely; the ISO's default Limine entry passes `cmdline: install`, and the kernel then starts `/bin/apps/install.zxe` instead of the live desktop (a setup disc, not a live ISO with a hidden installer); setup runs on an 80x25 framebuffer text console (`kernel/lib/fbcon.c`, VGA colours, scrolling) so the text UI is visible with no display service, the boot animation plays on setup media too, and kernel diagnostics are kept off that screen while the installer owns it; `kernel/boot/limine_boot.c` implements the Limine protocol, installs a 64 GiB identity map over Limine's HHDM page tables, maps the reserved kernel stack window, and translates Limine's memory map, framebuffer, RSDP, executable address and modules into `struct boot_info` (magic `0x5A4F5331`). Limine loads `casper/vga10h.kext`, `MiSans.ttf` and `WALL.RGB` as modules; the installed ESP carries `KERNEL.ELF`, `MiSans.ttf` and `WALL.RGB` next to `limine.conf` because Limine 12 has no ext4 driver. The old in-tree boot manager sources live in `archive/boot-legacy/` and are not part of the build chain. A boot animation paints the Limine framebuffer from early boot: `kernel/lib/fbdraw.c` provides the primitives and `kernel/lib/splash.c` draws the gradient, the rotating twelve-dot emblem, the stage label and the progress bar, advancing on the timer interrupt and stopping before Aqua's first present (`SPLASH_BOOT_TEST=1` animates a full bar, freezes the frame and halts for screenshots); setup discs show it too, then hand the screen to the framebuffer text console.

**OS_5P composition.** OS_5P is the hybrid kernel and `KERNEL.ELF` is its image. Yuanshuo owns CPU, memory, scheduling and IPC; IO/287 (C++, in-kernel, IOKit role) owns the device tree, properties and driver matching, with a PCI backend today; the BSD layer is the FreeBSD 15.1 implementation of **VFS, TCP/IP, cdev, pf and NFS**, statically linked into the same image (rule 51), in-kernel for cdev/pf/NFS/TCP and through the vnode layer for VFS. Until the personality's vnode layer runs, the core keeps a transitional provider behind `vfs_set_provider()` — the personality registers its own `struct zx_vfs_provider` through the service table (`core_abi.h` v4) and every path operation then routes to it. Self-written code in the BSD areas is limited to the transitional boot path and the IPC/descriptor plumbing that already exists (AF_UNIX sockets, zfd). Hardware drivers live in Ring3 `/bin/drivers.udrv` (Ada + C), sharing four physical pages (`kernel/include/dev.h`) for block I/O, RTC, input rings and kernel logs; `/bin/ext4fs` holds the trusted block capability. Rule 9 requires the Yuanshuo core's own binary to be driver-free and application-free; it does not forbid IO/287 or BSD drivers in OS_5P. Desktop and application code never go into `KERNEL.ELF`.

**Addressing and ABI.** Kernel VMA `0xffffffff80000000`, kernel stack top `0xffffffff87f00000` (a 128 MiB window: the FreeBSD personality's bss needs ~88 MiB), kernel heap `0xffffffff90000000`; physical memory is identity-mapped up to 64 GiB. User images load in `[1 TiB, 1 TiB + 1 GiB)`; user stack top is `0x10100000000` (32 pages). The initial stack is `argc`, `argv`, a default `envp` (`PATH=/bin:/usr/bin`, `HOME=/root`, `USER=root`, `SHELL=/bin/csh`, `TERM=zhaoxu`) and an auxv carrying `AT_PHDR/PHENT/PHNUM/PAGESZ/BASE/ENTRY/UID/EUID/GID/EGID/RANDOM/EXECFN`, so a C library can start without help; `kernel/proc/elf_loader.c` maps ET_EXEC segments, sets `brk` after the highest segment, and ZXE images ignore everything past `argv`. Syscalls are `int 0x80`, `rax` = number, `rdi/rsi/rdx/r10` = args; numbers live in `kernel/proc/syscall.c` and `user/userlib.h` and must stay identical (currently 0–112; `SYS_INSTALL_ERROR` is 68, `SYS_REBOOT` 69, `SYS_INSTALL_PROGRESS` 70, `SYS_INSTALL_CONFIG` 71, `SYS_FORK` 72, `SYS_EXEC` 73, `SYS_PIPE` 74, `SYS_DUP2` 75, `SYS_IOCTL` 76, `SYS_STAT` 77, `SYS_GETPPID` 78, `SYS_CONSOLE_COLOR` 79, `SYS_PTY` 80, `SYS_KLOG` 81, `SYS_MKDIR` 82, `SYS_RMDIR` 83, `SYS_RENAME` 84, then the glibc-facing block: `SYS_BRK` 85, `SYS_MPROTECT` 86, `SYS_MADVISE` 87, `SYS_ARCH_PRCTL` 88, `SYS_SET_TID_ADDRESS` 89, `SYS_GETTID` 90, `SYS_EXIT_GROUP` 91, `SYS_FUTEX` 92, `SYS_RT_SIGACTION` 93, `SYS_RT_SIGPROCMASK` 94, `SYS_RT_SIGRETURN` 95, `SYS_TGKILL` 96, `SYS_KILL` 97, `SYS_GETRANDOM` 98, `SYS_CLOCK_GETTIME` 99, `SYS_SET_ROBUST_LIST` 100, `SYS_PRLIMIT64` 101, `SYS_GETRLIMIT` 102, `SYS_UNAME` 103, `SYS_ACCESS` 104, `SYS_READLINK` 105, `SYS_LAST_ERROR` 112). Calls 0–85 keep the legacy `-1` failure convention with `SYS_LAST_ERROR` returning the recorded errno; 86 and up return `-errno` directly. `mmap` 54 serves memfd mappings unless `fd == -1` with `MAP_ANONYMOUS` (0x20), which selects the per-process anonymous allocator in `kernel/proc/anon.c` (48 GiB window at `1 TiB + 16 GiB`, 128 regions, 512 MiB per mapping). `SYS_KLOG` reads the 64 KiB kernel log ring (`kernel/lib/klog.c`) with the Linux klogctl types; `dmesg` uses it. `SYS_MKDIR`/`SYS_RMDIR`/`SYS_RENAME` route through `vfs_mkdir`/`vfs_rmdir`/`vfs_rename` to the rootfs copy-up layer (ramfs `wramfs_*`; a lower-only file is copied into the upper layer before a rename) or the registered BSD provider. `fork()` copies the caller's pages and descriptor table and resumes the child after the syscall with `rax=0`; `exec()` loads a new image into a fresh address space and rewrites the interrupt frame. `pipe()` is a 64 KiB ring object in `kernel/ipc/pipe.c` shared by two zfd descriptors; `dup2()` installs a descriptor at an exact zfd number or redirects fd 0/1/2. `kernel/proc/tty.c` gives fd 0/1/2 canonical/raw termios, echo, window size 80x25 and FIONREAD; `ioctl(TCGETS/TCSETS/TIOCGWINSZ)` mirrors `kernel/include/tty.h` in `user/userlib.h`. newlib 4.6 is the C library: `tools/newlib.py` builds a PIC x86_64-elf libc with clang, and `user/lib/posix/libgloss.c` plus `crt0.S` implement the syscall stubs and `main(argc, argv, envp)` startup. `NLIBC_BOOT_TEST=1` runs a printf/malloc program and an ncurses screen test. ncurses 6.5 is built out of tree against newlib with `user/lib/posix/include` supplying dirent/glob/termios/ioctl/resource headers; the tty terminfo entry is `initramfs/usr/share/terminfo/z/zhaoxu`. nano is not compiled yet: gnulib's portability headers still conflict with newlib's signal surface.

**New IPC layer (M0).** `kernel/ipc/zfd.c` is a per-process descriptor table starting at 64 (`ZFD_BASE`), separate from legacy VFS descriptors 3–18. `unix_socket.c` implements AF_UNIX stream endpoints (`socket/bind/listen/connect/accept/socketpair`, 64 KiB receive rings, blocking via 1 ms scheduler sleeps). `memfd.c` provides shared-memory files with `mmap/munmap` at `1 TiB + 2 GiB`. `proc/io_syscall.c` carries Linux-compatible `sockaddr_un`/`iovec`/`msghdr`/`cmsghdr` layouts, `SCM_RIGHTS` (memfd only), `poll`, and `fcntl(O_NONBLOCK)`. `SCM_RIGHTS`-transferred descriptors are refcounted, not copied. `eventfd.c`, `timerfd.c`, and `epoll.c` add the event-loop objects behind syscalls 61–67; epoll registrations are level-triggered and capped at 32 per object. Named-port control messages are capped at `IPC_MSG_MAX` (256 bytes) and the ADE desktop protocol carries up to `ADE_TEXT_MAX` (160 bytes, including the terminator) per text command; `ade_text` truncates on a UTF-8 boundary.

**Wayland/GBM/KMS direction.** The mandatory target architecture is documented in `docs/superpowers/specs/2026-11-14-wayland-gbm-kms-design.md`: Wayland 1.x core + xdg-shell + wl_shm/dmabuf, buffers allocated through GBM, GPU render/compose, then KMS atomic commit and page flip. M0 (transport) is implemented and verified; M1 is the Ring3 `/bin/display` KMS service (virtio-gpu 2D resource/backing/set_scanout/flush, double-buffered flip) plus GBM. The kernel now provides epoll, eventfd, and timerfd. `src/third_party/wayland` vendors libwayland 1.23.1's client core plus generated core/xdg-shell bindings; `src/third_party/libffi` carries the x86_64 SysV `ffi_call` subset; `src/user/lib/wayland/compat.c` supplies libc, errno, pthread and socket entry points, and `wayland-os-zos.c` replaces the Linux descriptor-flag helpers. `make out/wlstock.zxe` builds `/data/WLSTOCK.ZXE`; `WLSTOCK_BOOT_TEST=1` starts `/bin/wayland` before it and the serial log must end with `[wlstock] WLSTOCK exit 0` and a server-side `commit 320x240` line. Unmodified stock applications (`weston-terminal`) still need the remaining libc/`shm_open` surface and application migration; dma-buf remains open with M4.

**Desktop stack.** `/bin/aqua` is the Ring3 display server. `src/apps/ADE/main.cpp` is entry/registration; `wallpaper.cpp`, `menubar.cpp`, `dock.cpp` own chrome; `winmgr/dwm.cpp` owns z-order, focus, dragging, close, input, damage; `compositor/` owns the system-RAM back buffer, damage-only present, and cursor. Clients use `user/lib/include/ade_protocol.h` + `ade_client.h`; commands are replayed through `kernel/gfx/render.c` (Z64 1.0), which is shared with the kernel. Cursor themes are PNG frames plus a text `cursor.theme` descriptor under `/usr/share/cursors/Aqua`; `compositor/cursor.c` parses it and `cursor_stb.c` decodes with the vendored `stb_image.h`.

**Installer and graphics APIs.** `/bin/apps/install.zxe` is a text-mode setup program in the Windows XP i386 tradition: it reads keys from the tty in raw mode and walks welcome, licence, disk selection, GPT/MBR and data-partition choice, format warning, progress and restart. It runs on the kernel console without the compositor. It spawns a `worker` copy of itself; the worker blocks in `SYS_INSTALL_PROGRESS` while the parent reads `done/total` from a shared-memory page and prints progress. The custom-partition screen sends ESP/data sizes through `SYS_INSTALL_CONFIG`; the kernel writes the optional third FAT32 `ZHAOXUDATA` partition and formats it. `SYS_INSTALL` writes the MBR/GPT, Limine's BIOS boot sector/stage 2, the ESP files (`EFI/BOOT/BOOTX64.EFI`, `EFI/BOOT/limine.conf`, `boot/limine/limine-bios.sys`, plus `KERNEL.ELF`, `MiSans.ttf` and `WALL.RGB` under `/boot/limine` on the ESP) and formats the ext4 `ZHAOXUROOT` volume; the ext4 root holds no boot files because Limine 12 reads FAT and ISO9660 only. Failures arrive through `SYS_INSTALL_ERROR`, and `SYS_REBOOT` resets the machine from the done screen. `src/user/lib/opengl` is a software OpenGL 1.1 immediate-mode implementation (matrix stacks, ortho/frustum, primitives, depth, culling, RGBA textures, `GL_LIGHT0..7` lighting/materials, blending, alpha test, scissor, `glReadPixels`), built with XMM and covered by `GL_BOOT_TEST=1`. `src/user/lib/vulkan` is a software Vulkan 1.0 ICD with a SPIR-V interpreter and CPU rasterizer, covered by `VK_BOOT_TEST=1`; it has no WSI, descriptors/UBOs, compute or depth, and the desktop still uses the ZOS raster renderer, so rules 37/38 are not met.

**Storage and install.** `/` is the initramfs-backed rootfs (initramfs lower + ramfs copy-up); `/mnt` is the FAT32 `ZHAOXUDATA` volume or, without a data disk, the embedded `/data` mirror. The initramfs embedded in `kernel.elf` is a ZIR1-wrapped zlib stream (`tools/initramfs-compress.py`); `kernel/fs/initramfs.c` inflates it before mounting rootfs, which keeps the image small enough for the legacy-BIOS stage 2 buffer. Installation is self-contained in `kernel/fs/install.c` (single ISO, no extra media): it writes MBR or GPT, formats a FAT32 ESP (`ZHAOXUEFI`) and `ZHAOXUDATA`, and copies the UEFI loader plus a synthesized single-segment kernel ELF.

**User executables.** ZXE v1 is a PIE linked at RVA 0 (`-fpie`/`-fPIC`, `user/user.ld`), packed by `tools/zxe-pack.py`, loaded at a random 64 KiB-aligned base inside `[1 TiB, +1 GiB)` with W^X enforced (CODE=RX, RODATA=R, DATA/ZERO=RW). Dependencies load from `/lib` by plain filename. Kernel and user code are freestanding: no SSE/red zone except the Ring3 font sources; the Yuanshuo core is C and IO/287 is freestanding C++ without exceptions, RTTI, STL, static guards or a C++ runtime.

**Fatal screen.** A kernel exception paints the Limine framebuffer red and renders the `KERNEL PANIC` banner from `ascii.md` with the public-domain 8x8 font (`kernel/lib/panic.c`), then halts; the kernel console has no framebuffer sink before the Ring3 display service runs, so the panic path draws directly and also writes one report line to COM1. `PANIC_BOOT_TEST=1` boots into that screen; `tools/screenshot.py` captures it (the red screen is verified).

**Fedora 45 ports.** Owner directive 2026-11-26: btrfs comes from Fedora 45 and glibc, gcc, gdb, binutils, llvm, lua and python are ported from the same tree. `tools/fedora-fetch.py` reads the release's real `repodata`, downloads each source RPM and verifies its published SHA-256 before use; the provenance ledger is `third_party/fedora/45/SOURCES.md`, the plan is `docs/superpowers/plans/2026-11-26-fedora45-toolchain.md`. btrfs-progs provides `mkfs.btrfs`/`btrfs`/`fsck.btrfs`; the kernel filesystem is ported from Fedora's `fs/btrfs` behind the rule-51 VFS provider so the on-disk format matches Fedora's tools. Porting order: the glibc-facing kernel syscall surface, binutils, glibc (`sysdeps/x86_64/zhaoxu`), gcc, gdb, llvm, lua, python.

## Repository rules that change how you work

- All repository scripts must be Python, sh or ps1 (rule 6); code must be generated by AI, never hand-written.
- One file does one job. Do not create catch-all files such as a single `shell.cpp`.
- The Yuanshuo core binary stays driver-free and app-free; OS_5P's IO/287 and BSD layer are part of `KERNEL.ELF`. Desktop/application code never goes into the kernel.
- `version.cfg` is the only place to edit versions (`os_version`, `kernel_version`, and the `YYYY-MM-DD` `build_date`); run `make version` in `src/` afterward. Update `build_date` for every routine build/revision; major = epic change, minor = 10+ features, patch = routine.
- `README.md` holds only the latest release; all history goes in `CHANGELOG.md`. README must keep Emoji/HTML.
- Sources of truth: `AGENTS.md` for rules and invariants, `docs/superpowers/` for designs, `version.cfg` for versions.
- Push priority: GitHub is the primary remote; GitCode is synced only for releases; Gitee is ignored (do not commit or push). Branches: GitHub `main`, GitCode `main`, Gitee `master`. Pushing from WSL fails without credentials; use Windows Git for `git push github main` and only `git push gitcode main` on release.
