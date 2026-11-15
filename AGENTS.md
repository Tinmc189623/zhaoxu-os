# Zhaoxu OS™（内核 Yuanshuo™）Agent 指南

## 规则（必须遵守）

1. 内核名称：`Yuanshuo™`，操作系统名称：`Zhaoxu OS™`，桌面合成器：`Desktop Window Compositor™`（Wayland），Shell：`8sh`，桌面：`Aqua Desktop Environment™`（风格：Aqua），窗口管理器：`Desktop Window Manager`，不得改动任何字符，严禁使用`Yuanshuo™ Shell`。
2. Github 仓库（main）：https://github.com/Tinmc189623/zhaoxu-os
3. 必须深度自定义启动菜单。
4. Limine 必须使用原样，并完全使用它的功能。 
5. 使用 WSL2，可用：C（内核）、C++（应用）、GAS（C/C++ 内的汇编）、FASM、SPARK（驱动）。
6. 脚本必须完全使用 Ruby 来编写，必须完全使用 AI 来生成代码，**禁止人类编写代码**。
7. 引导加载器必须支持 UEFI 和 Legacy BIOS 两种启动模式。 
8. 引导管理器必须得是：`Limine`。
9. 内核必须得是微内核，用户程序必须得放在 Ring3，内核的二进制必须得是干净的二进制，不允许往里投放什么驱动/应用。
10. 该操作系统必须遵循 Unix 哲学和使用 Unix 方式。 
11. iso命名示例：`zhaoxu-os_x86-64_{version}_{build}_ld.iso`。
12. 不得将所有功能挤在一个文件里，如 `apps/shell.cpp`。
13. README.md 里只允许编写近一次版本更新，而不能编写所有版本，所有版本应放入 CHANGELOG.md 文件中。
14. README.md 里必须拥有 Emoji 和 HTML 以增加视觉效果。
15. 系统必须同时支持 MBR 和 GPT。
16. **AI** 写代码时优先追求效果和稳定性，不要为了快而在那块儿编写1s的代码，却看不着任何效果。
17. 字体（必须 OpenType）：MiSans、MiSans Global、Inter。
18. 字体渲染引擎必须移植使用 FreeType。
19. 写代码的时候先看 stb 里头有没有需要的，如果有需要就直接复用，别自己自研。
20. 项目必须遵循 KISS 原则。
21. 必须支持的文件系统：FAT32、ext4，且为完整支持。
22. 应用图标必须得是 SVG 图标。
23. 一个文件只允许做一个功能，不允许把所有东西塞到一个文件中。
24. 系统 API：`Zhaoxu Operating System Application Programming Interface Version 1.0`
25. 不得将内核写入 `BOOTX64.EFI`。
26. 内核：`KERNEL.ELF`。
27. 用户在安装系统时，只需要一个 ISO 即可安装，不需要让用户插入其他磁盘等需要外面软盘。
28. ESP 分区必须得是 FAT32。
29. 一个文件只能干一件事，一个文件不能干出超过一件事的事情，错误示例：`src/apps/cwsl/shell.cpp`。
30. 禁止使用 busybox，使用 toybox。
31. 引导流程：`固件（UEFI 或 Legacy BIOS）→ Limine 引导管理器 → KERNEL.ELF → vga.ko 通用显卡驱动`。
32. zhaoxuOS 的 GUI 采用经典 Aqua（Classic Aqua）视觉风格。计划构建独立的 Zhaoxu Aqua Framework（ZAF），以 Skia 为图形渲染基础，自研窗口管理器、桌面合成器、控件系统、动画引擎和主题引擎。要求具备深度自定义能力、可移植到自研操作系统，并尽量减少对其他操作系统 GUI 组件的依赖。Aqua 为默认主题，底层框架支持未来扩展其他视觉主题。
33. 系统默认壁纸必须得是 `src\apps\ADE\web\wallpaper\3pty\img0.jpg`。
34. 应用本身一般**不直接往屏幕上画**，它会分配一块显存缓冲区（帧缓冲 / 纹理 buffer），调用图形 API（Vulkan），在这块缓冲区里把这个图形渲染出来。
 - 应用里面发起绘图请求：我要画这个图形，设置位置、颜色、大小。
 - 渲染完成之后，这块显存里就存好了这个图形对应的像素点阵。
 - 应用发送消息给显示服务端：我的窗口新画面准备好了，这是纹理的句柄 / 缓冲区，还有窗口在桌面里的位置、层级信息。
 - 显示服务里的 WM 模块，检查这个窗口：焦点、窗口坐标、Z 序（前后层级）、裁剪区域。
 - 合成器收集所有窗口、桌面背景、光标等全部图层纹理，按照 Z 序一层层叠加。在这里做透明、抗锯齿等效果，把所有图层合并成**一整张最终的屏幕画面纹理**，交给 GPU 排队输出。
 - GPU 把合成好的帧送入显示流水线，等待显示器的垂直同步信号，避免撕裂。
 - 显示控制器从显存里按行读取像素数据，转换成显示器识别的信号（DP / HDMI /eDP 信号）。
 - 面板收到像素数据，驱动每一个子像素，就在屏幕上看到这个图形了。
35. **版权声明：**
 - English: `Copyright (C) <YEAR> <NAME>. All Rights Reserved.`
 - 中文：`©<年> <名> 版权所有`
