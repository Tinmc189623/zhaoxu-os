# Zhaoxu OS（内核 Yuanshuo）Agent 指南

## 规则（必须遵守）

1. 内核名称：`Yuanshuo Kernel`，操作系统名称：`Zhaoxu OS`，Shell：`csh`，桌面：`ADE`（风格：Aqua），窗口管理器：`DWM`，不得改动任何字符，严禁使用`Yuanshuo shell`。
2. Github 仓库（main）：https://github.com/Tinmc189623/zhaoxu-os
3. Gitee 仓库（master）：https://gitee.com/Tinmc189623/zhaoxu-os
4. GitCode 仓库（main）：https://gitcode.com/2503_94276351/zhaoxu-os
5. 用 WSL2，可用：C（内核+UEFI引导）、C++（上层应用）、GAS（底层）、FASM（BIOS引导）、SPARK（驱动）。
6. 脚本必须完全使用 ruby 来编写，必须完全使用 AI 来生成代码。
7. 引导加载器必须支持 uefi 和 legacy bios 两种启动模式。 
8. 引导加载器的名称必须得是：`ZhaoxuBootloader`。
9. 内核必须得是微内核，用户程序必须得放在 Ring3，内核的二进制必须得是干净的二进制，不允许往里投放什么驱动/应用。
10. 该操作系统必须遵循 unix 哲学和使用 unix 方式。 
11. iso命名示例：`zhaoxu_{version}_installer.iso`。
12. 不得将所有功能挤在一个文件里，如 `apps/shell.cpp`。
13. README.md 里只允许编写进一次版本更新，而不能编写所有版本，所有版本应放入 CHANGELOG.md 文件中。
14. readme里必须拥有emoji和html以增加视觉效果。
15. 系统必须同时支持 MBR 和 GPT。
16. **AI** 写代码时优先追求快与效果和稳定性，不要为了“稳定性”而在那块儿编写三千天的代码，却看不着任何效果。
17. 字体：MiSans、MiSans Global、Inter。
18. 必须使用此`stb_truetype.h`，`https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h`（必须得是**原文**，不允许自己写一份儿）。
19. 写东西的时候先看见 stb 里头有没有需要的，如果有需要就直接复用，别自己自研。
20. 项目必须遵循 KISS 原则。
21. 必须支持的文件系统：FAT32、ext4，且为完整支持。
22. 应用图标必须得是svg图标。
23. 一个文件只允许做一个功能，不允许把所有东西塞到一个文件中。
24. 系统 API：`Zhaoxu Operating System Application Programming Interface Version 1.0`
25. 不得将内核写入 `BOOTX64.EFI`。
26. 内核：`\EFI\BOOT\KERNEL.ELF`。
27. 用户在安装系统时，只需要一个iso即可安装，不需要让用户挂载其他磁盘等一大堆什么应用程序需要外面软盘。
28. ESP 分区必须得是 FAT32。
29. 一个文件只能干一件事，一个文件不能干出超过一件事的事情，错误示例：`src/apps/cwsl/shell.cpp`。

## 构建与运行

- 构建在 WSL2 中执行（脚本默认发行版 `Ubuntu-26.04`），工作目录必须是仓库内的 `src/`（Windows 路径先用 `wslpath` 换算，不要写死盘符）；Windows 脚本只做转发。
  ```bash
  cd src                   # 仓库的 src 目录（WSL 内）
  make -j4                 # HEADLESS=0（默认，带 GOP 图形）
  make run                 # 构建 out/data.img 并启动 QEMU
  make run QEMU_DISPLAY="-display gtk"   # WSLg 图形窗口
  make iso                 # 生成 UEFI + legacy BIOS 双启动 ISO：out/zhaoxu-os.iso
  make run-iso             # 从 ISO 启动（UEFI），并挂载 out/data.img
  make run-bios            # 从 ISO 启动（SeaBIOS 走 MBR 引导链），并挂载安装盘
  ```
  Windows 入口：`./src/build.ps1`、`./src/run.ps1`（PowerShell 7+），默认发行版 `Ubuntu-26.04`。
