# Zhaoxu OS（内核 Yuanshuo）Agent 指南

## 规则（必须遵守）

1. 内核名称：`Yuanshuo Kernel`，操作系统名称：`Zhaoxu OS`，Shell：`csh`，桌面：`ADE`（风格：Aqua），窗口管理器：`DWM`，不得改动任何字符，严禁使用`Yuanshuo shell`。
2. Github 仓库（main）：https://github.com/Tinmc189623/zhaoxu-os
3. Gitee 仓库（master）：https://gitee.com/Tinmc189623/zhaoxu-os
4. GitCode 仓库（main）：https://gitcode.com/2503_94276351/zhaoxu-os
5. 用 WSL2，可用：C（内核+UEFI引导）、C++（上层应用）、GAS（底层）、FASM（BIOS引导）、SPARK（驱动）。
6. 脚本必须完全使用 Ruby 来编写，必须完全使用 AI 来生成代码，**禁止人类编写代码**。
7. 引导加载器必须支持 UEFI 和 Legacy BIOS 两种启动模式。 
8. 引导管理器的名称必须得是：`ZhaoxuBootMgr`。
9. 内核必须得是微内核，用户程序必须得放在 Ring3，内核的二进制必须得是干净的二进制，不允许往里投放什么驱动/应用。
10. 该操作系统必须遵循 Unix 哲学和使用 Unix 方式。 
11. iso命名示例：`zhaoxu-os_{version}_installer.iso`。
12. 不得将所有功能挤在一个文件里，如 `apps/shell.cpp`。
13. README.md 里只允许编写近一次版本更新，而不能编写所有版本，所有版本应放入 CHANGELOG.md 文件中。
14. README.md 里必须拥有 Emoji 和 HTML 以增加视觉效果。
15. 系统必须同时支持 MBR 和 GPT。
16. **AI** 写代码时优先追求效果和稳定性，不要为了快而在那块儿编写1s的代码，却看不着任何效果。
17. 字体：MiSans、MiSans Global、Inter。
18. 必须使用此`stb_truetype.h`，`https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h`（必须得是**原文**，不允许自己写一份）。
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
30. 禁止不使用 busybox。
31. 引导流程：`BIOS 固件 → ZhaoxuBootMgr 引导管理器 → osloader.elf（BIOS） / OSLDRX64.ELF（UEFI） → kernel.elf → vga.ko 通用显卡驱动`。
32. zhaoxuOS 的 GUI 采用经典 Aqua（Classic Aqua）视觉风格。计划构建独立的 Zhaoxu Aqua Framework（ZAF），以 Skia 为图形渲染基础，自研窗口管理器、桌面合成器、控件系统、动画引擎和主题引擎。要求具备深度自定义能力、可移植到自研操作系统，并尽量减少对其他操作系统 GUI 组件的依赖。Aqua 为默认主题，底层框架支持未来扩展其他视觉主题。
33. 系统默认壁纸必须得是 [src\apps\ADE\web\wallpaper\3pty\img0.jpg](src\apps\ADE\web\wallpaper\3pty\img0.jpg)。

## ISO（这是**ISO 介质本身**的目录，不是启动后内存里的根文件系统；live 系统本体在 `casper/filesystem.squashfs` 压缩包内，版本：v1.1.13）

```iso
iso-root/
├── boot/
│   └── zhaoxu/
│       ├── zhaoxu.cfg        # ZhaoxuBootMgr 主配置
│       ├── zhaoxuenv
│       ├── osloader.elf      # OS 加载器（BIOS）
│       ├── BOOTX16.BIN       # BIOS 引导管理程序
│       └── fonts/
│           └── unicode.pf2
├── EFI\
│   └── BOOT\
│       ├── BOOTX64.EFI       # EFI 引导管理程序
│       └── OSLDRX64.ELF      # OS 加载器（UEFI）
├── casper/                   # Live 核心目录，最重要
│   ├── filesystem.squashfs   # 主rootfs压缩镜像（live系统本体）
│   ├── initrd.img            # 初始化内存盘
│   ├── kernel.elf            # Yuanshuo 内核
│   ├── manifest
│   ├── vga.ko                # 通用显卡驱动
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
├── preseed/                  # 自动化安装预置脚本
│   └── os.seed
├── README.html
├── autorun.inf
└── md5sum.txt
```

