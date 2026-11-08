# Zhaoxu OS（内核 Yuanshuo）Agent 指南

## 规则（必须遵守）

1. 内核名称：`Yuanshuo Kernel`，操作系统名称：`Zhaoxu OS`，Shell：`Command-Shell`，桌面：`Desktop-Shell`（风格：Aqua），窗口管理器：`DS-WM`。
2. Github 仓库（main）：https://github.com/Tinmc189623/zhaoxu-os
3. Gitee 仓库（master）：https://gitee.com/Tinmc189623/zhaoxu-os
4. GitCode 仓库（main）：https://gitcode.com/2503_94276351/zhaoxu-os
5. 用 WSL2，可用：C（底层）、C++（上层应用）、FASM+GAS（底层）、Ada+SPARK（驱动）。
6. 脚本必须完全使用 ruby 来编写，必须完全使用 AI 来生成代码。
7. 引导加载器必须支持 uefi 和 legacy bios 两种启动模式。 
8. 引导加载器的名称必须得是：`ZhaoxuBootloader`。

## 构建与运行

- 构建在 WSL2 中执行（脚本默认发行版 `Ubuntu-26.04`），工作目录必须是仓库内的 `src/`（Windows 路径先用 `wslpath` 换算，不要写死盘符）；Windows 脚本只做转发。
  ```bash
  cd src                   # 仓库的 src 目录（WSL 内）
  make -j4                 # HEADLESS=0（默认，带 GOP 图形）
  make run                 # 构建 out/data.img 并启动 QEMU
  make run QEMU_DISPLAY="-display gtk"   # WSLg 图形窗口
  make iso                 # 生成 UEFI 启动 ISO：out/zhaoxu-os.iso
  make run-iso             # 从 ISO 启动，并挂载 out/data.img
  ```
  Windows 入口：`./src/build.ps1`、`./src/run.ps1`（PowerShell 7+），默认发行版 `Ubuntu-26.04`。
- 单个用户程序：`make out/hello.zxe`（内部先链接 `build/hello.uelf`，再由 `tools/zxe-pack.rb` 转成 `.zxe`）。模式规则会自动编译 `user/hello.c` 并链接 `user/userlib.c`，不需要改 Makefile。程序入口是 `int user_main(long *stack)`，不是 `main`；`userlib.c` 的 `_start` 会把返回值作为退出码。ELF→.zxe 转换需要 Ruby：WSL 内没有时通过 Makefile 的 `RUBY` 变量指向 Windows 侧 ruby.exe（按本机位置覆盖，不要写死盘符）。
- `make`/`make all` 的最终产物是 `out/BOOTX64.EFI` 与 `esp/EFI/BOOT/BOOTX64.EFI`，不会生成 `out/data.img`；测试文件系统时要 `make out/data.img` 或直接 `make run`。
- `make run` 默认 q35、512 MiB、smp=1、`-display none` + 串口 stdio，并把 `out/data.img` 挂为第二个 IDE 盘（`ide.1`）。带 `-no-reboot`，所以 Shell 的 `reboot` 会直接退出 QEMU。
- `make iso` 生成 `out/zhaoxu-os.iso`：16 MiB FAT16 ESP（`out/efi.img`）内含 `EFI/BOOT/BOOTX64.EFI`，由 xorriso 以 UEFI El Torito + GPT hybrid 打包；数据盘不会进入 ISO。`make run-iso` 从 ISO 启动并挂载 `out/data.img`。
- OVMF VARS 不能用只读的系统文件；`make run` 会先复制到可写的 `out/OVMF_VARS.fd`。OVMF 路径可用 `OVMF_CODE`/`OVMF_VARS` 覆盖。
- `HEADLESS=1` 只影响 FASM 引导层（跳过 GOP 探测与 UEFI ConOut），不改变内核/用户态代码；此时显示只能走串口。`build.ps1 -Headless` 就是设这个值。
- 依赖：`gcc`、`g++`、`gcc-14` + `gnat-14`（Ada 前端）、`ld.lld`、`objcopy`、`qemu-system-x86_64`、OVMF、`mkfs.vfat`、`mtools`、`tar`、`xorriso`。FASM 已随仓库放在 `src/tools/fasm`。
- 现有 `build.ps1`/`run.ps1`/`tools/install-fasm.sh` 是当前入口；新增脚本按规则 6 用 Ruby，迁移现有入口前先与用户确认。

## 验证

没有自动化测试；改动后至少做一次 QEMU 冒烟：

1. `make clean && make -j4` 干净重建，`nm -u out/kernel.elf` 应无未定义符号。
2. `make run` 后确认自动运行 ring 3 `/bin/init`，再在 Shell 执行 `ls /mnt`、`cat /mnt/HELLO.TXT`、`run /mnt/HELLO.ZXE`（退出码 7）、`run /mnt/HELLO.ELF`（ELF 兼容路径，退出码 7）、`fonttest 0x4e2d`（应打出 `中` 的 ASCII 密度图）、`shutdown`。
3. 用户态异常只应终止进程并回到 Shell（退出码 `-向量号`，如 `#PF` 为 `-14`）；内核异常才 panic。
4. ISO 改动：`make iso` 生成 `out/zhaoxu-os.iso`，再用 `make run-iso` 从 ISO 启动并重复上面的 `/mnt` 测试；无数据盘时应打印 `[ttf ] no CJK font`，`run /mnt/...` 与中文回退为 `?`，不崩溃。
5. 帧缓冲中文/字形效果无法无头验证：需要 `make run QEMU_DISPLAY="-display gtk"` 人工查看。无头回归可用 `ruby src/tools/smoke.rb <marker> [commands]`（Windows Ruby 驱动 WSL QEMU，按提示符逐条送命令）。