- 单个用户程序：`make out/hello.zxe`（内部先链接 `build/hello.uelf`，再由 `tools/zxe-pack.rb` 转成 `.zxe`）。模式规则会自动编译 `user/hello.c` 并链接 `user/userlib.c`，不需要改 Makefile。程序入口是 `int user_main(long *stack)`，不是 `main`；`userlib.c` 的 `_start` 会把返回值作为退出码。ELF→.zxe 转换需要 Ruby：WSL 内没有时通过 Makefile 的 `RUBY` 变量指向 Windows 侧 ruby.exe（按本机位置覆盖，不要写死盘符）。
- `make`/`make all` 的最终产物是 `out/BOOTX64.EFI` 与 `esp/EFI/BOOT/BOOTX64.EFI`，不会生成 `out/data.img`；测试文件系统时要 `make out/data.img` 或直接 `make run`。
- `make run` 默认 q35、512 MiB、smp=4、`-display none` + 串口 stdio，并把 `out/data.img` 挂为第二个 IDE 盘（`ide.1`），把安装介质 `out/efi.img` 挂为 `ide.2`、空白目标盘 `out/target.img`（64 MiB，首次创建）挂为 `ide.3`。带 `-no-reboot`，所以 Shell 的 `reboot` 会直接退出 QEMU。单核回归用 `make run QEMU_SMP=1`。
- `make iso` 生成 `out/zhaoxu-os.iso`：16 MiB FAT16 ESP（`out/efi.img`）内含 `EFI/BOOT/BOOTX64.EFI`，另带 legacy BIOS 的 `bios.img`，由 xorriso 以 El Torito 双入口 + GPT hybrid 打包；数据盘不会进入 ISO。`make run-iso` 从 ISO 启动并挂载 `out/data.img`。
- `out/bios.img` 是 1.44 MB 软盘仿真 El Torito 镜像：LBA 0 为 `boot/bios_stage1.asm`（512 字节、≤446 字节代码的 MBR/引导扇区），LBA 1 起为 `boot/bios_stage2.asm`，随后是内核；`tools/bios-img.rb` 把参数块 `ZBOOTPRM` 补成 kernel LBA/字节数/扇区数。stage 2 由 stage 1 载入 0x8000（CS=0），必须 ≤32 KiB；内核被整块读进 0x10000（≤448 KiB / 0x380 扇区，避开 EBDA），再由保护模式一次拷到 32 MiB 窗口。
- legacy BIOS 链：stage1 用 INT 13h CHS 读 stage2 → stage2 做 VBE 32bpp LFB 选择、E820、RSDP 扫描 → 读内核 → 建页表与 `boot_info` → 进长模式，契约与 UEFI 层一致。
- OVMF VARS 不能用只读的系统文件；`make run` 会先复制到可写的 `out/OVMF_VARS.fd`。OVMF 路径可用 `OVMF_CODE`/`OVMF_VARS` 覆盖。
- `HEADLESS=1` 只影响 FASM 引导层（跳过 GOP 探测与 UEFI ConOut），不改变内核/用户态代码；此时显示只能走串口。`build.ps1 -Headless` 就是设这个值。
- 依赖：`gcc`、`g++`、`gcc-14` + `gnat-14`（Ada 前端）、`ld.lld`、`objcopy`、`qemu-system-x86_64`、OVMF、`mkfs.vfat`、`mtools`、`tar`、`xorriso`。FASM 已随仓库放在 `src/tools/fasm`。
- 现有 `build.ps1`/`run.ps1`/`tools/install-fasm.sh` 是当前入口；新增脚本按规则 6 用 Ruby，迁移现有入口前先与用户确认。

## 验证

没有自动化测试；改动后至少做一次 QEMU 冒烟：