## 构建与运行

- 构建必须支持四种宿主：原生 Linux、原生 macOS、原生 Windows（MSYS2/MinGW bash）与 WSL2。工作目录必须是仓库内的 `src/`；统一入口是 Ruby（规则 6），`ruby tools/build.rb` 自动识别宿主并调用 make，`ruby tools/build.rb --check` 打印当前宿主的工具矩阵与安装提示，`ruby tools/run.rb` 负责 QEMU/OVMF。具备完整工具链的 POSIX 环境也可以直接 `make`。
  ```bash
  cd src
  ruby tools/build.rb                  # 等同 make all
  ruby tools/build.rb --check          # base/kernel/iso/run 工具检查
  ruby tools/build.rb iso              # UEFI + legacy BIOS 双启动 ISO
  ruby tools/run.rb                    # 等同 make run-iso（UEFI，仅光盘）
  ruby tools/run.rb --display gtk      # 图形窗口
  make -j4                             # Linux/macOS/WSL 直接构建
  make host-test                       # 纯 Ruby 宿主检测单元测试
  ```
  - **Linux / WSL**：`gcc`、`g++`、`gcc-14`+`gnat-14`、`ld.lld`、`objcopy`、`clang`、`lld-link`、`qemu-system-x86_64`、OVMF、`mkfs.vfat`、`mtools`、`tar`、`xorriso`、`squashfs-tools`。
  - **macOS**：Homebrew `llvm`（clang、llvm-objcopy）、`lld`（ld.lld、lld-link）、`qemu`、`xorriso`、`mtools`、`dosfstools`、`squashfs`；Ada 驱动需要 x86_64-elf GNAT（用 `ADA=` 指向）。
  - **原生 Windows**：Ruby + MSYS2 的 `make`、`mingw-w64-x86_64-clang`、`-lld`、`-llvm`、`dosfstools`、`mtools`、`xorriso`、`squashfs-tools`、`qemu`；FASM 用 `winget install fasm.fasm` 或 `tools/fasm.exe`；Ada 驱动需要 x86_64-elf GNAT。没有 MSYS2 时用 `--host wsl` 或自动回退 WSL。
  - **Windows 入口**：`./src/build.ps1`、`./src/run.ps1`（PowerShell 7+）优先转发给 Ruby；Ruby 缺失时才退回旧的 WSL 转发路径。