36. 必须支持所有unix自带的命令。 
37. 必须完整移植并使用 Vulkan 作为默认图形渲染 API。
38. OpenGL 仅作为兼容处理，但必须完整移植。
39. 第三方必须按照“原样”/裁剪提供，不得进行自我更改，导致以后更新不了。
40. **色彩渲染支持规范：**
- 10bit：面向支持HDR的民用显示器，进入Wayland桌面合成器后，硬件达标则启用该色深。
- 8bit：面向常规SDR显示器，Limine开机菜单与它提供的帧缓冲默认使用此规格；**8bit SDR屏幕同样正常运行Wayland合成器，合成器只是不启用10bit色深能力**。
41. AI **可**改动本文件。


## ISO（这是**ISO 介质本身**的目录，不是启动后内存里的根文件系统；live 系统本体在 `casper/filesystem.squashfs` 压缩包内，版本：v1.14.1）

```iso
iso-root/
├── boot/
│   └── limine/
│       ├── limine-bios.sys       # Limine BIOS stage 3
│       ├── limine-bios-cd.bin    # BIOS El Torito 入口
│       ├── limine-uefi-cd.bin    # UEFI El Torito 入口
│       ├── limine.conf           # Limine 主配置（BIOS 查找副本）
│       ├── MiSans.ttf            # 作为 Limine 模块传给内核
│       └── WALL.RGB              # 默认壁纸原始帧模块
├── EFI/
│   └── BOOT/
│       ├── BOOTX64.EFI           # Limine UEFI 引导管理程序
│       └── limine.conf           # Limine 主配置（UEFI 同目录副本）
├── casper/                       # Live 核心目录，最重要
│   ├── filesystem.squashfs       # 主 rootfs 压缩镜像（live 系统本体）
│   ├── initrd.img                # 初始化内存盘
│   ├── kernel.elf                # Yuanshuo 内核（Limine executable）
│   ├── kernel.bin                # 扁平内核镜像（Limine KERNEL.BIN 模块）
│   ├── manifest
│   ├── vga.ko                    # 通用显卡驱动
│   ├── README.diskdefines
│   └── uuid
├── install/                      # 兼容保留的安装介质副本
│   ├── KERNEL.ELF
│   ├── KERNEL.BIN
│   ├── MiSans.ttf
│   └── Inter.otf
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
├── README.html
├── autorun.inf
└── md5sum.txt
```

## 构建与运行

- 构建必须支持四种宿主：原生 Linux、原生 macOS、原生 Windows（MSYS2/MinGW bash）与 WSL2。工作目录必须是仓库内的 `src/`。**构建系统是 Makefile**；Ruby 只负责辅助脚本（`tools/zxe-pack.rb`、`tools/version.rb`、`tools/iso-*.rb`、smoke/screenshot）和可选的宿主探测（`ruby tools/build.rb --check`）。Windows 首选 WSL2，在其内部直接 `make`；`tools/build.rb` 只是便捷包装，不再是构建入口。
  ```bash
  cd src
  make -j4                             # Linux/macOS/WSL 构建全部
  make iso                             # UEFI + legacy BIOS 双启动 ISO
  make run-iso                         # UEFI 光盘启动
  make host-test                       # 纯 Ruby 宿主检测单元测试
  ruby tools/build.rb --check          # 辅助：base/kernel/ada/iso/run 工具检查
  ```
  - **Linux / WSL**：`gcc`、`g++`、带 Ada 支持的 gcc（`gcc-14`+`gnat-14` 或 `gcc-16`+`gnat-16`；Makefile 会检查 `-print-prog-name=gnat1` 是否真实存在，按 `gcc-14/gcc-15/gcc-16` 顺序选择，可用 `ADA=` 覆盖）、`ld.lld`、`objcopy`、`clang`、`lld-link`、`qemu-system-x86_64`、OVMF、`mkfs.vfat`、`mtools`、`tar`、`xorriso`、`squashfs-tools`、`ffmpeg`（壁纸转 raw，可用已有 `build/ade/wallpaper.rgb` 缓存跳过）。
  - **macOS**：Homebrew `llvm`（clang、llvm-objcopy）、`lld`（ld.lld、lld-link）、`qemu`、`xorriso`、`mtools`、`dosfstools`、`squashfs`；Ada 驱动需要 x86_64-elf GNAT（用 `ADA=` 指向）。
  - **原生 Windows**：Ruby + MSYS2 的 `make`、`mingw-w64-x86_64-clang`、`-lld`、`-llvm`、`dosfstools`、`mtools`、`xorriso`、`squashfs-tools`、`qemu`（Limine 构建不再需要 FASM）；Ada 驱动需要 x86_64-elf GNAT。**Windows 上优先使用 WSL2**：自动检测先探测 WSL 的 `make/cc/clang/ld/objcopy`，齐全时走 WSL；不齐全才回退原生 Windows，也可用 `--host wsl` / `--host native` 强制。
  - **Windows 入口**：`./src/build.ps1`、`./src/run.ps1`（PowerShell 7+）优先转发给 Ruby，`auto` 模式随 `tools/build.rb` 优先选择 WSL2；Ruby 缺失时才退回旧的 WSL 转发路径。