1. `make clean && make -j4` 干净重建，`nm -u out/kernel.elf` 应无未定义符号。
2. `make run` 后应直接进入 ADE 桌面：日志出现 `[proc] ... /bin/ade`、`[ade ] font /mnt/...`、`[ade ] Ring3 display server`，随后 `spawn /bin/apps/welcome.zxe` 与 `/bin/apps/calc.zxe` 并显示两个客户端窗口；点击欢迎窗口应回显 `已收到点击事件`，计算器按 `7 + 5 =` 应显示 `12`，关闭计算器后 Dock 回收对应图标。内核态 ADE 仅作为 `/bin/ade` 启动失败时的回退。无帧缓冲时仍按旧 Shell 流程测试 `ls /mnt`、`cat /mnt/HELLO.TXT`、`/mnt/HELLO.ZXE`（退出码 7）、`fonttest 0x4e2d`、`disks`、`install`、`shutdown`。
3. 用户态异常只应终止进程并回到 Shell（退出码 `-向量号`，如 `#PF` 为 `-14`）；内核异常才 panic。
4. ISO 改动：`make iso` 生成 `out/zhaoxu_{version}_installer.iso`（规则 11 命名），分别用 `make run-iso`（UEFI）与 `make run-bios`（SeaBIOS）从 ISO 启动并重复上面的 `/mnt` 测试；无数据盘时应打印 `[ttf ] no CJK font`，`run /mnt/...` 与中文回退为 `?`，不崩溃。
5. SMP/调度器回归：`make run QEMU_SMP=4` 应打印 `4/4 CPU(s) online` 与三行 `[sched] ... churn=12/12`；各 AP 的 worker 计数并行增长，`threads` 为 9、`heap` 三次采样一致（证明线程栈已回收）。再用 `QEMU_SMP=1` 验证单核回退，ADE 桌面与 Dock 应用仍正常。BIOS 路径应打印 `ACPI RSDP located.`；UEFI 与 BIOS 的 LAPIC timer 校准值在数千万 Hz 量级。
6. legacy BIOS 的 RSDP 由 stage2 在低端 1 MiB 按字节扫描（QEMU q35/SeaBIOS 会放在非规范位置）；内核 `apic_locate_rsdp` 再做一次兜底扫描，二者都找不到时退回单 CPU + PIT。
7. 帧缓冲与桌面：`make run QEMU_DISPLAY="-display gtk"` 人工查看；无头回归用 QEMU HMP `screendump /tmp/ade.ppm`（QEMU 输出 PPM，可用脚本转 PNG），检查菜单栏、窗口层叠、Dock 与 SVG 图标。`ruby src/tools/smoke.rb` 的 Shell 命令流程仅在无帧缓冲回退 csh 时有效；`SMOKE_BOOT` 可选 `esp`（默认）、`iso`、`iso-nodata`、`installed`；`SMOKE_FW` 可选 `uefi`（默认）与 `bios`。

## 架构与不变量