- 单个用户程序：`make out/hello.zxe`（内部先链接 `build/hello.uelf`，再由 `tools/zxe-pack.rb` 转成 `.zxe`）。模式规则会自动编译 `user/hello.c` 并链接 `user/userlib.c`，不需要改 Makefile。程序入口是 `int user_main(long *stack)`，不是 `main`；`userlib.c` 的 `_start` 会把返回值作为退出码。ELF→.zxe 转换需要 Ruby：WSL 内没有时通过 Makefile 的 `RUBY` 变量指向 Windows 侧 ruby.exe（按本机位置覆盖，不要写死盘符）。ZXE v1 结构自检：`ruby tools/zxe-check.rb out/*.zxe`；动态链接冒烟用 `ruby tools/zxe-fixture-lib.rb out/libztest.zxe` 生成共享库，配合 `user/ztestmain.c`（导入 `ztest_value`）。
- `make`/`make all` 的最终产物是 `out/BOOTX64.EFI` 与 `esp/EFI/BOOT/BOOTX64.EFI`；`make out/data.img` 只是手动生成一个 FAT32 数据盘镜像，测试一律走 ISO。
- `make run-iso` 默认 q35、512 MiB、smp=4、`-display none` + 串口 stdio，只把 ISO 挂为 `ide.0` 光驱，不挂数据盘/安装盘；`make run-install` 额外挂一个 256 MiB 空白 `out/target.img`（首次创建）作为 `ide.1`。`make run` 现在就是 `run-iso` 的别名。带 `-no-reboot`，所以来宾发起重启会直接退出 QEMU。单核回归用 `make run-iso QEMU_SMP=1`。
- `make iso` 生成 `out/zhaoxu_{version}_installer.iso`：El Torito ESP（`out/efi.img`）只含 `EFI/BOOT/BOOTX64.EFI`；内核与字体放在光盘根的 `install/KERNEL.ELF`、`install/WQY.TTC`（不在 ESP 内），legacy BIOS 另带展开后的 `install/KERNEL.BIN` 与引导镜像 `bios.img`，由 xorriso 以 El Torito 双入口（两者都是 no-emulation）+ GPT hybrid 打包，最后由 `tools/iso-bios.rb` 回填 stage2 参数块。UEFI 加载器用内置的极简 ISO9660 读取器从光盘根取内核与字体。
- `out/bios.img` 是 no-emulation El Torito 镜像，**不含内核**：`[0,512)` 为 `boot/bios_stage1.asm`（512 字节、≤446 字节代码的 MBR/引导扇区），`[512,1024)` 为填充，`[1024,…)` 为 `boot/bios_stage2.asm`（固件把整镜像装到 0x7C00，stage 2 因此正好落在 0x8000，必须 ≤32 KiB）；`tools/bios-img.rb` 把 stage1 的 `stage2_lba` 补成 0xFFFF，表示“stage 2 已驻留”。内核在光盘上是普通文件 `install/KERNEL.BIN`（`tools/kernel-flat.rb` 把 `out/kernel.elf` 的 PT_LOAD 展开并按 2048 对齐），`tools/iso-bios.rb` 解析 ISO9660 后把 48 字节 `ZBOOTPRM`（kernel LBA/字节数/扇区数、cd 标志、字体 LBA/字节数）写进光盘里的 stage2 副本。stage 2 用 INT 13h AH=48h 取设备扇区大小（CD 为 2048 → shift 11），每次 EDD 读截到 64 KiB 边界，内核被整块读进 0x10000（≤448 KiB，避开 EBDA），再由保护模式一次拷到 32 MiB 窗口。安装盘上的 `param_cd` 仍为 0、LBA 以 512 字节扇区计。
- legacy BIOS 链：stage1 用 INT 13h CHS 读 stage2（ISO 上 stage2 已驻留，直接远跳 0x8000）→ stage2 做 VBE 32bpp LFB 选择、E820、RSDP 扫描 → 用 EDD LBA48 读内核 → 建页表与 `boot_info` → 进长模式，契约与 UEFI 层一致。
- OVMF VARS 不能用只读的系统文件；`make run` 会先复制到可写的 `out/OVMF_VARS.fd`。OVMF 路径可用 `OVMF_CODE`/`OVMF_VARS` 覆盖。
- `HEADLESS=1` 只影响 FASM 引导层（跳过 GOP 探测与 UEFI ConOut），不改变内核/用户态代码；此时显示只能走串口。`build.ps1 -Headless` 就是设这个值。
- 依赖与 FASM：各宿主所需工具见 `ruby tools/build.rb --check`。仓库自带的 `src/tools/fasm` 是 Linux x86-64 ELF，只能用于 Linux/WSL；macOS 与原生 Windows 需要宿主的 FASM 可执行文件，用 `FASM=` 指定（Windows 也可把 `fasm.exe` 放在 `src/tools/`）。
- 入口脚本：`build.ps1`/`run.ps1` 是 Windows 薄转发层，实际逻辑在 `tools/build.rb`、`tools/run.rb`、`tools/host.rb`（全部 Ruby，规则 6）；`tools/install-fasm.sh` 已不存在，Windows/macOS 的 FASM 由用户按上面的方式提供。