- 单个用户程序：`make out/hello.zxe`（内部先链接 `build/hello.uelf`，再由 `tools/zxe-pack.rb` 转成 `.zxe`）。模式规则会自动编译 `user/hello.c` 并链接 `user/userlib.c`，不需要改 Makefile。程序入口是 `int user_main(long *stack)`，不是 `main`；`userlib.c` 的 `_start` 会把返回值作为退出码。ELF→.zxe 转换需要 Ruby：WSL 内没有时通过 Makefile 的 `RUBY` 变量指向 Windows 侧 ruby.exe（按本机位置覆盖，不要写死盘符）。ZXE v1 结构自检：`ruby tools/zxe-check.rb out/*.zxe`；动态链接冒烟用 `ruby tools/zxe-fixture-lib.rb out/libztest.zxe` 生成共享库，配合 `user/ztestmain.c`（导入 `ztest_value`）。
- `make`/`make all` 的最终产物是 `out/kernel.elf` 与宿主侧 `build/limine-host`；`make out/data.img` 只是手动生成一个 FAT32 数据盘镜像，测试一律走 ISO。
- `make run-iso` 默认 q35、2 GiB、smp=4、`-display none` + 串口 stdio，只把 ISO 挂为 `ide.0` 光驱，不挂数据盘/安装盘；PMM 会管理完整内存映射（上限 64 GiB，已在 8 GiB 下验证），需要更大内存时用 `QEMU_MEM=4G`。`make run-install` 额外挂一个 256 MiB 空白 `out/target.img`（首次创建）作为 `ide.1`。`make run` 现在就是 `run-iso` 的别名。带 `-no-reboot`，所以来宾发起重启会直接退出 QEMU。单核回归用 `make run-iso QEMU_SMP=1`。
- `make iso` 生成 `out/zhaoxu-os_x86-64_{version}_{build}_ld.iso`：用 Limine 原样提供的 `limine-bios-cd.bin`、`limine-uefi-cd.bin` 作为双 El Torito 入口（都是 no-emulation），`build/limine-host bios-install` 写入 BIOS 部署参数。光盘自带 `limine.conf` 两份（`EFI/BOOT/` 与 `boot/limine/`）、`limine-bios.sys`、`casper/kernel.elf` 与模块 `casper/kernel.bin`、`boot/limine/MiSans.ttf`、`boot/limine/WALL.RGB`；`install/` 保留 `KERNEL.ELF`、`KERNEL.BIN`、`MiSans.ttf`、`Inter.otf` 作为兼容副本。内嵌 initramfs 由 `tools/initramfs-compress.rb` 压成 ZIR1+zlib，内核在挂载 rootfs 前解压。`casper/vga.ko` 当前是 Ring3 通用驱动镜像（`out/drivers.zxe`，含 virtio-gpu），内核仍从 initramfs 启动 `/bin/drivers`，模块加载器尚未实现。字体只允许 MiSans、MiSans Global、Inter。
- legacy BIOS 链：SeaBIOS 加载 Limine 的 BIOS 引导扇区（ISO 上是 `limine-bios-cd.bin`，已安装盘上是安装器写入的 MBR 引导扇区）→ stage 1 从 LBA 8（MBR）或 LBA 34（GPT BIOS boot）加载 stage 2 → stage 2 通过自己的 FAT/ISO9660 驱动读取 `limine.conf` 与 `limine-bios.sys` → 进入 Limine 协议，与 UEFI 路径共用 `kernel/boot/limine_boot.c`。UEFI 链由 `EFI/BOOT/BOOTX64.EFI` 直接进入同一套协议。
- OVMF VARS 不能用只读的系统文件；`make run` 会先复制到可写的 `out/OVMF_VARS.fd`。OVMF 路径可用 `OVMF_CODE`/`OVMF_VARS` 覆盖。
- `HEADLESS=1` 不再改变引导层（Limine 原样使用）；它只让内嵌内核在安装时提示“装出的系统没有 Aqua 桌面”。`build.ps1 -Headless` 就是设这个值。
- 依赖：各宿主所需工具见 `ruby tools/build.rb --check`。Limine 的二进制与宿主部署工具已 vendored 在 `src/third_party/limine`，构建不再需要 FASM；`src/tools/fasm` 只是保留的历史工具。
- 入口脚本：`build.ps1`/`run.ps1` 是 Windows 薄转发层，实际逻辑在 `tools/build.rb`、`tools/run.rb`、`tools/host.rb`（全部 Ruby，规则 6）；`tools/install-fasm.sh` 已不存在，Limine 构建也不再需要宿主 FASM。
- 原生 Windows 与 WSL 共用 `build/` 和 `out/`。Windows 编译器生成的 `.d` 依赖文件使用反斜杠路径，WSL 的 GNU make 会把它当成字面文件名；`tools/build.rb` 在走 WSL 前会把反斜杠改写成 `/`，保留头文件依赖（否则 `version.h` 变更后二进制版本号不会更新）。切换编译器后仍建议 `make clean`，避免混用 clang/gcc 目标文件。

## 验证

没有自动化测试；改动后至少做一次 QEMU 冒烟：

