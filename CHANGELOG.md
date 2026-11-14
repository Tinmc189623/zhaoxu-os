# Changelog

Zhaoxu OS 全部版本记录。当前版本的功能说明见 [README.md](README.md)；版本号唯一来源是 `version.cfg`。

## 0.7.1 「Yuanshuo」 · build 1

### 构建
- Makefile 成为唯一构建入口，Ruby 只保留辅助脚本（ZXE 打包、版本同步、ISO 元数据、冒烟/截图、宿主探测）
- Windows 默认在 WSL2 内直接 `make`；PowerShell 入口同步改为 WSL make，原生 MSYS2 仅在 `-HostMode native` 时使用
- Ada 编译器探测会检查 `-print-prog-name=gnat1` 是否真实存在；`gcc-16` 没有 `gnat-16` 时自动回退到可用的 `gcc-14`，不再出现 `cannot execute gnat1`
- 切换原生/WSL 构建时把 Windows 风格 `.d` 里的反斜杠改写为 `/`，保留头文件依赖；修复 `version.h` 变更后 `aqua`/`8sh` 仍显示旧版本号的问题
- 内核内嵌 initramfs 改为 ZIR1 + zlib，启动时解压；legacy BIOS 的 `install/KERNEL.BIN` 保持在 stage 2 的 448 KiB 内

### 桌面
- 新增 `boot_wallpaper` 系统调用：live ISO 直接使用 rule 33 壁纸，不再退回纯渐变
- 窗口内容内缩 8 px，标题栏渐变避开圆角，修复内容覆盖窗口圆角的穿模
- 初始窗口改为左右并排，不再让计算器盖住欢迎窗口
- 菜单栏改为半透明，窗口标题栏区分焦点/非焦点，阴影与内边距统一

### 显示
- 新增 M2 Wayland 核心：`/bin/wayland` 的 wl_display/wl_registry/wl_compositor/wl_surface/wl_shm/wl_shm_pool/wl_buffer 对象与 wire 编码，`/bin/wldemo` 通过 AF_UNIX + SCM_RIGHTS 提交 memfd 帧；服务端把 320x240 帧合成到 `/bin/display` 的 1280x800 KMS 扫描缓冲并翻页
- 新增 M3 核心：`xdg_wm_base`/`xdg_surface`/`xdg_toplevel` configure/ack，`wl_seat`、`wl_pointer` enter/motion/button、`wl_keyboard` keymap/enter；服务端标题栏、关闭按钮、拖动命中与 demo toplevel 合成
- M4 起步：modern virtio 协商 VIRGL 并发 `CTX_CREATE`；QEMU `virtio-gpu-gl-pci` 仍报 `ctrl 0x103 error 0x1203`，GPU 合成与 dmabuf 未完成
- 新增 Ring3 `/bin/display` KMS 服务：modern virtio-pci 控制队列、2D resource/backing、memfd 扫描缓冲、双缓冲 page flip；内核新增受限 `memfd_phys`/`map_phys`（仅 `display_cap`）用于扫描页和 PCI BAR
- `apps/drivers` 不再初始化 virtio-gpu，控制队列归 `/bin/display` 独占；QEMU 1280x800 下验证两帧 scanout 不同

### 版本与内存
- `version.cfg` 新增必填 `build` 构建号，`make version` 同步到 `version.h`、motd、`/etc/version`、README 与 ISO 元数据
- QEMU 默认内存从 512 MiB 提升到 2 GiB；PMM 已验证管理 8 GiB（上限 64 GiB）

### 修复
- `blockfs.c` 的 bounce buffer 按 `sectors * 512` 分配，修复 2352 字节逻辑块越界踩坏内核堆头、导致所有用户进程无法启动的问题
- `apps/drivers/glue.c` 补齐 `inw_port/inl_port/outw_port/outl_port`，修复 `drivers.zxe` 缺少 ZXE import
- 修复 `sendmsg` 控制消息长度整数回绕：`cmsg_len` 现在与剩余控制缓冲区长度比较，不再能越界读内核栈
- memfd 总量限制为可用内存的 1/4，内核堆从固定 16 MiB 改为按可用内存 1/4 动态增长，避免单进程耗尽全部物理页
- ISO 补齐规则要求的 `boot/zhaoxu/fonts/unicode.pf2`、`casper/vga.ko`、`install/MiSans.ttf` 与 `install/Inter.otf`；`dists`/`pool` 目录保留
- UEFI live ISO 优先加载 `/install/MiSans.ttf`（7.9 MB），不再只依赖 WQY 回退；`boot/zhaoxu/fonts/unicode.pf2` 由 Inter（OFL）经 grub-mkfont 生成
- 新增 M0 传输层：AF_UNIX 流 socket、每进程描述符表、memfd、mmap、poll、SCM_RIGHTS（memfd），`/data/NET_TEST.ZXE` 回归通过