## 验证

没有自动化测试；改动后至少做一次 QEMU 冒烟：

1. `ruby tools/build.rb --check` 应列出当前宿主的工具与 OVMF；`ruby tests/tools/host_test.rb` 应输出 `PASS`。然后 `make clean && make -j4` 干净重建，`nm -u out/kernel.elf` 应无未定义符号；`nm out/kernel.elf` 不应出现 `ahci_*`/`acpi_init`/`serial_init`/`keyboard_*`/`mouse_*`/`pit_init`/`pci_*` 等驱动符号。
2. `make run-iso` 后应直接进入 ADE 桌面：日志出现 `Kernel: /install/KERNEL.ELF from the ISO.`、`Font: /install/WQY.TTC from the ISO.`、`[ade ] font boot volume: ...`、`[ade ] Ring3 display server`，随后 `spawn /bin/apps/welcome.zxe` 与 `/bin/apps/calc.zxe` 并显示两个客户端窗口；点击欢迎窗口应回显 `已收到点击事件`，计算器按 `7 + 5 =` 应显示 `12`，关闭计算器后 Dock 回收对应图标。内核不再链接任何桌面或应用代码（规则 9）。无帧缓冲时内核启动 Ring3 `/bin/csh`，应能执行 `help`、`ls /mnt`、`cat /mnt/HELLO.TXT`、`run /data/HELLO.ZXE`（退出码 7）、`write /mnt/NEW.TXT hi`、`cat /mnt/NEW.TXT`、`rm /mnt/NEW.TXT`、`disks`、`install`、`uptime`、`cpus`、`exit`。
3. 用户态异常只应终止进程并回到 Shell（退出码 `-向量号`，如 `#PF` 为 `-14`）；内核异常才 panic。
4. ISO 改动：`make iso` 生成 `out/zhaoxu_{version}_installer.iso`（规则 11 命名），分别用 `make run-iso`（UEFI）与 `make run-bios`（SeaBIOS）从 ISO 启动；两者都应打印内核/字体来自 `/install` 的日志，中文正常显示，且不挂任何数据盘。
5. 安装回归：`make run-install`（GPT）与 `make run-install-bios`（MBR）各挂一个空白 `out/target.img`，在安装窗口点选对应按钮，日志应有 `[install] complete`；随后只用该目标盘启动（UEFI 或 SeaBIOS），应打印 `mounted "ZHAOXUDATA" on /mnt` 且 `[ade ] font /mnt/WQY.TTC`，桌面中文正常。
6. SMP/调度器回归：`make run-iso QEMU_SMP=4 SCHED_STATS=1` 应打印（默认 `SCHED_STATS=0`，不输出周期统计，避免刷屏） `4/4 CPU(s) online` 与三行 `[sched] ... churn=12/12`；各 AP 的 worker 计数并行增长，`threads` 为 9、`heap` 三次采样一致（证明线程栈已回收）。再用 `QEMU_SMP=1` 验证单核回退，ADE 桌面与 Dock 应用仍正常。BIOS 路径应打印 `ACPI RSDP located.`；UEFI 与 BIOS 的 LAPIC timer 校准值在数千万 Hz 量级。
7. legacy BIOS 的 RSDP 由 stage2 在低端 1 MiB 按字节扫描（QEMU q35/SeaBIOS 会放在非规范位置）；内核 `apic_locate_rsdp` 再做一次兜底扫描，二者都找不到时退回单 CPU，仍用 LAPIC 计时。
8. 帧缓冲与桌面：`make run-iso QEMU_DISPLAY="-display gtk"` 人工查看；无头回归用 QEMU HMP `screendump /tmp/ade.ppm`（QEMU 输出 PPM，可用脚本转 PNG），检查菜单栏、窗口层叠、Dock 与 SVG 图标。`ruby src/tools/smoke.rb` 的 Shell 命令流程仅在无帧缓冲回退 csh 时有效。

