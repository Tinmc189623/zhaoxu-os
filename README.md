<div align="center">

# 🖥️ Zhaoxu OS

### Yuanshuo · Aqua Desktop Environment · x86_64

<kbd>🧩 微内核</kbd> <kbd>🧵 Ring 3 用户态</kbd> <kbd>💾 FAT32 + ext4</kbd> <kbd>💿 UEFI + Legacy BIOS</kbd> <kbd>🧬 MBR + GPT(ISO)</kbd> <kbd>🎨 Aqua</kbd>

**C** 内核与 UEFI 引导 · **FASM** BIOS 引导 · **GAS** 底层汇编 · **Ada** 驱动 · **C++20** 上层应用 · **Ruby** 工具链

[🌐 OSHUB](https://oshub.org/projects/zhaoxu-os) · [🐙 GitHub](https://github.com/Tinmc189623/zhaoxu-os) · [🦊 Gitee](https://gitee.com/Tinmc189623/zhaoxu-os) · [🦄 GitCode](https://gitcode.com/2503_94276351/zhaoxu-os)

**当前版本：0.7.1 「Yuanshuo」 · 内核 0.7.1 · 构建 1** · 历史版本见 [CHANGELOG.md](CHANGELOG.md)

</div>

---

> 🧭 **一句话介绍**：Zhaoxu OS 是一个面向 x86_64 的微内核操作系统。引导管理器 **ZhaoxuBootMgr** 同时支持 UEFI 与 legacy BIOS；**Yuanshuo** 是 C 编写的微内核；驱动由 **Ada + C** 实现并运行在 ring 3；Shell **8sh** 与桌面 **Aqua Desktop Environment** 同样位于 ring 3，界面由 **Desktop Window Compositor** 合成；VFS 覆盖 ext4、UBIFS、tmpfs、initramfs、procfs、sysfs、debugfs、efivarfs、ramfs、rootfs、overlayfs、ecryptfs、vfat、NTFS、ISO 9660/CDFS、UDF 与 exFAT；可执行格式为 **ZXE v1**。

---

## ✨ 0.7.1 内容

### 🚀 0.7.1 修复与改进
- Makefile 成为唯一构建入口，Ruby 只负责辅助脚本；Windows 默认在 WSL2 内 `make`
- 窗口内容内缩、标题栏避开圆角，修复内容覆盖窗口圆角的穿模；初始窗口左右并排
- live ISO 直接使用 rule 33 壁纸，菜单栏改为半透明
- `version.cfg` 新增 `build` 构建号，当前 **0.7.1 build 1**
- QEMU 默认内存提升到 2 GiB；PMM 已验证管理 8 GiB（上限 64 GiB）
- 内核内嵌 initramfs 使用 zlib 压缩，legacy BIOS 的 `KERNEL.BIN` 回到 448 KiB 限制内
- ISO 目录结构补齐 `boot/zhaoxu/fonts/unicode.pf2`、`casper/vga.ko`、`install/MiSans.ttf` 与 `install/Inter.otf`；UEFI live ISO 优先使用 MiSans
- 修复 `sendmsg` 控制消息长度回绕；memfd 与内核堆按可用内存的 1/4 动态限额/增长
- M1：新增 Ring3 `/bin/display` KMS 服务（modern virtio-pci、2D 扫描输出、双缓冲 flip）
- M2：新增 `/bin/wayland` 与 `/bin/wldemo`，Wayland wire/对象/wl_shm + SCM_RIGHTS 帧提交并合成到 KMS 扫描输出，QEMU 已验证
- M3：xdg-shell configure/ack、wl_seat/wl_pointer、wl_keyboard keymap/enter、服务端标题栏/关闭/拖动已实现；M4 virgl 上下文起步，GPU 合成与 dmabuf 待续

<table>
<tr>
<td width="50%" valign="top">

### 🗂️ 文件系统矩阵
- 新增只读 **NTFS**（MFT、runlist、$INDEX_ROOT/$INDEX_ALLOCATION）、**exFAT**（NoFatChain/FAT、ValidDataLength）、**ISO 9660/CDFS**（Joliet + Rock Ridge + multi-extent）、**UDF**（AVDP/VDS/ICB/FID）与 **UBIFS**（UBI 附着、节点 CRC、zlib 解压）
- **vfat** 独立驱动覆盖 FAT12/16/32 与 VFAT 长名
- **rootfs** 改为 initramfs 下层 + ramfs copy-up 的可写根；**overlayfs** 支持写入与 `.wh.` 白障；新增 **ramfs** 与 **ecryptfs**（AES-256-CBC）
- 启动后扫描 MBR/GPT/EBR，把可识别的卷挂到 `/media/<n>`

</td>
<td width="50%" valign="top">

### 💽 存储与总线驱动
- Ring3 **NVMe**（admin/I/O 队列、PRP、identify）与 **SDHCI**（CMD0/8/ACMD41/2/3/9/7/16，PIO 读写）
- **IDE** PIIX4 驱动接入 `/bin/drivers`，与 AHCI、USB MSC 共同组成块设备池
- **DesignWare I2C**、**PCH SPI** 与 **LM75/TMP102** 温度传感器；`/sys/class/thermal/thermal_zone0/temp`
- USB **OHCI/EHCI** 根端口复位与控制器初始化，与既有 **xHCI** 存储栈并存

</td>
</tr>
<tr>
<td width="50%" valign="top">

### 🧠 内核平台层
- 显式 **MMU/页表驱动**：四层遍历、1 GiB/2 MiB 大页拆分、映射/解映射/保护/翻译
- **HPET** ACPI 表解析与 64 位计数器；APIC Timer、PIT 与 ARM **SysTick** 后端
- 中断控制器前端统一 APIC/PIC；ARM **GICv3** 驱动位于 `kernel/arch/arm64`
- 设备头扩展到 16 块盘，并共享温度传感器状态

</td>
<td width="50%" valign="top">

### 🎨 桌面与多媒体
- **Desktop Window Compositor** 独立承担后缓冲、损伤提交与硬件光标，DWM 只负责窗口策略
- **Intel HDA** 音频输出路径（CORB/RIRB、DAC→pin、48 kHz/16-bit 立体声）
- **Intel 8254x/82574L** 网卡驱动（DMA 环、EEPROM MAC、收发）
- 遗留 virtio-gpu **3D** 通道：virgl 上下文、3D 资源与 SUBMIT_3D

</td>
</tr>
</table>

---

## 📸 实机截图

> 以下十张图片由 `tools/screenshot.rb` 在 QEMU + OVMF 实机启动后抓取帧缓冲得到，**1920×1080**，非绘制示意图。部分画面来自 0.5.0 及更早的版本（保留用于展示硬件、字体与桌面效果），命令名以当前版本的 8sh 为准。

<table>
<tr>
<td width="50%" align="center">
<img src="docs/shot-shell.png" width="100%" alt="8sh neofetch">
<br><b>🖥️ 8sh</b><br>
<sub>neofetch：内核摘要与语言构成</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-files.png" width="100%" alt="FAT32 文件读取">
<br><b>💾 FAT32 数据盘</b><br>
<sub><code>ls /mnt</code> 与 <code>cat</code>，AHCI → FAT32 → VFS → 系统调用</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-run.png" width="100%" alt="运行 .zxe 与 ELF">
<br><b>🧵 Ring 3 程序</b><br>
<sub>同一份 hello 程序分别以 <code>.zxe</code> 与 ELF64 运行，退出码 7</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-editor.png" width="100%" alt="系统内编译器">
<br><b>🛠️ 系统内开发</b><br>
<sub>历史版本的系统内编辑器：编辑后直接以 ring 3 运行 .zxe</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-disks.png" width="100%" alt="磁盘角色">
<br><b>🗄️ 磁盘与角色</b><br>
<sub><code>disks</code>：ZHAOXUDATA 数据分区 / ZHAOXUEFI ESP / 空白目标盘</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-install.png" width="100%" alt="安装器">
<br><b>📥 安装器</b><br>
<sub><code>install</code>：目标盘写入 MBR/GPT + FAT32 ESP + ZHAOXUDATA 并校验</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-desktop.png" width="100%" alt="Aqua 桌面与终端">
<br><b>🎨 Aqua Desktop Environment · Aqua</b><br>
<sub>菜单栏、窗口、Dock、软件光标；终端里运行真实的 neofetch</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-desktop-apps.png" width="100%" alt="桌面应用">
<br><b>🧮 原生桌面应用</b><br>
<sub>欢迎、便签、监视器、计算器；TrueType 中文排版</sub>
</td>
</tr>
<tr>
<td width="50%" align="center">
<img src="docs/shot-desktop-install.png" width="100%" alt="图形安装器">
<br><b>💿 图形安装器</b><br>
<sub>识别安装源与目标盘，确认后开始写入，支持 UEFI/BIOS 启动</sub>
</td>
<td width="50%" align="center">
<img src="docs/shot-fonttest.png" width="100%" alt="TrueType 字形">
<br><b>🔤 TrueType 字形</b><br>
<sub>TrueType 字形光栅：20×23 位图、24 px em（历史命令输出）</sub>
</td>
</tr>
</table>

---

## 🧠 架构

**微内核结构**：`kernel.elf` 只包含 CPU/内存/调度/IPC/VFS 核心，不含任何驱动或应用；AHCI、IDE、NVMe、SDHCI、USB MSC、PS/2、鼠标、RTC、串口、HDA、e1000、I2C/SPI/温度传感器由 Ring 3 `/bin/drivers` 独占，virtio-gpu 由 `/bin/display` 独占；Aqua Desktop Environment、8sh、`/bin/wayland` 与全部应用同样是 Ring 3 程序（规则 9）。

<table>
<tr><th>层</th><th>语言</th><th>实现</th></tr>
<tr><td>UEFI 引导</td><td>🧩 C + GAS</td><td><code>efi_bootmgr.c</code>（ZhaoxuBootMgr）→ <code>efi_main.c</code>（OSLDRX64）：GOP、RSDP、内存映射、页表、<code>ExitBootServices</code></td></tr>
<tr><td>Legacy BIOS 引导</td><td>⚙️ FASM</td><td><code>bios_stage1.asm</code> → <code>bios_stage2.asm</code>：VBE、E820、长模式</td></tr>
<tr><td>内核</td><td>🔧 C</td><td><code>kernel/</code>：GDT/TSS、IDT、PMM/VMM、堆、进程、VFS；不含任何驱动</td></tr>
<tr><td>文件系统</td><td>💾 C（内核 + Ring3）</td><td>initramfs/rootfs/overlayfs/ramfs/ecryptfs、FAT32/vfat、ext4（<code>fs/ext4</code> + <code>/bin/ext4fs</code>）、只读 NTFS/exFAT/ISO 9660/CDFS/UDF/UBIFS</td></tr>
<tr><td>底层汇编</td><td>🧱 GAS</td><td>入口、ISR 桩、用户态切换、内核镜像嵌入</td></tr>
<tr><td>驱动</td><td>✨ Ada + C，Ring3</td><td><code>/bin/drivers</code>：AHCI/IDE/NVMe/SDHCI/USB MSC、PS/2/鼠标、RTC、UART、HDA、e1000、I2C/SPI、温度；<code>/bin/display</code>：virtio-gpu 2D scanout</td></tr>
<tr><td>Shell / 桌面 / 应用</td><td>🚀 C + C++20</td><td><code>apps/csh/</code>（8sh）、<code>apps/ADE/</code>（C++20）、<code>apps/display/</code>（KMS）、<code>apps/wayland/</code>（Wayland 核心）、<code>apps/ext4fs/</code> 与 <code>user/</code> 下的独立客户端</td></tr>
<tr><td>图形框架</td><td>🎨 C + C++20</td><td>Desktop Window Compositor + Desktop Window Manager；M1 KMS、M2 wl_shm 合成、M3 xdg-shell/seat；<code>zaf/</code> 仍是 Z0 光栅</td></tr>
<tr><td>用户程序</td><td>🧵 C</td><td><code>user/</code>：`init`、`hello`、`wldemo` 等 ZXE；链接到 <code>0x10000000000</code>（1 TiB），运行在 ring 3</td></tr>
<tr><td>工具链</td><td>💎 Make + Ruby</td><td>Makefile 是构建入口；<code>tools/</code> Ruby 只做 ELF→.zxe、BIOS 镜像、冒烟测试、截图、版本同步与 ISO 元数据</td></tr>
</table>

**内存布局**

```
0xffffffff80000000  内核虚拟基址（higher-half）
0xffffffff80f00000  内核栈顶
0xffffffff90000000  内核堆
0x00010000000000    ring 3 程序加载基址（1 TiB）
0x00010100000000    ring 3 用户栈顶（32 页）
```

**系统调用 ABI**：`int 0x80`，`rax` = 调用号，`rdi/rsi/rdx/r10` = 参数；用户指针按进程地址空间逐页校验。

| # | 调用 | # | 调用 |
| :---: | --- | :---: | --- |
| 0 | `exit` | 1 | `write` |
| 2 | `getpid` | 3 | `open` |
| 4 | `read` | 5 | `close` |
| 6 | `uptime` | 7 | `spawn` |
| 8 | `wait` | 9–14 | 命名端口 `listen/connect/accept/send/recv/close` |
| 15–18 | 共享内存 `create/map/unmap/close` | 19 | `fb_map` |
| 20 | `mouse` | 21 | `key` |
| 22 | `sleep` | 23 | `cpus` |
| 24 | `rtc` | 25 | `read_path` |
| 26–27 | `port_try_accept/recv` | 28 | `serial_write` |
| 29 | `readdir` | 30 | `disks` |
| 31 | `install` | 32 | `write_file` |
| 33 | `unlink` | 34 | `data_volume` |
| 35 | `dev_alloc_page` | 36 | `boot_font` |
| 37 | `spawn_arg` | 38–42 | 块 I/O `blk_count/info/read/write/flush` |
| 43 | `socket` | 44 | `bind` |
| 45 | `listen` | 46 | `accept` |
| 47 | `connect` | 48 | `sendmsg` |
| 49 | `recvmsg` | 50 | `poll` |
| 51 | `memfd_create` | 52 | `ftruncate` |
| 53 | `lseek` | 54 | `mmap` |
| 55 | `munmap` | 56 | `fcntl` |
| 57 | `socketpair` | 58 | `boot_wallpaper` |
| 59 | `memfd_phys` | 60 | `map_phys` |

完整定义在 `kernel/proc/syscall.c` 与 `user/userlib.h`，两处必须同步。59/60 只授予内核启动的 `/bin/display`（`display_cap`），用户进程无法读取扫描页物理地址或映射设备 BAR。

---

## 🚀 引导流程

**UEFI 路径**

1. 固件加载 `EFI/BOOT/BOOTX64.EFI`（**ZhaoxuBootMgr**，C + `lld-link` 生成的 PE32+ 应用）。
2. 引导管理器加载 `OSLDRX64.ELF`，由它取得 GOP 帧缓冲、ACPI RSDP 与 UEFI 内存映射。
3. 从光盘根 `install/KERNEL.ELF`（ISO）或已安装 ext4 根下的 `KERNEL.ELF` 读入内核，复制到 4 GiB 以下的页对齐物理内存，建立低端恒等映射与 higher-half 页表。
4. `ExitBootServices` → 切换 CR3 → 跳转 `0xffffffff80000000`。

**Legacy BIOS 路径**

1. SeaBIOS 从 El Torito（或安装盘 MBR）加载 512 字节 stage 1。
2. stage 1 用 INT 13h 读 stage 2（ISO 上 stage 2 已驻留在引导镜像内，直接远跳 `0x8000`）。
3. stage 2 选择 32bpp VBE LFB、收集 E820、扫描 RSDP，用 EDD LBA48 读入内核。
4. 建立页表、清零 16 MiB 内核窗口、写 `boot_info`，进入长模式。

**内核公共路径**

5. GDT/IDT/PIC → PMM/VMM/堆 → CPUID/ACPI/RTC → VFS → 字体；块设备由 Ring3 驱动服务提供。
6. 挂载 initramfs（`/`）与数据卷（`/mnt`）：FAT32 卷标 `ZHAOXUDATA`；安装后的 ext4 根由 `/bin/ext4fs` 服务经 VFS 转发。
7. 自动以 ring 3 运行 `/bin/init`；有 GOP 帧缓冲时启动 **Aqua Desktop Environment**，无帧缓冲时进入 **8sh**。

---

## 💻 8sh 命令

<table>
<tr><th>命令</th><th>说明</th><th>命令</th><th>说明</th></tr>
<tr><td><code>help</code></td><td>命令列表</td><td><code>neofetch</code></td><td>系统摘要</td></tr>
<tr><td><code>version</code></td><td>OS 与内核版本</td><td><code>uname</code></td><td>系统标识</td></tr>
<tr><td><code>echo</code></td><td>输出文本</td><td><code>uptime</code></td><td>运行时间</td></tr>
<tr><td><code>ls [path]</code></td><td>列目录</td><td><code>cat &lt;path&gt;</code></td><td>输出文件内容</td></tr>
<tr><td><code>write &lt;path&gt; &lt;text&gt;</code></td><td>创建/覆盖 /mnt 文件</td><td><code>rm &lt;path&gt;</code></td><td>删除 /mnt 文件</td></tr>
<tr><td><code>run &lt;path&gt;</code></td><td>以 ring 3 运行 .zxe / ELF64</td><td><code>cpus</code></td><td>在线 CPU 数</td></tr>
<tr><td><code>pid</code></td><td>8sh 进程号</td><td><code>disks</code></td><td>磁盘与安装角色</td></tr>
<tr><td><code>install</code></td><td>安装到空白目标盘（MBR/GPT）</td><td><code>exit</code></td><td>退出 shell</td></tr>
</table>

行编辑支持退格与 `Ctrl+C` 取消当前行。

---

## 🧬 可执行格式：ZXE v1

```text
zhaoxu> run /data/HELLO.ZXE
hello from ring 3
[zxe] exit code 7
```

- 头部 32 字节 + 段表 40 字节 + 原始数据；段权限强制 W^X（CODE=RX、RODATA=R、DATA/ZERO=RW）
- 加载基址在 `[0x10000000000, +1 GiB)` 内按 64 KiB 对齐随机选择（ASLR，最多尝试 64 次）
- 依赖按简单文件名从 `/lib` 加载配套库；`tools/zxe-fixture-lib.rb` 可生成测试共享库
- `tools/zxe-pack.rb` 把 ELF64 转成 `.zxe`，`tools/zxe-check.rb` 校验结构
- 入口是 `int user_main(long *stack)`；用户栈固定在 `0x10100000000`（32 页）
- 旧式 ELF64 兼容路径保留，加载器按魔数自动分发

---

## 📦 构建与运行

构建必须支持四种宿主：**原生 Linux、原生 macOS、原生 Windows（MSYS2/MinGW）与 WSL2**。**构建系统是 Makefile**；Ruby 只负责辅助脚本（ELF→ZXE、版本同步、ISO 元数据、冒烟/截图）和可选的宿主探测。Windows 首选 WSL2，在其内部直接 `make`。

```bash
cd src
make -j4                         # 构建全部（Linux/macOS/WSL）
make iso                         # 生成 out/zhaoxu_{os_version}_installer.iso（UEFI + BIOS）
make run-iso                     # UEFI 光盘启动（不挂数据盘）
make host-test                   # 宿主检测单元测试
ruby tools/build.rb --check      # 辅助：工具矩阵与安装提示
```

```powershell
# Windows（PowerShell 7+）：默认转发到 WSL2 执行 make；-HostMode native 才走原生 MSYS2
./src/build.ps1                  # WSL2 make all
./src/build.ps1 -Check           # 工具矩阵（Ruby 辅助探测）
./src/run.ps1 -Display gtk       # WSL2 QEMU 图形窗口
```

真机与虚拟机的安装步骤见 [docs/INSTALL.md](docs/INSTALL.md)（[Word 版](docs/INSTALL.docx)）。

**依赖**：逐项检查用 `ruby tools/build.rb --check`。Linux/WSL 需要 `gcc`、`g++`、`gcc-14`+`gnat-14`、`ld.lld`、`objcopy`、`clang`、`lld-link`、`qemu-system-x86_64`、OVMF、`mkfs.vfat`、`mtools`、`tar`、`xorriso`、`squashfs-tools`；macOS 用 Homebrew 的 `llvm`、`lld`、`qemu`、`xorriso`、`mtools`、`dosfstools`、`squashfs`；原生 Windows 用 MSYS2 中对应包加 Ruby。Ada 驱动需要 x86_64-elf GNAT（`ADA=` 指定），FASM 需使用宿主版本（`FASM=`，Windows 也可放 `src/tools/fasm.exe`）。

**验证工具**（Ruby）：

```bash
ruby tools/smoke.rb <marker> [commands]     # 无头冒烟，逐条发送 8sh 命令
ruby tools/screenshot.rb 3.0 out/shot.png   # 实机启动并抓取帧缓冲
SMOKE_BOOT=iso-install ruby tools/smoke.rb ... # ISO + 空白目标盘安装测试
SMOKE_BOOT=installed ruby tools/smoke.rb ...   # 从安装后的目标盘启动
```

---

## 🗂️ 目录结构

```
Zhaoxu OS/
├── README.md                 当前版本说明（历史见 CHANGELOG.md）
├── CHANGELOG.md              全部版本记录
├── LICENSE                   Apache License 2.0
├── THIRD_PARTY_NOTICES.md    第三方代码与字体声明
├── version.cfg               版本号唯一来源（os_version + kernel_version + build，make version 同步）
├── docs/                     实机截图与架构/ZAF 设计文档
└── src/
    ├── Makefile              四宿主构建规则（Linux/macOS/Windows/WSL）
    ├── boot/
    │   ├── efi_bootmgr.c     UEFI 引导管理器 ZhaoxuBootMgr（C）
    │   ├── efi_main.c        UEFI OS 加载器 OSLDRX64（C）
    │   ├── bios_stage1.asm   MBR 引导扇区（FASM）
    │   ├── bios_stage2.asm   实模式 → 长模式（FASM）
    │   └── fonts/            引导用 unicode.pf2（Inter + grub-mkfont）
    ├── kernel/
    │   ├── arch/x86_64/      入口、GDT/TSS、ISR、PIC、ring 3 切换
    │   ├── int/              IDT 与异常/IRQ/系统调用
    │   ├── drivers/          计时与 ACPI/AHCI 胶水（硬件驱动在 Ring3）
    │   ├── fs/               initramfs、FAT32、VFS、安装器
    │   ├── proc/             ZXE/ELF64 加载与系统调用、fd 表
    │   ├── ipc/              命名端口、共享内存、AF_UNIX、memfd、poll
    │   ├── crypto/           AES、SHA-256
    │   ├── mem/              PMM、VMM、MMU、可增长堆
    │   ├── lib/              字符串、printf、UTF-8 控制台、inflate
    │   ├── font/             8x8 点阵 + TrueType glyf 光栅化
    │   └── include/          内核公共头文件
    ├── fs/ext4/              OS 无关 ext4 核心（挂载/目录/读写/分配/格式化）
    ├── zaf/                  Zhaoxu Aqua Framework（Z0 光栅接口）
    ├── drivers/              Ada/SPARK 驱动（AHCI、IDE、NVMe、SDHCI、USB MSC/xHCI、PS/2、鼠标、串口）
    ├── apps/                 8sh、Aqua Desktop Environment、display KMS、wayland 核心、ext4fs、Ring3 驱动 C 胶水
    ├── user/                 ring 3 程序、Wayland/ADE 客户端、wl_demo、用户库
    ├── initramfs/            ramfs 初始内容、数据盘素材与光标主题
    ├── tests/                host 测试（ext4、构建宿主检测）
    ├── tools/                Makefile 构建入口；Ruby 辅助：zxe-pack、iso-*、smoke、screenshot、version
    └── out/                  构建产物（kernel.elf、ISO）
```

---

## ⚠️ 当前限制

- 🧵 进程与线程：最多 16 个进程、32 个线程；已有抢占式 SMP 调度和 `spawn/wait`，但无 `fork/exec`、信号、COW 与交换
- 🧠 内存：无按需分页，用户页在加载时一次性映射；用户栈固定 32 页；内核堆按需增长到可用内存 1/4，memfd 单对象 64 MiB、总量 1/4，PMM 位图上限 64 GiB
- 💾 文件系统：FAT32/vfat 可写（8.3 短名、单文件 16 MiB）与 ext4 可写（Ring3 服务，单次 RPC 8 MiB）；NTFS/exFAT/ISO 9660/CDFS/UDF/UBIFS 只读；另含 initramfs/rootfs/overlayfs/ramfs/ecryptfs
- 🔐 权限：无多用户、文件属主与 Unix 权限位，所有用户进程以同一特权运行
- 🧱 兼容性：无 POSIX/libc 兼容层，系统调用为 ZOS API 1.0；ZXE v1 支持按文件名从 `/lib` 加载依赖，无 ELF 动态链接器；Wayland 是自研 wire/对象核心，stock libwayland 客户端尚不能直接运行
- 🌐 网络：无 TCP/IP 协议栈；已有 AF_UNIX 流 socket、poll、SCM_RIGHTS、memfd，用于 Wayland 与 M0 传输
- 🔌 外设：输入为 PS/2 键盘/鼠标与 COM1；存储覆盖 AHCI/IDE/NVMe/SDHCI/USB MSC；有 HDA、e1000、I2C/SPI 与温度传感器；无 USB HID 与完整网络协议栈
- 🖼️ 图形：M1 virtio-gpu 2D KMS 扫描输出与双缓冲 flip；合成仍为软件光栅；M2/M3 Wayland 核心与 xdg-shell/seat 已通，virgl GPU 合成与 linux-dmabuf 待续；无多显示器
- ⚡ 电源：ACPI 仅用于关机/重启，无睡眠与休眠
- 🛡️ 启动：引导加载器未签名，开启 Secure Boot 的机器需要先关闭 Secure Boot 或自行签名
- ⏱️ 时间：仅 RTC 本地时间，无 NTP 与时区处理

---

## 📜 许可证

本项目采用 [Apache License 2.0](LICENSE)。第三方组件与字体声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

<div align="center">

**Zhaoxu OS · Yuanshuo** · C / FASM / GAS / Ada / C++ 技术栈

Copyright © 2026 Nexsteaduser. All Rights Reserved. · admin@nexsteaduser.com

</div>
