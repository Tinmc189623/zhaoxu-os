# Changelog

Zhaoxu OS 全部版本记录。当前版本的功能说明见 [README.md](README.md)；版本号唯一来源是 `version.cfg`。

## 0.6.0 「Yuanshuo」

### 构建与版本
- 构建支持原生 Linux、原生 macOS、原生 Windows（MSYS2/MinGW）与 WSL2；Ruby 入口 `tools/host.rb` / `build.rb` / `run.rb` 统一探测宿主、工具链、OVMF 与 QEMU
- Makefile 跨平台：`ZOS_HOST` 检测、macOS/Windows 使用 clang `--target=x86_64-elf` + lld + llvm-objcopy、可移植的 dd/tar/stat、按宿主 OVMF 与镜像工具、`--check` 工具矩阵
- `version.cfg` 成为唯一版本源，`os_version` 与 `kernel_version` 分离；`version.h` 新增 `ZHAOXU_KERNEL_VERSION`，ISO 元数据同时写入 OS 与内核版本
- 新增构建宿主检测单元测试 `tests/tools/host_test.rb` 与 `make host-test`

### 存储与文件系统
- ext4 核心从内核剥离为 OS 无关的 `src/fs/ext4`，带 host 测试与 Ruby 格式化矩阵
- 新增 Ring3 `/bin/ext4fs` 服务：块 RPC、读写/分配/格式化、超时与请求加固
- `/mnt` 由 VFS 转发到 Ring3 ext4 服务；安装器现场构建 ext4 根，内核可从已安装 ext4 根启动
- 安装目标盘支持 MBR 与 GPT，保留 FAT32 ESP 与 `ZHAOXUDATA` 数据卷

### 引导与安装
- UEFI 拆成两段：`ZhaoxuBootMgr`（BOOTX64.EFI）→ `OSLDRX64.ELF` → `kernel.elf`
- legacy BIOS 改为 no-emulation El Torito ISO，stage2 参数块由 `tools/iso-bios.rb` 回填
- ISO 介质按文档生成 casper/dists/pool/.disk/preseed 与 md5sum 元数据
- 单 ISO 自包含安装（规则 27），不依赖额外介质

### 图形与桌面
- ZAF Z0：后端中立 `zaf_gfx` 光栅接口、软件后端与 Ring3 fps 演示；补齐 ZAF 架构与 Z1 设计文档
- ADE 后缓冲合成，只提交损伤区域；菜单栏、About 面板与真实 SVG 图标
- 默认壁纸（rule 33）在构建期转成原始帧，启动时从数据卷或 ISO 加载，无盘回退 Aqua 渐变
- Ring3 ADE 显示服务器、计算器迁移为独立客户端；截图工具可通过 HMP 驱动点击

### 运行时与可执行格式
- ZXE v1 原生格式：加载器、打包器、W^X 段权限、ASLR 映像基址与 `/lib` 动态依赖
- stb_truetype 在内核与 Ring3 共用同一份适配代码
- SMP AP 启动同步与 LAPIC 延时修正；硬件驱动全部下沉 Ring3

### 修复
- ext4 损坏元数据、陈旧/伪造 RPC 与未对齐目录项校验
- 安装失败清理、GPT LastUsable LBA、BIOS El Torito 目录校验
- 安装器进度条启动前显示 0%

## 0.5.0 「Yuanshuo」

### 桌面与安装
- ADE 图形桌面（Aqua 风格）：渐变菜单栏、RTC 时钟、Dock、软件光标
- DWM 窗口管理器：拖动、关闭、层叠、焦点与键盘路由
- 原生桌面应用：终端（可直接执行 csh 命令）、文件、计算器、便签、监视器
- 图形安装器：识别安装源 `ZHAOXUEFI` 与空白目标盘，AHCI 写入 MBR + FAT32 ESP，进度与结果提示
- Shell `install [disk]` 与 `disks`：安装介质、数据盘、目标盘角色识别
- PS/2 鼠标 IRQ12 三字节包解析，窗口与安装按钮可点击

### 系统内开发
- `edit`：系统内 C 编辑器，支持示例程序、`.list`、`.run`、`.clear`、`.end`、`.quit`
- `cc`：编译编辑器缓冲区并立即以 ring 3 运行
- `zcc`：C 子集编译器，直接生成 `.zxe` v1 机器码镜像（int 变量、print/printnum、if/else、while、比较与四则运算、return）
- Shell 提示符在编辑模式下切换为 `edit>`

### 驱动与平台
- ACPI 驱动迁移到 Ada：RSDP 校验、RSDT/XSDT 遍历、FADT 解析、DSDT `_S5` AML 解析、关机/重启回退链；C 侧只保留端口 I/O、日志与 CPU 控制胶水
- PCI/PCIe 枚举、AHCI、IDE（PIO）、SATA（链路状态）、SCSI（ATAPI）驱动以 Ada 实现
- Shell 新增 `ata`、`sata`、`scsi`、`ps` 命令

### 修复
- 便签窗口按 UTF-8 码点绘制文本，不再把中文拆成单字节导致乱码；退格按完整字符删除
- 桌面高分辨率下窗口布局与字号自适应（1920×1080 实机验证）

## 0.4.0

- TrueType 中文渲染：TTC/sfnt 解析、cmap format 4/12、26.6 定点扫描线光栅化、256 槽字形缓存
- 控制台 UTF-8 解码，西文 8×8 点阵、中文矢量字形；文泉驿微米黑随数据盘分发
- 自研 `.zxe` 可执行格式：32 字节头 + 40 字节段表 + 原始数据，页对齐、R/W/X 段标志
- 纯 Ruby 转换器 `tools/zxe-pack.rb`：ELF64 → `.zxe`
- 加载器按魔数自动分发 `.zxe` 与 ELF64，ELF 兼容路径保留
- Shell 新增 `fonttest <hex>`：任意码点光栅结果的 ASCII 密度图

## 0.3.0

- ring 3 用户进程：ELF64 加载、用户页映射、进程内核栈与 TSS `rsp0`
- `int 0x80` 系统调用 ABI：exit/write/getpid/open/read/close/uptime
- ustar initramfs 嵌入内核，作为只读 ramfs 根文件系统
- PCI → AHCI（SATA）块设备驱动，IDENTIFY / READ DMA EXT
- FAT32 只读驱动挂载到 `/mnt`，支持 LFN、子目录与簇链；按卷标 `ZHAOXUDATA` 选盘
- VFS 统一 `/`（ramfs）与 `/mnt`（FAT32），只读文件描述符
- 启动时自动以 ring 3 运行 `/bin/init`；Shell 新增 `ls`、`cat`、`run`、`mounts`
- 用户态异常只终止进程并回到 Shell，内核异常 panic

## 0.2.0

- 首个可启动版本：UEFI 引导进入 higher-half C 内核
- `boot_info` 契约：帧缓冲、内存映射、RSDP
- GDT/TSS（双故障与 NMI 独立 IST 栈）、IDT、8259 PIC
- PIT 节拍、PS/2 键盘 IRQ、COM1 串口输入输出
- 物理页位图分配器、4 KiB 虚拟映射、内核堆
- `kprintf` / `ksnprintf`、字符串库、图形 + 串口双路控制台
- csh 基础命令、异常现场打印、CPUID/RTC/PCI 信息

## 0.1.0

- 仓库初始化与工具链约定（WSL2、多语言分工、Ruby 脚本）
- 引导层与内核骨架、链接脚本、构建系统
- 控制台、字符串与打印基础设施