## 架构与不变量

- 单镜像：引导层统一使用 `out/kernel.elf`：UEFI 加载器从 ISO 读 `/install/KERNEL.ELF`、从已安装盘读 ext4 根下的 `/KERNEL.ELF`；legacy BIOS 从 ISO 读 ISO9660 的 `install/KERNEL.BIN`（LBA 由 `tools/iso-bios.rb` 回填进光盘里的 stage2 副本），从已安装盘读 ext4 上的 `KERNEL.BIN`（安装器把该连续区段的 512 字节 LBA 块列表写进 stage2 参数块，`param_cd = 0`）；`bios.img` 只装 stage1/stage2，任何工具都不再往引导镜像里塞内核（不再生成 `kernel.bin`）；内核二进制只含 CPU/内存/调度/IPC/VFS 核心，不含任何驱动或应用；AHCI、PS/2、鼠标、RTC、串口由 Ring3 `/bin/drivers` 独占（规则 9）。引导入口有两条：UEFI 是 `src/boot/efi_main.c`（C 编写，clang + lld-link 生成 PE32+ 应用，通过 Simple File System 读取 `\EFI\BOOT\KERNEL.ELF`，规则 25/26），legacy BIOS 是 `src/boot/bios_stage1.asm` → `src/boot/bios_stage2.asm`。UEFI 加载器先用 AllocateAddress 申请固定 16 MiB 的内核暂存区，失败则用 AllocateMaxAddress 保证仍落在 4 GiB 以下；页表池同样必须低于 4 GiB（写页表发生在切 CR3 之前）。自建页表时必须把 EFI 镜像区（Loaded Image Protocol 查基址，VMware 会把 EFI 应用装到 4 GiB 以上）、内核暂存区（含 +12 MiB 内存映射、+14 MiB boot_info）、GOP 帧缓冲（大显存机器常在 4 GiB 以上）和固件栈一并恒等映射，然后再切 CR3——否则切完取指/写像素即 #PF。`vmm_init` 之后再把恒等映射扩展到内存映射覆盖的 64 GiB。AHCI/PCIe 等 MMIO BAR 仍按 4 GiB 以下使用。
- `struct boot_info` 的字段顺序必须与 `src/boot/efi_main.c` 和 `src/boot/bios_stage2.asm` 同步，magic 为 `0x5A4F5331`。两层都在物理窗口 `+12 MiB` 放内存映射、`+14 MiB` 放 `boot_info`；`kernel/linker.ld` 的 `ASSERT` 只保证内核结束于 `+14 MiB` 之前，内核显著增大时要同时检查这两个偏移。BIOS 层把 E820 转成 48 字节 UEFI 描述符（type 1→7，其余→2），并必须把 16 MiB 内核窗口整体清零（内核 `.bss` 不会由引导层外的代码清）。
- 地址布局：内核 VMA `0xffffffff80000000`，内核栈顶 `0xffffffff80f00000`，内核堆 `0xffffffff90000000`；低端 `0..64 GiB` 是物理内存恒等映射（loader 建 4 GiB，`vmm_init` 按内存映射补到 64 GiB），用户程序基址 `0x10000000000`（1 TiB，必须避开恒等映射区），用户栈顶 `0x10100000000`（32 页）。内核栈顶在 `boot/efi_main.c`、`kernel/linker.ld`、`arch/x86_64/entry.S`、`arch/x86_64/gdt.c`（TSS `rsp0`）、`proc/process.c` 五处重复，改布局必须同步。
- 多进程：每个进程独立 CR3（内核高半区共享，用户窗口 PML4 slot 2 私有）、独立内核栈和抢占线程；`spawn/exit/wait` 已实现，调度器切换 CR3 与 TSS rsp0。调度交接用 `on_cpu` 标记，旧线程完全切走后才允许其他 CPU 重新选中；新线程以关中断状态起步，`thread_start` 完成交接后再 `sti`。
- 抢占式多线程调度器：`kernel/proc/thread.c` 维护共享线程表（32 槽），BSP/AP 都用 LAPIC timer 以 1 kHz 触发 `sched_tick`（PIT 仅在启动时做一次 LAPIC 校准），量子 `SCHED_QUANTUM_TICKS=10`。`arch/x86_64/switch.S` 保存/恢复 callee-saved 寄存器与 RFLAGS；线程栈 16 KiB，退出线程由下一次上下文切换在 `sched_reap_local` 中释放。`kthread_create` 的 `affinity` 为 -1 表示可在任意 CPU 运行，主线程固定 affinity 0。
- SMP：`arch/x86_64/apic.c` 解析 RSDP/XSDT/MADT（`apic_locate_rsdp` 在内核侧兜底扫描低端 1 MiB）；`arch/x86_64/smp.c` 把 `ap_trampoline.S` 复制到物理 `0x7000`，以 INIT-SIPI-SIPI 启动 AP。AP 必须在 `ap_entry` 中重新加载 GDT 和 IDT，并使用自己的 TSS（选择子 `0x28 + 16*cpu`），否则第一个中断就是三重故障。`arch/x86_64/percpu.c` 按 APIC id 索引每 CPU 数据；PIC 设备中断只留在 BSP（LINT0=ExtINT），AP 的 LINT0/1 屏蔽。
- 并发保护：`spinlock.h` 提供 `spin_lock_irqsave`；PMM、堆、控制台输出和调度器共享状态都必须在锁内操作，中断上下文只使用 irqsave 变体，避免同 CPU 抢占自锁。
- 桌面分层（规则 22/23/24）：开机在图形模式直接 `spawn /bin/ade`。`src/apps/ADE/main.cpp` 只做入口、应用注册与 `ade.h` API 转发，壁纸/菜单栏/Dock 分别在 `wallpaper.cpp`/`menubar.cpp`/`dock.cpp`，窗口协议在 `display/server.cpp`；`src/apps/ADE/winmgr/dwm.cpp` 独占窗口装饰、z 序、拖动/关闭、焦点、损伤合成与输入。应用在 `src/apps/deskapps/`（历史源码，不再进入内核）；csh 在 `src/apps/csh/`，按命令拆分为 `main.c`/`io.c`/`commands.c`/`builtins.c`/`files.c`/`run.c`/`write.c`/`install.c`，独立客户端在 `src/user/` 与 `src/user/lib/`，只依赖 `ade_protocol.h` + `ade_client.h`；图标为内联 SVG，由 `kernel/gfx/svg.c` 光栅化。所有绘制经 `kernel/gfx/render.c`（ZOS API 1.0，见 `zos_api.h`），Ring3 复用同一份实现；DWM/ADE 不直接访问帧缓冲像素。字体由系统字体引擎（`third_party/stb/stb_truetype.h` v1.26 原文，经**同一份** `kernel/font/stbtt.c` 适配内核与 Ring3）加载 MiSans/MiSans Global/Inter，最后回退 WQY.TTC。UEFI 加载器从光盘根 `/install/WQY.TTC`（ISO9660）预读字体并经 `boot_info.font_phys/font_size` 传给内核（`36=boot_font` 系统调用供 ADE 取用），因此单 ISO/无数据盘启动也能显示中文；安装器把字体写入目标盘的 `ZHAOXUDATA` 数据分区。
- 迁移中：ADE 已作为 `/bin/ade` 在 Ring3 运行，csh 作为 `/bin/csh` 在 Ring3 运行，内核不再保留内核态桌面回退；Welcome 与 Calculator 已迁移为独立 `.zxe` 客户端，Files/Monitor 的 Ring3 客户端迁移待续；全部硬件驱动已下沉到 `/bin/drivers`。显示协议在 `user/lib/include/ade_protocol.h`，服务端在 `src/apps/ADE/display/server.cpp`，客户端库在 `src/user/lib/ade_client.c`。
- 系统调用 ABI：`int 0x80`，`rax`=调用号，`rdi/rsi/rdx/r10`=参数；0=exit、1=write、2=getpid、3=open、4=read、5=close、6=uptime、7=spawn、8=wait、9=port_listen、10=port_connect、11=port_accept、12=port_send、13=port_recv、14=port_close、15=shm_create、16=shm_map、17=shm_unmap、18=shm_close、19=fb_map、20=mouse、21=key、22=sleep、23=cpus、24=rtc、25=read_path、26=port_try_accept、27=port_try_recv、28=serial_write、29=readdir、30=disks、31=install、32=write_file、33=unlink、34=data_volume、35=dev_alloc_page、36=boot_font、37=spawn_arg。调用号在 `kernel/proc/syscall.c` 与 `user/userlib.h` 各一份，必须同步；用户指针按当前进程地址空间逐页校验。
- 用户线程经 `user_enter` `iretq` 进 Ring3；退出或用户异常调用 `process_exit_current()` → `kthread_exit()`，不再通过 `user_resume()` 返回。只有 `process_handle_exception` 声明 `noreturn`。
- 用户 ELF 是链接到 RVA 0 的 PIE（`-fpie`/`-fPIC` + `src/user/user.ld`，无 `-mcmodel=large`），由 `tools/zxe-pack.rb` 转成 ZXE v1；内核 `proc/zxe_loader.c` 按 64 KiB 对齐在 `[USER_LOAD_BASE, USER_LOAD_BASE + 1 GiB)` 内随机选择映像基址（最多 64 次，失败即拒绝启动），用户栈仍在 `0x10100000000`。ZXE 段权限强制 W^X（CODE=RX、RODATA=R、DATA/ZERO=RW，页 NX 由 `vmm_init`/AP trampoline 打开 EFER.NXE 支持）。`tools/zxe-check.rb` 校验 ZXE v1 结构，`tools/zxe-fixture-lib.rb` 生成动态链接测试用的最小共享库；依赖只从 `/lib` 按简单文件名加载。内核与用户代码一样禁用 SSE/red zone（Ring3 字体源码例外）。
- 内核是 freestanding：无 libc，常规文件禁用浮点/SSE/MMX/AVX（`-mgeneral-regs-only`）；`kernel/font/ttf.c`、`kernel/font/stbtt.c` 在内核通过 `FONT_CFLAGS`，Ring3 用**同一份**源码通过 `ADE_FONT_CFLAGS` 启用 SSE2/x87 供 stb_truetype 使用。C++ 关闭异常、RTTI、threadsafe statics，不要用 STL/iostream。字符串/打印用 `kernel/lib` 里的实现。
- initramfs 源在 `src/initramfs/`，`src/Makefile` 的 `RAMFS_FILES` 是打包白名单；新增文件必须同时改 `RAMFS_FILES`。`src/initramfs/data/` 只用于生成 FAT32 数据盘，不进入 initramfs。
- 存储经 Ring3 驱动服务（AHCI 在 `/bin/drivers`，内核通过共享页 RPC）；FAT32 支持文件级创建/覆盖/删除（`kernel/fs/fat32_write.c`），按卷标 `ZHAOXUDATA` 选盘并挂到 `/mnt`，`/` 是 initramfs；VFS 读取 fd 为只读，写入走 `write_file`/`unlink` 路径。没有数据盘时 `/mnt/...` 自动回退到 initramfs 内嵌镜像 `/data/...`（`RAMFS_FILES` 的 data 条目，名字必须用大写 8.3 风格与 FAT 卷一致）。ISO 启动时字体来自光盘根 `/install/WQY.TTC`；安装后的系统字体来自目标盘 `ZHAOXUDATA` 数据分区（UEFI 与 BIOS 启动相同）。数据盘上的 MiSans/MiSans Global/Inter 优先。
- 安装器（`kernel/fs/install.c`）自包含（规则 27）：不读取任何额外介质盘，UEFI 引导器来自 initramfs 的 `/boot/BOOTX64.EFI`，内核取运行中镜像并合成单段 ELF。安装时用 `kernel/fs/fat32_format.c` 在目标盘 LBA 2048 现场格式化两个 FAT32 卷：64 MiB 的 ESP（卷标 `ZHAOXUEFI`，仅 `EFI/BOOT/BOOTX64.EFI` 与 `EFI/BOOT/KERNEL.ELF`）和其后的 `ZHAOXUDATA` 数据分区（字体等运行资产）。布局二选一（规则 15）：MBR = stage1 + LBA1 stage2 + kernel + 空隙 + ESP + 数据分区；GPT = 保护性 MBR + LBA1/2 头与项 + LBA34 stage2 + kernel + 空隙 + ESP + 数据分区 + 末尾备份 GPT。stage1 的 stage2 LBA 通过 `ZSLBA` 标记打补丁。目标盘必须是空白或既有 Zhaoxu 布局（首分区 0xEF/0x0C/0x0E 或保护性 MBR）；`ZHAOXUDATA` 盘永不作为目标。内核挂载时解析 GPT 项或 MBR 分区表（`kernel/fs/fat32.c`）并按卷标优先选择 `ZHAOXUDATA`。
- AHCI DMA 用物理地址，依赖内核的物理内存恒等映射（低 4 GiB + `vmm_init` 扩展的高内存）；命令头 `prdtl` 在 offset 2（`opts | prdtl << 16`），不要写到 `status`。无数据命令（FLUSH）必须保持 `prdtl = 0`。
- Ada 驱动在 `src/drivers/*.ads` 用 `pragma Export, Convention => C` 导出符号；它们现在编译为 Ring3 `/bin/drivers`（`-mcmodel=large`），与 `apps/drivers/` 的 C 胶水一起链接。驱动进程有 0..4 GiB 恒等用户映射与 IOPL=3，DMA/MMIO 指针即物理地址；`pmm_alloc_page` 通过 `dev_alloc_page` 系统调用实现。
- 内核与驱动共享 4 个物理页（头/数据/输入/日志，`kernel/include/dev.h`）：块 I/O、RTC、信息查询走请求状态机；键盘字符与鼠标状态走原子输入环；内核日志写入日志环，由驱动输出到 COM1。`DEBUG_SERIAL=1` 仅用于开发，默认构建不包含内核串口代码。
- `build/`、`out/`、`esp/` 是生成物，不要手改。

## 版本与发布

- 版本号唯一来源是仓库根的 `version.cfg`（`key = value`，支持引号字符串、null 与行尾注释）。改版本只改这个文件，然后在 `src/` 运行 `make version`（Ruby 脚本 `tools/version.rb` 同步 `src/kernel/include/version.h`、`src/initramfs/etc/motd`、`src/initramfs/etc/version`、`README.md`）；C/C++ 代码统一 include `<version.h>` 使用 `ZHAOXU_VERSION`/`ZHAOXU_CODENAME`，不要手工单独改派生文件。`kernel_cn` 缺失或为 null 时 motd 回退使用英文内核名。
- 三个远端分支不同：GitHub `main`、Gitee `master`、GitCode `main`。
- WSL 内没有 Git 凭据，push 会报 `could not read Username`。从 Windows Git 推送：
  ```powershell
  # 在仓库根目录执行（PowerShell，PSCommandPath 所在目录）
  git push github main
  git push gitee main:master
  git push gitcode main
  ```
- 没有 CI；推送后用 `git ls-remote` 核对三个远端指向同一提交。