## 架构与不变量

- 单镜像：FASM 引导层把 `out/kernel.bin` 嵌入 EFI；C 内核、Ada 驱动、C++ Shell 全部链接进内核，只有 ring 3 程序是独立 ELF。引导层唯一入口是 `src/boot/main.asm`。
- `struct boot_info` 的字段顺序必须与 `src/boot/main.asm` 同步，magic 为 `0x5A4F5331`。引导层在物理窗口 `+12 MiB` 放 UEFI 内存映射、`+14 MiB` 放 `boot_info`；`kernel/linker.ld` 的 `ASSERT` 只保证内核结束于 `+14 MiB` 之前，内核显著增大时要同时检查这两个偏移。
- 地址布局：内核 VMA `0xffffffff80000000`，内核栈顶 `0xffffffff80f00000`，内核堆 `0xffffffff90000000`；用户程序基址 `0x100000000`，用户栈顶 `0x200000000`（32 页）。内核栈顶在 `boot/main.asm`、`kernel/linker.ld`、`arch/x86_64/entry.S`、`arch/x86_64/gdt.c`（TSS `rsp0`）、`proc/process.c` 五处重复，改布局必须同步。
- ring 3 一次只跑一个进程，共享内核页表，靠页级 `U/S` 隔离；无调度器、fork/exec、独立地址空间。
- 系统调用 ABI：`int 0x80`，`rax`=调用号，`rdi/rsi/rdx`=参数；0=exit、1=write、2=getpid、3=open、4=read、5=close、6=uptime。调用号在 `kernel/proc/process.c` 与 `user/userlib.h` 各一份，必须同步；用户指针按页校验。
- `user_enter` 不能标 `noreturn`：它保存内核上下文后 `iretq` 进 ring 3，退出或用户异常时 `user_resume()` 恢复 `g_kernel_rsp` 并返回原调用点。只有 `process_handle_exception` 是 `noreturn`。
- 用户 ELF 必须用 `src/user/user.ld` 链接到 `0x100000000`；该基址在 `process.c` 的 `USER_LOAD_BASE` 与链接脚本两处定义。用户代码用 `-mcmodel=large`，与内核一样禁用 SSE/red zone。
- 内核是 freestanding：无 libc，禁用浮点/SSE/MMX/AVX（`-mgeneral-regs-only`）；C++ 关闭异常、RTTI、threadsafe statics，不要用 STL/iostream。字符串/打印用 `kernel/lib` 里的实现。
- initramfs 源在 `src/initramfs/`，`src/Makefile` 的 `RAMFS_FILES` 是打包白名单；新增文件必须同时改 `RAMFS_FILES`。`src/initramfs/data/` 只用于生成 FAT32 数据盘，不进入 initramfs。
- 存储仅 AHCI；FAT32 只读、按卷标 `ZHAOXUDATA` 选盘并挂到 `/mnt`，`/` 是 initramfs；VFS 只提供只读 fd。没有数据盘时 `/mnt/...` 自动回退到 initramfs 内嵌镜像 `/data/...`（`RAMFS_FILES` 的 data 条目，名字必须用大写 8.3 风格与 FAT 卷一致），纯 ISO/无盘启动也能打开示例文件；字体不在镜像内（OVMF 拒绝启动 ~5MB 的 BOOTX64.EFI），无盘时中文回退 `?`。
- AHCI DMA 用物理地址，依赖引导层对前 4 GiB 的恒等映射；命令头 `prdtl` 在 offset 2（`opts | prdtl << 16`），不要写到 `status`。
- Ada 驱动在 `src/drivers/*.ads` 用 `pragma Export, Convention => C` 导出符号；端口 I/O 走 C 侧 `outb_port`/`inb_port`。编译带 `-gnatp` 且不链接 libgnat，避免依赖 GNAT 运行时。
- `build/`、`out/`、`esp/` 是生成物，不要手改。

## 版本与发布

- 版本号唯一来源是仓库根的 `version.php`。改版本只改这个文件，然后在 `src/` 运行 `make version`（Ruby 脚本 `tools/version.rb` 同步 `README.md`、`src/apps/shell.cpp`、`src/kernel/main.c`、`src/initramfs/etc/motd`、`src/initramfs/etc/version`）；不要手工单独改这些派生文件。
- 三个远端分支不同：GitHub `main`、Gitee `master`、GitCode `main`。
- WSL 内没有 Git 凭据，push 会报 `could not read Username`。从 Windows Git 推送：
  ```powershell
  # 在仓库根目录执行（PowerShell，PSCommandPath 所在目录）
  git push github main
  git push gitee main:master
  git push gitcode main
  ```
- 没有 CI；推送后用 `git ls-remote` 核对三个远端指向同一提交。
