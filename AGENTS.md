# Zhaoxu OS（内核 Yuanshuo）Agent 指南

## 规则（必须遵守）

1. 内核名称：`Yuanshuo Kernel`，操作系统名称：`Zhaoxu OS`，Shell：`Command-Shell`，桌面：`Desktop-Shell`（风格：Aqua），窗口管理器：`DS-WM`。
2. Github 仓库（main）：https://github.com/Tinmc189623/zhaoxu-os
3. Gitee 仓库（master）：https://gitee.com/Tinmc189623/zhaoxu-os
4. GitCode 仓库（main）：https://gitcode.com/2503_94276351/zhaoxu-os
5. 用 WSL2，可用：C（内核+UEFI引导）、C++（上层应用）、GAS（底层）、FASM（BIOS引导）、SPARK（驱动）。
6. 脚本必须完全使用 ruby 来编写，必须完全使用 AI 来生成代码。
7. 引导加载器必须支持 uefi 和 legacy bios 两种启动模式。 
8. 引导加载器的名称必须得是：`ZhaoxuBootloader`。
9. 内核必须得是宏内核，用户程序必须得放在 Ring3，内核的二进制必须得是干净的二进制，不允许往里投放什么驱动/应用。

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
- `make run` 默认 q35、512 MiB、smp=1、`-display none` + 串口 stdio，并把 `out/data.img` 挂为第二个 IDE 盘（`ide.1`），把安装介质 `out/efi.img` 挂为 `ide.2`、空白目标盘 `out/target.img`（64 MiB，首次创建）挂为 `ide.3`。带 `-no-reboot`，所以 Shell 的 `reboot` 会直接退出 QEMU。
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
2. `make run` 后确认自动运行 ring 3 `/bin/init`，再在 Shell 执行 `ls /mnt`、`cat /mnt/HELLO.TXT`、`run /mnt/HELLO.ZXE`（退出码 7）、`run /mnt/HELLO.ELF`（ELF 兼容路径，退出码 7）、`fonttest 0x4e2d`（应打出 `中` 的 ASCII 密度图）、`disks`（应标出 media/data/target 三个角色）、`install`（应安装到 `target` 盘并打印成功）、`desktop 2`（图形桌面 2 秒后自动退出并回到 Shell）、`shutdown`。
3. 用户态异常只应终止进程并回到 Shell（退出码 `-向量号`，如 `#PF` 为 `-14`）；内核异常才 panic。
4. ISO 改动：`make iso` 生成 `out/zhaoxu-os.iso`，分别用 `make run-iso`（UEFI）与 `make run-bios`（SeaBIOS）从 ISO 启动并重复上面的 `/mnt` 测试；无数据盘时应打印 `[ttf ] no CJK font`，`run /mnt/...` 与中文回退为 `?`，不崩溃。
5. 帧缓冲中文/字形与桌面效果：`make run QEMU_DISPLAY="-display gtk"` 人工查看，或 `SHOT_CMD="desktop install" ruby src/tools/screenshot.rb`（无头进入桌面并经 HMP `screendump` 抓 `out/desk.png`）。无头回归用 `ruby src/tools/smoke.rb <marker> [commands]`（Windows Ruby 驱动 WSL QEMU，按提示符逐条送命令）；`SMOKE_BOOT` 可选 `esp`（默认）、`iso`、`iso-nodata`、`installed`（只从 `out/target.img` 启动，验证安装结果可被引导）；`SMOKE_FW` 可选 `uefi`（默认）与 `bios`（SeaBIOS，仅支持 `iso`/`iso-nodata`/`installed`）。

## 架构与不变量