1. `ruby tools/build.rb --check` 应列出当前宿主的工具与 OVMF；`ruby tests/tools/host_test.rb` 应输出 `PASS`。然后 `make clean && make -j4` 干净重建，`nm -u out/kernel.elf` 应无未定义符号；`nm out/kernel.elf` 不应出现 `ahci_*`/`acpi_init`/`serial_init`/`keyboard_*`/`mouse_*`/`pit_init`/`pci_*` 等驱动符号。
2. `make run-iso` 后应直接进入 Aqua Desktop Environment：日志出现 `limine: Loading executable`、`[ade ] font boot volume: ...`、`[ade ] wallpaper boot volume: ...` 或 `[dwc] Desktop Window Compositor back buffer`、`[aqua] Ring3 display server`，随后 `spawn /bin/apps/welcome.zxe` 与 `/bin/apps/calc.zxe` 并显示两个客户端窗口；点击欢迎窗口应回显 `已收到点击事件`，计算器按 `7 + 5 =` 应显示 `12`，关闭计算器后 Dock 回收对应图标。内核不再链接任何桌面或应用代码（规则 9）。无帧缓冲时内核启动 Ring3 `/bin/8sh`，应能执行 `help`、`ls /mnt`、`cat /mnt/HELLO.TXT`、`run /data/HELLO.ZXE`（退出码 7）、`write /mnt/NEW.TXT hi`、`cat /mnt/NEW.TXT`、`rm /mnt/NEW.TXT`、`disks`、`install`、`uptime`、`cpus`、`exit`。
3. 用户态异常只应终止进程并回到 Shell（退出码 `-向量号`，如 `#PF` 为 `-14`）；内核异常才 panic。
4. ISO 改动：`make iso` 生成 `out/zhaoxu-os_x86-64_{version}_{build}_ld.iso`（规则 11 命名），分别用 `make run-iso`（UEFI）与 `make run-bios`（SeaBIOS）从 ISO 启动；两者都应经 Limine 协议进入内核并打印字体、壁纸模块来自 `/boot/limine`，中文正常显示，且不挂任何数据盘。
5. 安装回归：`make run-install`（GPT）与 `make run-install-bios`（MBR）各挂一个空白 `out/target.img`，在安装窗口点选对应按钮，日志应有 `[install] complete`；随后只用该目标盘启动（UEFI 或 SeaBIOS），Limine 从 ESP 读取 `KERNEL.ELF` 与模块，日志应出现 `limine: Loading executable`、`[ade ] font boot volume: ...`、`[ade ] wallpaper boot volume: ...`，桌面中文与壁纸正常。UEFI 的 ESP 必须含 `EFI/BOOT/BOOTX64.EFI`、`EFI/BOOT/limine.conf`、`boot/limine/limine-bios.sys`、`boot/limine/limine.conf`，以及根部的 `KERNEL.ELF`、`KERNEL.BIN`、`MiSans.ttf`、`WALL.RGB`；Limine 12 没有 ext4 驱动，缺任何一个都会停在 `Failed to open executable`。安装失败时 `SYS_INSTALL_ERROR`（68）返回内核给出的具体原因。安装 UI 把磁盘写入交给 `worker` 子进程，父进程通过共享内存读取 `SYS_INSTALL_PROGRESS`（70）的 done/total 并重绘进度条；完成后可点“立即重启”，走 `SYS_REBOOT`（69）的键盘控制器复位（失败再三重故障）。自定义分区走 `SYS_INSTALL_CONFIG`（71）：ESP 64/128/256 MiB，可选 FAT32 `ZHAOXUDATA` 数据分区 64/128/256 MiB，ext4 系统分区取剩余空间，UI 最少留 64 MiB（内核格式化下限是 32 MiB；目标盘识别门槛按 2048 + 70000 + 65536 扇区，约 68 MiB）。数据分区写入 MBR/GPT 第三项并格式化、校验标签。`blockfs.c` 扫描分区时必须用磁盘总扇区校验起点，不能拿分区自身长度比较，否则靠后的 GPT 分区永远不会被 `/media` 探测到。
6. SMP/调度器回归：`make run-iso QEMU_SMP=4 SCHED_STATS=1` 应打印（默认 `SCHED_STATS=0`，不输出周期统计，避免刷屏） `4/4 CPU(s) online` 与三行 `[sched] ... churn=12/12`；各 AP 的 worker 计数并行增长，`threads` 为 9、`heap` 三次采样一致（证明线程栈已回收）。再用 `QEMU_SMP=1` 验证单核回退，Aqua Desktop Environment 与 Dock 应用仍正常。UEFI 与 BIOS 两条 Limine 路径的 LAPIC timer 校准值都应在数千万 Hz 量级。
7. RSDP 由 Limine 通过 `RSDP` 请求提供，`kernel/boot/limine_boot.c` 把它转成物理地址写入 `boot_info`；内核 `apic_locate_rsdp` 仍保留低端 1 MiB 兜底扫描，找不到时退回单 CPU，仍用 LAPIC 计时。
8. 帧缓冲与桌面：`make run-iso QEMU_DISPLAY="-display gtk"` 人工查看；无头回归用 QEMU HMP `screendump /tmp/ade.ppm`（QEMU 输出 PPM，可用脚本转 PNG），检查菜单栏、窗口层叠、Dock 与 SVG 图标。`ruby src/tools/smoke.rb` 的 Shell 命令流程仅在无帧缓冲回退 8sh 时有效。
9. M0 传输回归：`ruby tools/build.rb M0_BOOT_TEST=1 out/kernel.elf -o out/initramfs.tar` 构建自测内核（initramfs 需含 `/data/NET_TEST.ZXE`），用 OVMF 从 FAT ESP 引导后，串口应出现 `[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS` 和 `[m0 ] NET_TEST exit 0`。该测试覆盖 socketpair、poll、memfd、mmap、SCM_RIGHTS 与 O_NONBLOCK。
10. M5 事件循环回归：ISO 启动后在 8sh 运行 `/data/M5_TEST.ZXE`，串口应出现 `[m5] eventfd PASS`、`[m5] epoll PASS`、`[m5] timerfd PASS` 与 `[m5] eventfd, timerfd, epoll PASS`。该测试覆盖系统调用 61–67：eventfd 计数/非阻塞读写、timerfd 一次性到期、epoll 注册/等待/超时。
11. stock libwayland 回归：`make WLSTOCK_BOOT_TEST=1 iso` 启动后串口应出现 `[wlstock] ffi 42`、`[wlstock] globals bound`、`[wayland] commit 320x240 stride=1280 ...` 与 `[wlstock] WLSTOCK exit 0`。`out/wlstock.zxe` 由 `third_party/wayland`（1.23.1 客户端核心与生成的核心/xdg-shell 协议）、`third_party/libffi`（3.4.6 x86_64 SysV `ffi_call` 子集）和 `user/lib/wayland` 兼容层构建；编译 stock 源码时不要把 `kernel/include` 加进头文件路径，否则 `poll.h` 会遮蔽 libc 的同名头。
12. FreeType 回归：`make out/ft_test.zxe out/data.img` 后以 `FT_BOOT_TEST=1 iso` 启动，并把 `out/data.img` 挂为第二块 IDE 盘；串口应出现 `[ft] face MiSans`、`[ft] glyph 26x31 covered 334`、`[ft] FreeType PASS` 与 `[ft ] FT_TEST exit 0`。字体只允许 MiSans、MiSans Global、Inter；ISO 上字体是 Limine 模块 `/boot/limine/MiSans.ttf`，安装后 ESP 根部是 `/MiSans.ttf`。
13. 规则 37/38（Vulkan 默认、OpenGL 兼容）尚未完全满足：当前桌面仍走 `kernel/gfx/render.c` 软件路径。`src/user/lib/opengl` 提供 OpenGL 1.1 立即模式软件实现（矩阵栈、正交/透视、glBegin/glEnd、纹理、光照、深度、混合、Alpha 测试、裁剪、背面剔除、glReadPixels），但仍缺显示列表、求值器、雾、选择/反馈、扩展与着色器。`src/user/lib/vulkan` 提供单 ICD 的 Vulkan 1.0 软件实现：实例/设备/队列、内存/缓冲区/图像、渲染通道/帧缓冲、图形管线、命令缓冲、栅栏、SPIR-V 1.0 解释器和 CPU 三角光栅化；没有 WSI/交换链、描述符集/UBO、计算、深度/模板、多重采样、信号量。发布说明必须如实描述这些限制，不得声称规则 37/38 已完全满足。
14. OpenGL 回归：`make out/gl_test.zxe out/data.img` 后以 `GL_BOOT_TEST=1 iso` 启动并把 `out/data.img` 挂为第二块盘；串口应依次出现 `[gl ] quad PASS`、`gouraud PASS`、`depth PASS`、`cull PASS`、`matrix PASS`、`perspective PASS`、`texture PASS`、`lighting PASS`、`blend PASS`、`scissor PASS`、`readpixels PASS`、`[gl ] OpenGL PASS` 与 `[gl ] GL_TEST exit 0`。GL 源码需要 XMM，编译时从 `USER_CFLAGS` 过滤 `-mno-sse` 并加 `-msse2`。
15. Vulkan 回归：`make out/vk_test.zxe out/data.img` 后以 `VK_BOOT_TEST=1 iso` 启动并把 `out/data.img` 挂为第二块盘；串口应出现 `[vk ] instance, physical device, queue PASS`、`[vk ] triangle PASS`、`[vk ] Vulkan PASS` 与 `[vk ] VK_TEST exit 0`。测试用真实 `vkCreateInstance`/`vkCreateDevice`/渲染通道/图形管线/命令缓冲渲染 64x64 RGBA 图像并回读，SPIR-V 由 `tools/spirv-test-gen.rb` 生成。
16. fork/exec 回归：`make FE_BOOT_TEST=1 iso` 启动后串口应出现 `[fe ] running /data/FORKEXEC.ZXE`、`[forkexec] parent pid`、`[forkexec] child pid`、`[forkexec] wait returned 7, child status 7`，随后 `exec` 进入 `HELLO.ZXE`，打印 `[hello] ... argc=1, argv[0]=hello-from-exec`，最后 `[fe ] FORKEXEC exit 7`。子进程的 fork 返回值必须是 0，父进程拿到子 pid；exec 成功后同一 pid 运行新镜像。
17. pipe/dup2 回归：`make POSIX_BOOT_TEST=1 iso` 启动后串口应出现 `[posix] pipe PASS`、`[posix] pipe eof PASS`、`[posix] dup2 PASS`、`[posix] PIPE, EOF, DUP2 PASS` 与 `[posix] POSIX_TEST exit 0`。测试覆盖 fork 后跨进程管道、写端关闭后的 EOF、以及 dup2 到 zfd 编号 90。