- 单镜像：引导层把 `out/kernel.bin` 嵌入 EFI（UEFI）或放进 `bios.img`/安装盘低端（BIOS）；当前 C 内核、Ada 驱动、C++ ADE 仍链接进内核，迁移目标是 ADE 与驱动都移到 Ring3（见 `docs/ARCHITECTURE.md`）。引导入口有两条：UEFI 是 `src/boot/efi_main.c`（C 编写，clang + lld-link 生成 PE32+ 应用，`src/boot/kernel_blob.S` 把内核嵌入镜像），legacy BIOS 是 `src/boot/bios_stage1.asm` → `src/boot/bios_stage2.asm`。UEFI 加载器先用 AllocateAddress 申请固定 16 MiB 的内核暂存区，失败则用 AllocateMaxAddress 保证仍落在 4 GiB 以下；页表池同样必须低于 4 GiB（写页表发生在切 CR3 之前）。自建页表时必须把 EFI 镜像区（Loaded Image Protocol 查基址，VMware 会把 EFI 应用装到 4 GiB 以上）、内核暂存区（含 +12 MiB 内存映射、+14 MiB boot_info）、GOP 帧缓冲（大显存机器常在 4 GiB 以上）和固件栈一并恒等映射，然后再切 CR3——否则切完取指/写像素即 #PF。`vmm_init` 之后再把恒等映射扩展到内存映射覆盖的 64 GiB。AHCI/PCIe 等 MMIO BAR 仍按 4 GiB 以下使用。
- `struct boot_info` 的字段顺序必须与 `src/boot/efi_main.c` 和 `src/boot/bios_stage2.asm` 同步，magic 为 `0x5A4F5331`。两层都在物理窗口 `+12 MiB` 放内存映射、`+14 MiB` 放 `boot_info`；`kernel/linker.ld` 的 `ASSERT` 只保证内核结束于 `+14 MiB` 之前，内核显著增大时要同时检查这两个偏移。BIOS 层把 E820 转成 48 字节 UEFI 描述符（type 1→7，其余→2），并必须把 16 MiB 内核窗口整体清零（内核 `.bss` 不会由引导层外的代码清）。
- 地址布局：内核 VMA `0xffffffff80000000`，内核栈顶 `0xffffffff80f00000`，内核堆 `0xffffffff90000000`；低端 `0..64 GiB` 是物理内存恒等映射（loader 建 4 GiB，`vmm_init` 按内存映射补到 64 GiB），用户程序基址 `0x10000000000`（1 TiB，必须避开恒等映射区），用户栈顶 `0x10100000000`（32 页）。内核栈顶在 `boot/efi_main.c`、`kernel/linker.ld`、`arch/x86_64/entry.S`、`arch/x86_64/gdt.c`（TSS `rsp0`）、`proc/process.c` 五处重复，改布局必须同步。
- 多进程：每个进程独立 CR3（内核高半区共享，用户窗口 PML4 slot 2 私有）、独立内核栈和抢占线程；`spawn/exit/wait` 已实现，调度器切换 CR3 与 TSS rsp0。调度交接用 `on_cpu` 标记，旧线程完全切走后才允许其他 CPU 重新选中；新线程以关中断状态起步，`thread_start` 完成交接后再 `sti`。
- 抢占式多线程调度器：`kernel/proc/thread.c` 维护共享线程表（32 槽），BSP 用 PIT、AP 用 LAPIC timer 以 1 kHz 触发 `sched_tick`，量子 `SCHED_QUANTUM_TICKS=10`。`arch/x86_64/switch.S` 保存/恢复 callee-saved 寄存器与 RFLAGS；线程栈 16 KiB，退出线程由下一次上下文切换在 `sched_reap_local` 中释放。`kthread_create` 的 `affinity` 为 -1 表示可在任意 CPU 运行，主线程固定 affinity 0。
- SMP：`arch/x86_64/apic.c` 解析 RSDP/XSDT/MADT（`apic_locate_rsdp` 在内核侧兜底扫描低端 1 MiB）；`arch/x86_64/smp.c` 把 `ap_trampoline.S` 复制到物理 `0x7000`，以 INIT-SIPI-SIPI 启动 AP。AP 必须在 `ap_entry` 中重新加载 GDT 和 IDT，并使用自己的 TSS（选择子 `0x28 + 16*cpu`），否则第一个中断就是三重故障。`arch/x86_64/percpu.c` 按 APIC id 索引每 CPU 数据；PIC 设备中断只留在 BSP（LINT0=ExtINT），AP 的 LINT0/1 屏蔽。
- 并发保护：`spinlock.h` 提供 `spin_lock_irqsave`；PMM、堆、控制台输出和调度器共享状态都必须在锁内操作，中断上下文只使用 irqsave 变体，避免同 CPU 抢占自锁。
- 桌面分层（规则 22/23/24）：开机在图形模式直接 `spawn /bin/ade`。`src/apps/ADE/main.cpp` 只做入口、应用注册与 `ade.h` API 转发，壁纸/菜单栏/Dock 分别在 `wallpaper.cpp`/`menubar.cpp`/`dock.cpp`，窗口协议在 `display/server.cpp`；`src/apps/ADE/winmgr/dwm.cpp` 独占窗口装饰、z 序、拖动/关闭、焦点、损伤合成与输入。应用在 `src/apps/deskapps/`（仍为内核回退路径），独立客户端在 `src/user/` 与 `src/user/lib/`，只依赖 `ade_protocol.h` + `ade_client.h`；图标为内联 SVG，由 `kernel/gfx/svg.c` 光栅化。所有绘制经 `kernel/gfx/render.c`（ZOS API 1.0，见 `zos_api.h`），Ring3 复用同一份实现；DWM/ADE 不直接访问帧缓冲像素。字体由系统字体引擎（`third_party/stb/stb_truetype.h` v1.26 原文，经**同一份** `kernel/font/stbtt.c` 适配内核与 Ring3）加载 MiSans/MiSans Global/Inter，最后回退 WQY.TTC。
- 迁移中：ADE 已作为 `/bin/ade` 在 Ring3 运行，内核保留内核态 ADE 作为 spawn 失败时的回退；Welcome 与 Calculator 已迁移为独立 `.zxe` 客户端，P3 继续迁移 Files/Monitor，P4 再移驱动。显示协议在 `user/lib/include/ade_protocol.h`，服务端在 `src/apps/ADE/display/server.cpp`，客户端库在 `src/user/lib/ade_client.c`。
- 系统调用 ABI：`int 0x80`，`rax`=调用号，`rdi/rsi/rdx/r10`=参数；0=exit、1=write、2=getpid、3=open、4=read、5=close、6=uptime、7=spawn、8=wait、9=port_listen、10=port_connect、11=port_accept、12=port_send、13=port_recv、14=port_close、15=shm_create、16=shm_map、17=shm_unmap、18=shm_close、19=fb_map、20=mouse、21=key、22=sleep、23=cpus、24=rtc、25=read_path、26=port_try_accept、27=port_try_recv、28=serial_write。调用号在 `kernel/proc/process.c` 与 `user/userlib.h` 各一份，必须同步；用户指针按当前进程地址空间逐页校验。
- 用户线程经 `user_enter` `iretq` 进 Ring3；退出或用户异常调用 `process_exit_current()` → `kthread_exit()`，不再通过 `user_resume()` 返回。只有 `process_handle_exception` 声明 `noreturn`。
- 用户 ELF 必须用 `src/user/user.ld` 链接到 `0x10000000000`（1 TiB）；该基址在 `process.c` 的 `USER_LOAD_BASE`、链接脚本、`tools/zxe-pack.rb` 与 `apps/zcc/main.cpp` 同步。用户代码用 `-mcmodel=large`，与内核一样禁用 SSE/red zone。
- 内核是 freestanding：无 libc，常规文件禁用浮点/SSE/MMX/AVX（`-mgeneral-regs-only`）；`kernel/font/ttf.c`、`kernel/font/stbtt.c` 在内核通过 `FONT_CFLAGS`，Ring3 用**同一份**源码通过 `ADE_FONT_CFLAGS` 启用 SSE2/x87 供 stb_truetype 使用。C++ 关闭异常、RTTI、threadsafe statics，不要用 STL/iostream。字符串/打印用 `kernel/lib` 里的实现。
- initramfs 源在 `src/initramfs/`，`src/Makefile` 的 `RAMFS_FILES` 是打包白名单；新增文件必须同时改 `RAMFS_FILES`。`src/initramfs/data/` 只用于生成 FAT32 数据盘，不进入 initramfs。
- 存储仅 AHCI；FAT32 只读、按卷标 `ZHAOXUDATA` 选盘并挂到 `/mnt`，`/` 是 initramfs；VFS 只提供只读 fd。没有数据盘时 `/mnt/...` 自动回退到 initramfs 内嵌镜像 `/data/...`（`RAMFS_FILES` 的 data 条目，名字必须用大写 8.3 风格与 FAT 卷一致），纯 ISO/无盘启动也能打开示例文件；字体不在镜像内（OVMF 拒绝启动 ~5MB 的 BOOTX64.EFI），无盘时中文回退 `?`。
- 安装器（`kernel/fs/install.c`）不解析 FAT：源是卷标 `ZHAOXUEFI` 的 FAT16 ESP 镜像盘，目标是扇区 0 全零或旧 Zhaoxu MBR（55AA + 类型 0x0E + 起始 LBA 2048）的空白盘；写入的盘 UEFI/BIOS 双启动：MBR = stage1 前 446 字节 + 单个 0x0E 分区表项（起始 LBA 2048），LBA 1 起写 stage2，随后是运行中的内核，LBA 1..2047 的空隙清零，ESP 按扇区整盘复制到 LBA 2048，校验起始扇区后 flush。内核必须放得下分区前空隙（<2048 扇区）且 ≤448 KiB（BIOS stage2 低端缓冲上限）。安装介质盘与 `ZHAOXUDATA` 盘永不作为目标（`install_scan` 的 `is_target` 与 `install_run` 双重校验）。
- AHCI DMA 用物理地址，依赖内核的物理内存恒等映射（低 4 GiB + `vmm_init` 扩展的高内存）；命令头 `prdtl` 在 offset 2（`opts | prdtl << 16`），不要写到 `status`。无数据命令（FLUSH）必须保持 `prdtl = 0`。
- Ada 驱动在 `src/drivers/*.ads` 用 `pragma Export, Convention => C` 导出符号；端口 I/O 走 C 侧 `outb_port`/`inb_port`。编译带 `-gnatp` 且不链接 libgnat，避免依赖 GNAT 运行时。
- `build/`、`out/`、`esp/` 是生成物，不要手改。

## 版本与发布

- 版本号唯一来源是仓库根的 `version.php`。改版本只改这个文件，然后在 `src/` 运行 `make version`（Ruby 脚本 `tools/version.rb` 同步 `src/kernel/include/version.h`、`src/initramfs/etc/motd`、`src/initramfs/etc/version`、`README.md`）；C/C++ 代码统一 include `<version.h>` 使用 `ZHAOXU_VERSION`/`ZHAOXU_CODENAME`，不要手工单独改派生文件。
- 三个远端分支不同：GitHub `main`、Gitee `master`、GitCode `main`。
- WSL 内没有 Git 凭据，push 会报 `could not read Username`。从 Windows Git 推送：
  ```powershell
  # 在仓库根目录执行（PowerShell，PSCommandPath 所在目录）
  git push github main
  git push gitee main:master
  git push gitcode main
  ```
- 没有 CI；推送后用 `git ls-remote` 核对三个远端指向同一提交。