- 单镜像：引导层把 `out/kernel.bin` 嵌入 EFI（UEFI）或放进 `bios.img`/安装盘低端（BIOS）；C 内核、Ada 驱动、C++ Shell 全部链接进内核，只有 ring 3 程序是独立 ELF。引导入口有两条：UEFI 是 `src/boot/main.asm`，legacy BIOS 是 `src/boot/bios_stage1.asm` → `src/boot/bios_stage2.asm`。
- `struct boot_info` 的字段顺序必须与 `src/boot/main.asm` 和 `src/boot/bios_stage2.asm` 同步，magic 为 `0x5A4F5331`。两层都在物理窗口 `+12 MiB` 放内存映射、`+14 MiB` 放 `boot_info`；`kernel/linker.ld` 的 `ASSERT` 只保证内核结束于 `+14 MiB` 之前，内核显著增大时要同时检查这两个偏移。BIOS 层把 E820 转成 48 字节 UEFI 描述符（type 1→7，其余→2），并必须把 16 MiB 内核窗口整体清零（内核 `.bss` 不会由引导层外的代码清）。
- 地址布局：内核 VMA `0xffffffff80000000`，内核栈顶 `0xffffffff80f00000`，内核堆 `0xffffffff90000000`；用户程序基址 `0x100000000`，用户栈顶 `0x200000000`（32 页）。内核栈顶在 `boot/main.asm`、`kernel/linker.ld`、`arch/x86_64/entry.S`、`arch/x86_64/gdt.c`（TSS `rsp0`）、`proc/process.c` 五处重复，改布局必须同步。
- ring 3 一次只跑一个进程，共享内核页表，靠页级 `U/S` 隔离；无调度器、fork/exec、独立地址空间。
- 系统调用 ABI：`int 0x80`，`rax`=调用号，`rdi/rsi/rdx`=参数；0=exit、1=write、2=getpid、3=open、4=read、5=close、6=uptime。调用号在 `kernel/proc/process.c` 与 `user/userlib.h` 各一份，必须同步；用户指针按页校验。
- `user_enter` 不能标 `noreturn`：它保存内核上下文后 `iretq` 进 ring 3，退出或用户异常时 `user_resume()` 恢复 `g_kernel_rsp` 并返回原调用点。只有 `process_handle_exception` 是 `noreturn`。
- 用户 ELF 必须用 `src/user/user.ld` 链接到 `0x100000000`；该基址在 `process.c` 的 `USER_LOAD_BASE` 与链接脚本两处定义。用户代码用 `-mcmodel=large`，与内核一样禁用 SSE/red zone。
- 内核是 freestanding：无 libc，禁用浮点/SSE/MMX/AVX（`-mgeneral-regs-only`）；C++ 关闭异常、RTTI、threadsafe statics，不要用 STL/iostream。字符串/打印用 `kernel/lib` 里的实现。
- initramfs 源在 `src/initramfs/`，`src/Makefile` 的 `RAMFS_FILES` 是打包白名单；新增文件必须同时改 `RAMFS_FILES`。`src/initramfs/data/` 只用于生成 FAT32 数据盘，不进入 initramfs。
- 存储仅 AHCI；FAT32 只读、按卷标 `ZHAOXUDATA` 选盘并挂到 `/mnt`，`/` 是 initramfs；VFS 只提供只读 fd。没有数据盘时 `/mnt/...` 自动回退到 initramfs 内嵌镜像 `/data/...`（`RAMFS_FILES` 的 data 条目，名字必须用大写 8.3 风格与 FAT 卷一致），纯 ISO/无盘启动也能打开示例文件；字体不在镜像内（OVMF 拒绝启动 ~5MB 的 BOOTX64.EFI），无盘时中文回退 `?`。
- 安装器（`kernel/fs/install.c`）不解析 FAT：源是卷标 `ZHAOXUEFI` 的 FAT16 ESP 镜像盘，目标是扇区 0 全零或旧 Zhaoxu MBR（55AA + 类型 0x0E + 起始 LBA 2048）的空白盘；写入的盘 UEFI/BIOS 双启动：MBR = stage1 前 446 字节 + 单个 0x0E 分区表项（起始 LBA 2048），LBA 1 起写 stage2，随后是运行中的内核，LBA 1..2047 的空隙清零，ESP 按扇区整盘复制到 LBA 2048，校验起始扇区后 flush。内核必须放得下分区前空隙（<2048 扇区）且 ≤448 KiB（BIOS stage2 低端缓冲上限）。安装介质盘与 `ZHAOXUDATA` 盘永不作为目标（`install_scan` 的 `is_target` 与 `install_run` 双重校验）。
- AHCI DMA 用物理地址，依赖引导层对前 4 GiB 的恒等映射；命令头 `prdtl` 在 offset 2（`opts | prdtl << 16`），不要写到 `status`。无数据命令（FLUSH）必须保持 `prdtl = 0`。
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