## 架构与不变量

- 单镜像：引导层统一使用 `out/kernel.elf`。Limine 12.10.0 原样充当引导管理器（规则 4/8/12）：UEFI 用 `EFI/BOOT/BOOTX64.EFI`，legacy BIOS 用安装器写入的引导扇区/stage 2（GPT 放在 LBA 34 的 BIOS boot 分区，MBR 放在 LBA 8 之后的空隙）；两条路径都通过 Limine 协议进入 `kernel/boot/limine_boot.c`。ISO 上内核是 `casper/kernel.elf`，`casper/kernel.bin`、`MiSans.ttf`、`WALL.RGB` 作为 Limine 模块；已安装盘上 Limine 只读 FAT，所以 `KERNEL.ELF`、`KERNEL.BIN`、`MiSans.ttf`、`WALL.RGB` 全部放在 ESP 根，ext4 `ZHAOXUROOT` 不承载引导文件。内核二进制只含 CPU/内存/调度/IPC/VFS 核心，不含任何驱动或应用；AHCI、PS/2、鼠标、RTC、串口由 Ring3 `/bin/drivers` 独占（规则 9）。`limine_boot.c` 在 Limine 的 HHDM 页表上安装 64 GiB 恒等映射、映射 BSS 引导栈，再把 memmap、帧缓冲、RSDP、可执行地址与模块转成 `boot_info`。
- `struct boot_info` 的字段顺序必须与 `src/kernel/include/boot.h` 和 `kernel/boot/limine_boot.c` 同步，magic 为 `0x5A4F5331`。`build_memory_map` 把 Limine memmap 转成 40 字节 UEFI 风格描述符（`LIMINE_MEMMAP_USABLE`→7，其余→2），模块与帧缓冲地址经 HHDM 换成物理地址。内核 `.bss` 由 Limine 的 ELF 加载器按 `p_memsz` 清零，内核不得依赖引导层未定义的行为。
- 地址布局：内核 VMA `0xffffffff80000000`，内核栈顶 `0xffffffff80f00000`，内核堆 `0xffffffff90000000`；低端 `0..64 GiB` 是物理内存恒等映射（`limine_boot.c` 在切换页表前建好，`vmm_init` 之后继续维护），用户程序基址 `0x10000000000`（1 TiB，必须避开恒等映射区），用户栈顶 `0x10100000000`（32 页）。内核栈顶在 `kernel/boot/limine_boot.c`（BSS 栈窗口）、`kernel/linker.ld`、`arch/x86_64/entry.S`、`arch/x86_64/gdt.c`（TSS `rsp0`）、`proc/process.c` 五处重复，改布局必须同步。
- 多进程：每个进程独立 CR3（内核高半区共享，用户窗口 PML4 slot 2 私有）、独立内核栈和抢占线程；`spawn/exit/wait` 已实现，调度器切换 CR3 与 TSS rsp0。调度交接用 `on_cpu` 标记，旧线程完全切走后才允许其他 CPU 重新选中；新线程以关中断状态起步，`thread_start` 完成交接后再 `sti`。 fork/exec 见 `kernel/proc/process_fork.c`：`fork` 逐页复制用户映射（连同页权限）和 zfd 描述符，子线程经 `user_enter_context` 在父进程 `int 0x80` 之后以 `rax=0` 返回；`exec` 先在全新地址空间装载镜像，成功后才切 CR3、释放旧页并把中断帧的 `rip/rsp` 指向新入口，失败时保留原程序。`pipe`（`kernel/ipc/pipe.c`）是 64 KiB 环形缓冲，读端和写端各占一个 zfd，fork/dup 会同步读写端计数；`dup2` 只接受 zfd 编号（64..95），VFS 描述符仍是进程全局表。
- 抢占式多线程调度器：`kernel/proc/thread.c` 维护共享线程表（32 槽），BSP/AP 都用 LAPIC timer 以 1 kHz 触发 `sched_tick`（PIT 仅在启动时做一次 LAPIC 校准），量子 `SCHED_QUANTUM_TICKS=10`。`arch/x86_64/switch.S` 保存/恢复 callee-saved 寄存器与 RFLAGS；线程栈 16 KiB，退出线程由下一次上下文切换在 `sched_reap_local` 中释放。`kthread_create` 的 `affinity` 为 -1 表示可在任意 CPU 运行，主线程固定 affinity 0。
- SMP：`arch/x86_64/apic.c` 解析 RSDP/XSDT/MADT（`apic_locate_rsdp` 在内核侧兜底扫描低端 1 MiB）；`arch/x86_64/smp.c` 把 `ap_trampoline.S` 复制到物理 `0x7000`，以 INIT-SIPI-SIPI 启动 AP。AP 必须在 `ap_entry` 中重新加载 GDT 和 IDT，并使用自己的 TSS（选择子 `0x28 + 16*cpu`），否则第一个中断就是三重故障。`arch/x86_64/percpu.c` 按 APIC id 索引每 CPU 数据；PIC 设备中断只留在 BSP（LINT0=ExtINT），AP 的 LINT0/1 屏蔽。
- 并发保护：`spinlock.h` 提供 `spin_lock_irqsave`；PMM、堆、控制台输出和调度器共享状态都必须在锁内操作，中断上下文只使用 irqsave 变体，避免同 CPU 抢占自锁。
- 桌面分层（规则 22/23/24）：开机在图形模式直接 `spawn /bin/aqua`。`src/apps/ADE/main.cpp` 只做入口、应用注册与 `ade.h` API 转发，壁纸/菜单栏/Dock 分别在 `wallpaper.cpp`/`menubar.cpp`/`dock.cpp`，窗口协议在 `display/server.cpp`；`src/apps/ADE/winmgr/dwm.cpp` 独占窗口装饰、z 序、拖动/关闭、焦点、损伤合成与输入。应用在 `src/apps/deskapps/`（历史源码，不再进入内核）；8sh 在 `src/apps/csh/`，按命令拆分为 `main.c`/`io.c`/`commands.c`/`builtins.c`/`files.c`/`run.c`/`write.c`/`install.c`，独立客户端在 `src/user/` 与 `src/user/lib/`，只依赖 `ade_protocol.h` + `ade_client.h`；图标为内联 SVG，由 `kernel/gfx/svg.c` 光栅化。所有绘制经 `kernel/gfx/render.c`（ZOS API 1.0，见 `zos_api.h`），Ring3 复用同一份实现；Desktop Window Manager/Aqua Desktop Environment 不直接访问帧缓冲像素。Ring3 字体引擎是 FreeType 2.13.3（`third_party/freetype`，经 `user/lib/freetype/ttf_freetype.c` 适配 Aqua Desktop Environment），只加载 MiSans、MiSans Global、Inter 三种 OpenType 字体；WQY 及非 OpenType 回退已移除。内核启动字体暂沿用 `kernel/font/ttf.c`/`stbtt.c`。Limine 把字体与壁纸作为模块加载（ISO 上是 `/boot/limine/MiSans.ttf`、`/boot/limine/WALL.RGB`，安装后是 ESP 根的 `/MiSans.ttf`、`/WALL.RGB`），`boot_info.font_phys/font_size` 与 `wallpaper_phys/wallpaper_size` 再交给内核（`36=boot_font`、`58=boot_wallpaper` 供 Aqua Desktop Environment 取用），因此单 ISO/无数据盘启动也能显示中文和壁纸。
- 迁移中：Aqua Desktop Environment 已作为 `/bin/aqua` 在 Ring3 运行，8sh 作为 `/bin/8sh` 在 Ring3 运行，内核不再保留内核态桌面回退；Welcome 与 Calculator 已迁移为独立 `.zxe` 客户端，Files/Monitor 的 Ring3 客户端迁移待续；全部硬件驱动已下沉到 `/bin/drivers`。显示协议在 `user/lib/include/ade_protocol.h`，服务端在 `src/apps/ADE/display/server.cpp`，客户端库在 `src/user/lib/ade_client.c`。
- 系统调用 ABI：`int 0x80`，`rax`=调用号，`rdi/rsi/rdx/r10`=参数；0=exit、1=write、2=getpid、3=open、4=read、5=close、6=uptime、7=spawn、8=wait、9=port_listen、10=port_connect、11=port_accept、12=port_send、13=port_recv、14=port_close、15=shm_create、16=shm_map、17=shm_unmap、18=shm_close、19=fb_map、20=mouse、21=key、22=sleep、23=cpus、24=rtc、25=read_path、26=port_try_accept、27=port_try_recv、28=serial_write、29=readdir、30=disks、31=install、32=write_file、33=unlink、34=data_volume、35=dev_alloc_page、36=boot_font、37=spawn_arg、38=blk_count、39=blk_info、40=blk_read、41=blk_write、42=blk_flush、43=socket、44=bind、45=listen、46=accept、47=connect、48=sendmsg、49=recvmsg、50=poll、51=memfd_create、52=ftruncate、53=lseek、54=mmap、55=munmap、56=fcntl、57=socketpair、58=boot_wallpaper、59=memfd_phys、60=map_phys、61=eventfd、62=timerfd_create、63=timerfd_settime、64=timerfd_gettime、65=epoll_create1、66=epoll_ctl、67=epoll_wait、68=install_error、69=reboot、70=install_progress、71=install_config、72=fork、73=exec、74=pipe、75=dup2。调用号在 `kernel/proc/syscall.c` 与 `user/userlib.h` 各一份，必须同步；`memfd_phys`/`map_phys` 只授予内核启动的 `/bin/display`（`display_cap`），用于把 memfd 扫描缓冲和 PCI MMIO BAR 交给 virtio-gpu。用户指针按当前进程地址空间逐页校验。socket/memfd 描述符从 64（`ZFD_BASE`）起，VFS 保留 3..18；`SCM_RIGHTS` 目前只传 memfd，`/data/NET_TEST.ZXE` 覆盖 M0 回归。
- 用户线程经 `user_enter` `iretq` 进 Ring3；退出或用户异常调用 `process_exit_current()` → `kthread_exit()`，不再通过 `user_resume()` 返回。只有 `process_handle_exception` 声明 `noreturn`。
- 用户 ELF 是链接到 RVA 0 的 PIE（`-fpie`/`-fPIC` + `src/user/user.ld`，无 `-mcmodel=large`），由 `tools/zxe-pack.rb` 转成 ZXE v1；内核 `proc/zxe_loader.c` 按 64 KiB 对齐在 `[USER_LOAD_BASE, USER_LOAD_BASE + 1 GiB)` 内随机选择映像基址（最多 64 次，失败即拒绝启动），用户栈仍在 `0x10100000000`。ZXE 段权限强制 W^X（CODE=RX、RODATA=R、DATA/ZERO=RW，页 NX 由 `vmm_init`/AP trampoline 打开 EFER.NXE 支持）。`tools/zxe-check.rb` 校验 ZXE v1 结构，`tools/zxe-fixture-lib.rb` 生成动态链接测试用的最小共享库；依赖只从 `/lib` 按简单文件名加载。内核与用户代码一样禁用 SSE/red zone（Ring3 字体源码例外）。
- 内核是 freestanding：无 libc，常规文件禁用浮点/SSE/MMX/AVX（`-mgeneral-regs-only`）；内核启动字体仍由 `kernel/font/ttf.c`、`kernel/font/stbtt.c` 通过 `FONT_CFLAGS` 提供。Ring3 桌面字体改用 `third_party/freetype`，由 `FREETYPE_CFLAGS` 启用 SSE2；`smooth.c` 因 GCC 在 -O1/-O2 下会错误编译而单独使用 `-O0`。C++ 关闭异常、RTTI、threadsafe statics，不要用 STL/iostream。字符串/打印用 `kernel/lib` 里的实现。
- initramfs 源在 `src/initramfs/`，`src/Makefile` 的 `RAMFS_FILES` 是打包白名单；新增文件必须同时改 `RAMFS_FILES`。`src/initramfs/data/` 只用于生成 FAT32 数据盘，不进入 initramfs。
- 存储经 Ring3 驱动服务（AHCI 在 `/bin/drivers`，内核通过共享页 RPC）；FAT32 支持文件级创建/覆盖/删除（`kernel/fs/fat32_write.c`），按卷标 `ZHAOXUDATA` 选盘并挂到 `/mnt`，`/` 是 initramfs；VFS 读取 fd 为只读，写入走 `write_file`/`unlink` 路径。没有数据盘时 `/mnt/...` 自动回退到 initramfs 内嵌镜像 `/data/...`（`RAMFS_FILES` 的 data 条目，名字必须用大写 8.3 风格与 FAT 卷一致）。ISO 启动时字体来自 Limine 模块 `/boot/limine/MiSans.ttf`；安装后的系统字体来自 ESP 根的 `/MiSans.ttf`（`ZHAOXUDATA` 数据分区只用于用户数据）。UEFI 与 BIOS 启动相同。只允许 MiSans、MiSans Global、Inter 三种 OpenType 字体。
- 安装器（`kernel/fs/install.c`）自包含（规则 27）：不读取任何额外介质盘。Limine 的 UEFI 加载器来自 initramfs `/boot/limine/BOOTX64.EFI`，BIOS 引导写入 Limine 提供的 HDD 镜像（`tools/limine-bios-hdd.rb` 从 vendored 头文件抽出），内核取运行中镜像并合成单段 ELF。安装时用 `kernel/fs/fat32_format.c` 在目标盘 LBA 2048 格式化 FAT32 ESP（卷标 `ZHAOXUEFI`，默认 64 MiB，自定义 64/128/256 MiB），写入 `EFI/BOOT/BOOTX64.EFI`、`EFI/BOOT/limine.conf`、`boot/limine/limine-bios.sys`、`boot/limine/limine.conf`，并在 ESP 根写 `KERNEL.ELF`、`KERNEL.BIN`、`MiSans.ttf`、`WALL.RGB`（VFAT 长名，`fat32_write.c` 的写侧 FAT 缓存 + 空闲槽复用）。ext4 `ZHAOXUROOT` 由 `/bin/ext4fs` 服务格式化，只承载文件系统本身。布局二选一（规则 15）：MBR = LBA0 Limine stage1 + LBA1 stage2 + 空隙 + ESP + ext4 + 可选数据分区；GPT = 保护性 MBR + LBA1/2 头与项 + LBA34 stage2 + 空隙 + ESP + ext4 + 可选数据分区 + 末尾备份 GPT。`limine.conf` 在 GPT 下用 `boot(2)`、MBR 下用 `boot(1)` 指向 ESP。目标盘必须是空白、带 Limine 引导扇区（`EB 3C 90` + `LIMINE`）或既有 Zhaoxu 布局（首分区 0xEF/0x0C/0x0E）；`ZHAOXUDATA` 盘永不作为目标。校验阶段按偏移 3 的 `LIMINE` 签名检查引导扇区，并检查 ESP 的 FAT32 标志、ext4 超级块和（可选）数据分区标签。内核挂载时解析 GPT 项或 MBR 分区表（`kernel/fs/fat32.c`）并按卷标优先选择 `ZHAOXUDATA`。
- AHCI DMA 用物理地址，依赖内核的物理内存恒等映射（低 4 GiB + `vmm_init` 扩展的高内存）；命令头 `prdtl` 在 offset 2（`opts | prdtl << 16`），不要写到 `status`。无数据命令（FLUSH）必须保持 `prdtl = 0`。
- Ada 驱动在 `src/drivers/*.ads` 用 `pragma Export, Convention => C` 导出符号；它们现在编译为 Ring3 `/bin/drivers`（`-mcmodel=large`），与 `apps/drivers/` 的 C 胶水一起链接。驱动进程有 0..4 GiB 恒等用户映射与 IOPL=3，DMA/MMIO 指针即物理地址；`pmm_alloc_page` 通过 `dev_alloc_page` 系统调用实现。
- 内核与驱动共享 4 个物理页（头/数据/输入/日志，`kernel/include/dev.h`）：块 I/O、RTC、信息查询走请求状态机；键盘字符与鼠标状态走原子输入环；内核日志写入日志环，由驱动输出到 COM1。`DEBUG_SERIAL=1` 仅用于开发，默认构建不包含内核串口代码。
- `build/`、`out/`、`esp/` 是生成物，不要手改。