## 0.7.0 「Yuanshuo」

### 文件系统
- 新增只读 NTFS：引导扇区、MFT 记录与 update-sequence fixup、驻留/非驻留属性、runlist（含稀疏）、$FILE_NAME、$INDEX_ROOT/$INDEX_ALLOCATION B+ 树目录
- 新增只读 exFAT：FAT/NoFatChain 两种分配、目录项集合、ValidDataLength 零填充、UTF-16 名称
- 新增 ISO 9660/CDFS：主卷与 Joliet 描述符、Rock Ridge NM、版本号剥离、multi-extent 文件；CDFS 探测 2048/2336/2352 四种 CD 扇区布局
- 新增只读 UDF：VRS、AVDP、VDS（PD/LVD）、FSD、ICB 文件项、short_ad/long_ad、FID 目录与 OSTA 压缩 Unicode 名称
- 新增只读 UBIFS：UBI PEB 探测、EC/VID 解析、LEB→PEB 映射、UBIFS 超块与节点 CRC、平坦节点索引、zlib 数据节点解压
- 新增独立 vfat 驱动：FAT12/16/32、VFAT 长名、固定根目录与簇链
- 新增 ramfs 与可写 rootfs：initramfs 作下层、ramfs 作 copy-up、删除用 tombstone
- overlayfs 改为可写：upper 层提交、`.wh.` 白障与去重合并
- 新增 ecryptfs 堆叠加密：SHA-256 派生文件密钥、AES-256-CBC/PKCS#7、每文件盐与 IV
- 新增 AES/SHA-256/CRC32/zlib-inflate 内核实现
- 启动时扫描 MBR/GPT/EBR 并把 NTFS/exFAT/VFAT/ISO/CDFS/UDF/UBIFS 卷挂到 `/media/<n>`

### 存储与设备驱动
- 新增 Ring3 NVMe 驱动：控制器复位/使能、admin 与 I/O 队列、identify、PRP1 传输、flush
- 新增 SDHCI SD/MMC 驱动：卡初始化、CSD 容量解析（字节/块寻址）、单块 PIO 读写
- IDE PIIX4 驱动加入 `/bin/drivers` 构建并从设备服务枚举
- 设备头扩展为 16 块盘，磁盘顺序为 AHCI/NVMe/SD/IDE/USB
- 新增 DesignWare I2C、Intel PCH SPI、LM75/TMP102 温度传感器
- 新增 Intel 8254x/82574L 网卡驱动：RX/TX 环、EEPROM MAC、链路状态
- 新增 Intel HDA：CORB/RIRB、codec 枚举、DAC→pin 输出路径、48 kHz/16-bit 播放
- 新增 OHCI/EHCI USB 主机控制器初始化与根端口复位
- 新增 virtio-gpu 3D：virgl 上下文、3D 资源与 SUBMIT_3D

### 内核平台
- 新增 MMU/页表驱动 `kernel/mem/mmu.c`：四层遍历、大页拆分、映射/解映射/保护/翻译
- 新增 HPET 驱动与 ACPI XSDT/RSDT 解析
- 新增 APIC/PIC 中断控制器前端 `intc`；ARM GICv3 与 SysTick 位于 `kernel/arch/arm64`
- `/sys/class/thermal/thermal_zone0/temp` 暴露传感器温度

### 桌面
- 按规则 1 将合成职责从 Desktop Window Manager 拆出，新增 **Desktop Window Compositor**（`apps/ADE/compositor/`），独立拥有后缓冲、损伤提交与光标状态

### 修复
- 块卷逻辑扇区（2048/2352）边界计算改用设备字节数
- 消除各新驱动的未使用函数与符号告警

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
- Aqua Desktop Environment 后缓冲合成，只提交损伤区域；菜单栏、About 面板与真实 SVG 图标
- 默认壁纸（rule 33）在构建期转成原始帧，启动时从数据卷或 ISO 加载，无盘回退 Aqua 渐变
- Ring3 Aqua Desktop Environment 显示服务器、计算器迁移为独立客户端；截图工具可通过 HMP 驱动点击

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
- Aqua Desktop Environment 图形桌面（Aqua 风格）：渐变菜单栏、RTC 时钟、Dock、软件光标
- Desktop Window Manager 窗口管理器：拖动、关闭、层叠、焦点与键盘路由
- 原生桌面应用：终端（可直接执行 8sh 命令）、文件、计算器、便签、监视器
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
- 8sh 基础命令、异常现场打印、CPUID/RTC/PCI 信息

## 0.1.0

- 仓库初始化与工具链约定（WSL2、多语言分工、Ruby 脚本）
- 引导层与内核骨架、链接脚本、构建系统
- 控制台、字符串与打印基础设施