## 版本与发布

- 版本号唯一来源是仓库根的 `version.cfg`（`key = value`，支持引号字符串、null 与行尾注释），schema 固定为 `name`、`name_cn`、`kernel`、`kernel_cn`、`os_version`、`kernel_version`、`build`、`codename`。**OS 版本、内核版本与构建号是三个字段，不要合并成 `version`**；`build` 是正整数，日常构建/修订必须递增，语义版本（os_version/kernel_version）按规则 22 另改。改版本只改这个文件，然后在 `src/` 运行 `make version`（Ruby 脚本 `tools/version.rb` 同步 `src/kernel/include/version.h`、`src/initramfs/etc/motd`、`src/initramfs/etc/version`、`README.md`）；`version.h` 导出 `ZHAOXU_VERSION`（OS）、`ZHAOXU_CODENAME`、`ZHAOXU_KERNEL`、`ZHAOXU_KERNEL_VERSION`、`ZHAOXU_BUILD`，C/C++ 代码只 include `<version.h>`，不要手工改派生文件。ISO 文件名用 `os_version`，`tools/iso-manifest.rb` 把 OS、内核版本与构建号都写进光盘元数据；`name_cn`/`kernel_cn` 缺失或为 null 时分别回退到英文名。
- 远端分支：GitHub `main`、GitCode `main`、Gitee `master`。**推送优先级：GitHub 主推送；GitCode 只在发布（release）时同步；Gitee 忽略，不要求提交或推送。**
- WSL 内没有 Git 凭据，push 会报 `could not read Username`。从 Windows Git 推送：
  ```powershell
  # 日常提交只推 GitHub
  git push github main
  # 仅正式 release 时再同步 GitCode（需要 personal access token）
  # git push gitcode main
  ```
- 没有 CI；日常推送后用 `git ls-remote` 核对 GitHub `main`；release 时再核对 GitCode。
