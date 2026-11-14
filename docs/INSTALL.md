# Zhaoxu OS 安装指南

适用 Zhaoxu OS 0.6.0「Yuanshuo」，内核 Yuanshuo 0.6.0。安装介质是一张 ISO，UEFI 与 Legacy BIOS 都能启动；装完以后目标盘上是 FAT32 的 ESP 和 ext4 根卷，GPT 布局另带一个 BIOS boot 区，内核放在 ext4 里。

这篇文档按真机、虚拟机、无图形环境三条路径写。动手前先看“装之前先确认”，里面的限制会直接决定这台机器能不能装。

---

## 装之前先确认

### 硬件门槛

| 项目 | 要求 |
| --- | --- |
| CPU | x86_64，支持长模式与 NX |
| 内存 | 最低 512 MiB，建议 1 GiB 以上 |
| 目标盘 | SATA / AHCI 控制器上的磁盘，整盘安装 |
| 输入 | PS/2 键盘与鼠标 |
| 显示 | UEFI GOP，或 Legacy BIOS 下 32bpp VBE 线性帧缓冲 |
| 固件 | UEFI 或 Legacy BIOS/CSM，Secure Boot 必须关闭 |
| 安装介质 | U 盘或 DVD，容量不小于 1 GiB |

几件事需要提前说清楚：

- 当前存储驱动只覆盖 AHCI/IDE/SATA/SCSI。NVMe 硬盘、USB 移动硬盘、U 盘在系统启动后都不可见。U 盘可以当安装介质，因为读取 ISO 用的是固件；但目标盘必须挂在 SATA 控制器上。
- 输入只支持 PS/2 键盘和鼠标。纯 USB 键鼠的机器可能只有固件的 legacy 模拟能敲几下，进系统后不保证可用。装机前最好确认主板有 PS/2 接口，或者 BIOS 里打开了 USB legacy support。
- Secure Boot 必须关掉。ZhaoxuBootMgr 没有签名，开着 Secure Boot 会直接拒绝启动。
- SATA 模式要设成 AHCI。Intel RST、RAID、Optane 这类模式会让 Ring3 AHCI 驱动找不到盘。切换模式可能影响已经装好的 Windows，动之前先想清楚。
- 系统还没有网络、音频、USB HID、多用户和 POSIX 兼容层。这些不是安装步骤能解决的。

### 目标盘会被整盘覆盖

安装器不让你选分区。它扫描所有 AHCI 磁盘，挑出“空白或已有 Zhaoxu 布局”的那一块，然后从 LBA 0 开始重写：MBR/GPT、stage1/stage2、ESP、ext4 根卷全部重建。

这意味着：

- 目标盘上原有的 Windows、Linux、数据分区会一起消失。
- 有两块以上空白盘时，安装器默认选索引最小的那块，GUI 里看不到“盘 0 / 盘 1”的选择。装之前把不相关的空白盘拔掉，或者先用 `disks` 看清楚。
- 目标判断只看首扇区的分区表，不看文件系统内容。空白盘、首分区从 LBA 2048 开始且类型是 0xEF/0x0C/0x0E 的 MBR 盘，以及带 `ZSLBA` 标记的保护性 MBR，都会进入 `target` 角色。某些 Linux 或 Windows 盘的 ESP 刚好排在第一分区、起点是 LBA 2048，也会被当成可重装目标。拿不准就先拔盘，或者在 `8sh` 里跑 `disks` 核对。
- 已经装过 Zhaoxu OS 的盘会被识别为可重装目标，重新安装同样会清掉旧系统。

### 目标盘要多大

识别门槛是 2048 + 70000 + 32768 个 512 字节扇区，约 52 MiB。低于这个值的盘不会出现在 `target` 角色里。

实际使用建议 256 MiB 起步，跟 QEMU 回归用的 `out/target.img` 一致。1 GiB 以上更宽裕。ESP 最多 64 MiB，剩下的全给 ext4 根卷，字体和壁纸约占 11 MiB。

---

## 1. 拿到 ISO

### 从源码构建

仓库根目录的 `version.cfg` 是版本号唯一来源。构建入口是 Ruby，`tools/build.rb --check` 会按当前宿主打印工具矩阵和缺什么。

Linux、macOS、WSL2：

```bash
cd src
ruby tools/build.rb --check
ruby tools/build.rb iso
```

Windows PowerShell 7：

```powershell
cd src
.\build.ps1 -Check
.\build.ps1 iso
```

产物是 `src/out/zhaoxu_0.6.0_installer.iso`。文件名里的版本号取自 `version.cfg`，不要手工改。

构建依赖按宿主不同：

- Linux/WSL：`gcc`、`g++`、`gcc-14`/`gnat-14`、`ld.lld`、`objcopy`、`clang`、`lld-link`、`qemu-system-x86_64`、OVMF、`mkfs.vfat`、`mtools`、`tar`、`xorriso`、`squashfs-tools`。
- macOS：Homebrew 的 `llvm`、`lld`、`qemu`、`xorriso`、`mtools`、`dosfstools`、`squashfs`；Ada 驱动需要 x86_64-elf GNAT，用 `ADA=` 指定。
- 原生 Windows：Ruby、MSYS2 的 `make`、`mingw-w64-x86_64-clang`、`lld`、`llvm`、`dosfstools`、`mtools`、`xorriso`、`squashfs-tools`、`qemu`；FASM 用宿主版本，`FASM=` 指定，或把 `fasm.exe` 放进 `src/tools/`。

`ruby tools/build.rb --check` 的输出比这份清单更准，缺工具时优先看它给的安装提示。

### 从发布页下载

如果用的是已发布的 ISO，文件名同样应该是 `zhaoxu_0.6.0_installer.iso`。下载完先核对大小与校验值，再写盘。

---

## 2. 把 ISO 写进 U 盘或光盘

### U 盘

ISO 是 El Torito 双入口加 GPT hybrid 的混合镜像，UEFI 和 Legacy BIOS 的引导记录都在里面。写盘工具要用“原样写入”，不要让它重建文件系统。

Windows 上推荐 Rufus：

1. 选择 `zhaoxu_0.6.0_installer.iso`。
2. 分区类型选 GPT，目标系统选 UEFI；只用 Legacy BIOS 的机器选 MBR。
3. 点“开始”。Rufus 问“ISO 镜像模式 / DD 镜像模式”时选 DD。
4. U 盘上的原有数据会被清空。

Linux/macOS 直接用 `dd`：

```bash
sudo dd if=zhaoxu_0.6.0_installer.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

`of=/dev/sdX` 是整块 U 盘，不是 `/dev/sdX1`。写错设备会毁掉别的磁盘，执行前用 `lsblk` 看两遍。

不要直接把 ISO 文件复制到 FAT32 U 盘。那样只是一个 80 多 MB 的文件，固件不会把它当启动盘。

### 光盘

用刻录软件选择“刻录镜像”，不是“制作数据光盘”。DVD 容量足够，El Torito 的双入口会让 UEFI 和 Legacy BIOS 都认这张盘。

---

## 3. 固件设置

开机进 BIOS/UEFI 设置，逐项检查：

- **Secure Boot**：关闭。不同主板叫法不一样，可能在 Security、Boot 或 Authentication 菜单里。
- **SATA 模式**：AHCI。不要用 RAID、RST、Optane。
- **启动模式**：想走 UEFI 就保持 UEFI；想走 Legacy BIOS 就打开 CSM/Legacy Boot。两种都可以装，但建议 UEFI 配 GPT、Legacy BIOS 配 MBR，少碰固件兼容性问题。
- **启动顺序**：U 盘或光驱排第一。
- **USB legacy support**：打开。PS/2 键鼠可以跳过这一项；只有 USB 键鼠的机器需要它撑到系统启动。
- **快速启动**：可以先关掉，方便看到启动设备和错误信息。

虚拟机里对应的是“启用 EFI”“安全启动”“SATA/AHCI 控制器”这几个选项，下一节单独写。

---

## 4. 从 ISO 启动

UEFI 路径的串口日志大致长这样：

```text
ZhaoxuBootMgr (UEFI x86_64)
BootMgr: zhaoxu.cfg loaded
BootMgr: starting OSLDRX64.efi
Kernel: /install/KERNEL.ELF from the ISO.
Font: /install/WQY.TTC from the ISO.
```

Legacy BIOS 路径：

```text
ZhaoxuBootloader stage1 (legacy BIOS)
ZhaoxuBootloader stage2 (legacy BIOS)
A20 gate enabled.
VESA framebuffer selected.
E820 memory map collected.
ACPI RSDP located.
Reading kernel image.
Kernel image read.
Kernel image loaded, entering long mode.
```

内核起来后会启动 Aqua Desktop Environment 桌面，菜单栏、Dock、欢迎窗口和计算器窗口都会出现。中文能正常显示，说明字体已经从 ISO 的 `/install/WQY.TTC` 读进来了。

安装窗口是否自动弹出，看两个条件：

1. 当前没有挂载任何数据卷或根卷。
2. `disks` 里至少有一块 `target` 角色的磁盘。

从 ISO 启动、目标盘空白时，窗口会自动出现。已经挂着 `ZHAOXUDATA` 数据盘时不会自动出现，这时点菜单栏的“文件”可以强制打开安装器。窗口没出现先别怀疑安装器，按第 9 节查目标盘识别。

---

## 5. 执行安装

### 看清目标盘

安装窗口标题是“安装 Zhaoxu OS”，正文会写“单 ISO 安装，无需额外介质”和“安装会写入唯一的空白目标盘”。窗口里没有磁盘列表，也没有“选择第几块盘”的控件。

需要确认磁盘角色时，用无图形环境下的 `8sh`：

```text
8sh$ disks
Idx Port Size Label Role Model
 0 0 256 MiB - target QEMU HARDDISK
```

角色含义：

- `target`：空白盘，或首分区起点为 LBA 2048、类型为 0xEF/0x0C/0x0E 的旧 Zhaoxu 布局，或带 `ZSLBA` 标记的 Zhaoxu GPT。可以被安装。
- `media`：卷标 `ZHAOXUEFI` 的 ESP。安装器不会选它。
- `data`：卷标 `ZHAOXUDATA` 的数据盘。安装器不会选它。
- `other`：其他分区表或文件系统。安装器不会选它。

### 选 MBR 还是 GPT

窗口默认高亮 GPT。两块按钮分别是“使用 MBR 安装”和“使用 GPT 安装”。

点下去就开始写盘，没有第二次确认。手指离开鼠标之前，先确认目标盘里没有要留的东西。

| 布局 | 适用场景 | 写入内容 |
| --- | --- | --- |
| MBR | Legacy BIOS 启动 | LBA0 stage1，LBA1 stage2，LBA2048 起 FAT32 ESP（最多 64 MiB），其后 ext4 根卷 |
| GPT | UEFI 启动 | 保护性 MBR，LBA1/2 GPT 头与分区表，LBA34 起 stage2（占用 GPT BIOS boot 区），LBA2048 起 FAT32 ESP（最多 64 MiB），其后 ext4 根卷，盘尾备份 GPT |

两种布局都不把内核放进 ESP，也不放进引导扇区。内核只存在于 ext4 根卷：

- `/KERNEL.BIN`：给 legacy BIOS stage2 按连续块列表直接读。
- `/KERNEL.ELF`：给 UEFI 加载器解析。
- `/WQY.TTC`：中文矢量字体。
- `/WALLPAPER.RAW`：默认壁纸的原始帧。

ESP 的卷标是 `ZHAOXUEFI`，类型是 FAT32，里面只放 `EFI/BOOT/` 下的 UEFI 引导文件。ext4 根卷的卷标是 `ZHAOXUROOT`。

### 等它写完

写入过程中界面不会显示进度条，只有状态文字从“点击按钮开始安装”变成安装中。实际耗时取决于磁盘速度，多数 SATA SSD 几秒到十几秒，机械盘和虚拟磁盘会慢一些。

成功时窗口显示：

```text
安装完成，请重启并从目标盘启动
```

失败时显示：

```text
安装失败：请确认存在空白目标盘
```

串口里对应的成功日志是：

```text
[install] esp 131072 sectors, ext4 root ... @ 133120 (target ..., GPT)
[ext4] formatted ... label ZHAOXUROOT
[ext4] write /KERNEL.ELF ...
[ext4] write /KERNEL.BIN ...
[ext4] write /WQY.TTC ...
[install] complete
```

看到 `[install] complete` 再断电。没看到就按第 9 节排查，不要反复点按钮。

### 重启前

1. 关机。
2. 拔掉安装 U 盘，或把光盘退出。
3. 进固件设置，把目标盘调到启动顺序第一位。
4. UEFI 机器保留 UEFI 模式；Legacy BIOS 机器保留 Legacy/CSM。

---

## 6. 安装后的第一次启动

UEFI 启动成功后，串口应该出现：

```text
ZhaoxuBootMgr (UEFI x86_64)
BootMgr: starting OSLDRX64.efi
Kernel: /KERNEL.ELF from ext4.
Font: /WQY.TTC from ext4.
...
[fs  ] ext4 service ready
[vfs ] /mnt mounted (ext4 "ZHAOXUROOT")
Boot complete. Starting Aqua Desktop Environment.
```

Legacy BIOS 启动成功后：

```text
ZhaoxuBootloader stage1 (legacy BIOS)
ZhaoxuBootloader stage2 (legacy BIOS)
Reading kernel image.
Kernel image read.
Kernel image loaded, entering long mode.
...
[ext4] mounted "ZHAOXUROOT" on /mnt: ...
[vfs ] /mnt mounted (ext4 "ZHAOXUROOT")
Boot complete. Starting Aqua Desktop Environment.
```

`/` 仍然是 initramfs，ext4 根卷挂载在 `/mnt`。系统程序从 initramfs 启动，字体、壁纸和安装资产从 `/mnt` 读取。安装完成后的桌面会自动显示壁纸和中文，不再依赖 ISO。

### UEFI 安装后的已知问题

0.6.0 的安装器在写 ESP 时只复制了 `EFI/BOOT/BOOTX64.EFI`，没有把第二级加载器 `EFI/BOOT/OSLDRX64.efi` 一起写进去。结果就是：安装能完成，Legacy BIOS 也能正常启动，但 UEFI 重启后会停在：

```text
BootMgr: cannot read OSLDRX64.efi
```

ISO 本身包含这个文件，补进去就行。

Windows 下用 `diskpart` 给 ESP 临时分配盘符：

```text
diskpart
list disk
select disk N
list partition
select partition M
assign letter=S
exit
```

在资源管理器里挂载 ISO，把 `EFI\BOOT\OSLDRX64.efi` 复制到 `S:\EFI\BOOT\`。复制完成后收回盘符：

```text
diskpart
select disk N
select partition M
remove letter=S
exit
```

Linux 下：

```bash
lsblk -f
sudo mkdir -p /mnt/zhaoxu-esp
sudo mount /dev/sdX1 /mnt/zhaoxu-esp
sudo cp /media/$USER/ZHAOXU_OS/EFI/BOOT/OSLDRX64.efi /mnt/zhaoxu-esp/EFI/BOOT/
sync
sudo umount /mnt/zhaoxu-esp
```

`/dev/sdX1` 要换成 `ZHAOXUEFI` 那个 FAT32 分区，通常是 64 MiB。补完文件再启动，`BootMgr: zhaoxu.cfg missing, using defaults` 是正常输出，它会用默认路径找 `OSLDRX64.efi`。

如果你只想快速跑通，Legacy BIOS 安装不受这个问题影响。

---

## 7. 在虚拟机里装

### QEMU

仓库自带安装回归目标。它们会自动挂一张 256 MiB 空白 `out/target.img`，并把它作为 `ide.1` 磁盘：

```bash
cd src
ruby tools/build.rb iso
ruby tools/run.rb --display gtk run-install
```

Legacy BIOS 版本：

```bash
ruby tools/run.rb --display gtk run-install-bios
```

`--display gtk` 是必须的，默认 `-display none` 看不到窗口，也点不到安装按钮。

在 Aqua Desktop Environment 里点“使用 GPT 安装”或“使用 MBR 安装”，等“安装完成，请重启并从目标盘启动”出现，关闭 QEMU。然后只挂目标盘启动。

UEFI：

```bash
qemu-system-x86_64 -M q35 -m 512 -smp 4 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,unit=1,file=out/OVMF_VARS.fd \
  -drive if=none,id=zdst,format=raw,file=out/target.img \
  -device ide-hd,drive=zdst,bus=ide.0 \
  -boot order=c -serial stdio -display gtk
```

`OVMF_CODE` 指向宿主上的 OVMF 代码文件，PowerShell 里要写成 `$env:OVMF_CODE`，或者直接替换成实际路径。`out/OVMF_VARS.fd` 在 `run-install` 执行时已经生成。

Legacy BIOS：

```bash
qemu-system-x86_64 -M q35 -m 512 -smp 4 \
  -drive if=none,id=zdst,format=raw,file=out/target.img \
  -device ide-hd,drive=zdst,bus=ide.0 \
  -boot order=c -serial stdio -display gtk
```

### VirtualBox

1. 新建 x86_64 虚拟机，内存 512 MiB 以上，CPU 2 到 4 个。
2. 存储控制器选 SATA，类型 AHCI。不要用 NVMe 控制器。
3. 光驱挂 ISO，新建一块空白虚拟硬盘挂到 SATA。
4. 系统设置里启用 EFI 就按 UEFI 装，不启用就按 Legacy BIOS 装。EFI 模式下把 Secure Boot 关掉。
5. 启动虚拟机，安装完成后关机，把 ISO 从光驱移除，再启动。

VirtualBox 默认模拟 PS/2 键鼠，安装窗口可以正常点击。UEFI 安装后仍要按第 6 节补 `OSLDRX64.efi`。

### VMware Workstation

1. 新建自定义虚拟机，客户机系统选“其他 Linux 6.x 64 位”或“其他 64 位”。
2. 固件选 UEFI 或 BIOS，与安装时选的布局保持一致。UEFI 下关闭 Secure Boot。
3. 硬盘用 SATA，不要加 NVMe 控制器。
4. 光驱挂 ISO，启动安装。
5. 安装完成后断开 ISO，从虚拟硬盘启动。

VMware 的 PS/2 键鼠模拟够用；如果鼠标点不到窗口，检查虚拟机是否捕获了输入。

---

## 8. 无图形环境安装

机器没有可用帧缓冲时，内核会启动 Ring3 的 `8sh`，不启动 Aqua Desktop Environment。串口或无头 QEMU 里用命令安装。QEMU 的 `-display none` 只是不显示窗口，GOP 帧缓冲还在，内核照样启动 Aqua Desktop Environment；要走 8sh，构建时得关掉图形层：

```bash
ruby tools/build.rb --headless iso
```

然后按 `HEADLESS=1` 的 ISO 启动，串口里会出现 `8sh$ `。注意：安装器复制的是当前运行的引导器和 stage2，headless 装出来的系统重启后仍然只进 `8sh`，不会启动 Aqua Desktop Environment。要桌面就回第 5 节用图形安装器装。

```text
8sh$ disks
8sh$ install gpt
```

或：

```text
8sh$ install mbr
8sh$ install 0 gpt
```

`install` 不带参数时默认 MBR，自动挑第一块 `target` 盘。带索引时先跑 `disks` 看 `Idx` 列，索引从 0 开始。

输出应该是：

```text
Installing GPT + ext4 root; this may take a while...
Install complete. Boot the target disk directly.
```

失败时：

```text
8sh: no suitable blank target disk
```

或者：

```text
8sh: install failed while writing the target disk
```

第二种情况串口里会有 `[install]` 开头的具体错误，比如 `target is not a blank Zhaoxu target disk`、`ext4 service could not format the root volume`、`KERNEL.BIN is not contiguous inside ext4`。按错误文字对照下一节。

---

## 9. 常见问题

### 启动 ISO 后没有安装窗口

先看 `disks` 输出里有没有 `target`。没有的话，常见原因：

- 目标盘是 NVMe 或 USB，AHCI 驱动看不到。
- 目标盘已经有 Windows/Linux 分区表，首扇区既不空白，也不像 Zhaoxu 布局。
- 目标盘小于约 52 MiB。
- 目标盘是 `ZHAOXUEFI` 或 `ZHAOXUDATA` 卷，被安装器主动排除。
- 主板 SATA 模式不是 AHCI。

目标盘在，但窗口没自动弹出，通常是当前已经挂载了数据卷或根卷。点菜单栏“文件”强制打开安装器。

### 安装按钮点了没反应，或者窗口消失

安装器是独立进程 `/bin/apps/install.zxe`。窗口关闭后不会自己回来，点菜单栏“文件”可以再开。安装失败后窗口会显示红字“安装失败：请确认存在空白目标盘”，同时串口打印具体原因。

### 卡在 `BootMgr: cannot read OSLDRX64.efi`

UEFI 安装后的已知问题，按第 6 节把 `OSLDRX64.efi` 补进 ESP。Legacy BIOS 不会走这条路径。

### 卡在 `BootMgr: zhaoxu.cfg missing` 之后

这行本身不是错误，下一行才关键。如果接着是 `cannot read OSLDRX64.efi`，还是上面的缺文件问题。如果接着是 `starting OSLDRX64.efi`，说明引导管理器已经把控制权交出去了。

### UEFI 启动直接进固件设置或黑屏

- 开 Secure Boot 时，固件会拒绝未签名的 `BOOTX64.EFI`。关掉 Secure Boot。
- 目标盘按 Legacy 方式安装，却用 UEFI 模式启动，或者反过来。确认安装时选的布局和固件模式一致。
- 纯 NVMe 机器没有可用的 SATA 目标盘，系统装不上，也启动不了。

### Legacy BIOS 提示 `stage2: disk read failed`

引导阶段读不到内核。常见原因是安装没写完、目标盘掉电，或者启动的是另一块盘。重新安装一次，确认串口里有 `[install] complete`。

### 进系统后键鼠不动

系统只驱动 PS/2。USB 键鼠在固件阶段可能能用，进系统后没有 USB HID 驱动。换 PS/2 键鼠，或在 BIOS 里打开 USB legacy support 试试。虚拟机默认模拟 PS/2，一般不会遇到。

### 桌面能开，但分辨率或画面不对

UEFI 走 GOP，Legacy BIOS 走 32bpp VBE。部分独立显卡在 Legacy 模式下不提供可用的 VBE 线性帧缓冲，换集成显卡、开 CSM，或者改用 UEFI 启动。虚拟机里把显示适配器设成标准 VGA/VMware SVGA，不要开 3D 加速，也不要用 PCI passthrough 的显卡。

### 安装卡在 `KERNEL.BIN is not contiguous inside ext4`

ext4 服务没能给内核分配连续块。重新安装一次通常能过；反复失败就换一块目标盘，或者先确认磁盘没有坏道、容量没有被控制器截断。

### 目标盘有旧系统，安装器拒绝

安装器只接受空白盘或已有 Zhaoxu 布局。要整盘重装，先清空分区表。这一步会丢掉盘上所有数据，执行前再三确认磁盘编号。ESP 刚好排在第一分区、起点是 LBA 2048 的盘也会被当成 Zhaoxu 布局，清之前先确认这块盘确实不要了。

Windows：

```text
diskpart
list disk
select disk N
clean
exit
```

Linux：

```bash
lsblk
sudo wipefs -a /dev/sdX
```

清完之后重新启动 ISO，`disks` 里应该出现 `target`。

---

## 10. 卸载和回收磁盘

系统没有卸载程序。要退回 Windows 或 Linux，直接用安装介质覆盖目标盘，或者在 PE/Linux 环境里清空分区表、重新分区。安装器只改磁盘，不主动写 UEFI NVRAM；如果固件自己记住了 Zhaoxu 的启动项，可以在固件设置里删掉。

---

## 11. 安装结果自查

装完后目标盘上应该有两个可见分区。GPT 布局还会多一个 BIOS boot 区，里面是 stage2，不放文件系统：

| 分区 | 文件系统 | 卷标 | 内容 |
| --- | --- | --- | --- |
| BIOS boot（仅 GPT），LBA34 起 | 无 | 无 | stage2 引导代码 |
| ESP，从 LBA 2048 开始，最多 64 MiB | FAT32 | `ZHAOXUEFI` | `EFI/BOOT/BOOTX64.EFI`，以及需要手工补的 `EFI/BOOT/OSLDRX64.efi` |
| ext4 根卷，ESP 之后到备份 GPT 之前 | ext4 | `ZHAOXUROOT` | `/KERNEL.ELF`、`/KERNEL.BIN`、`/WQY.TTC`、`/WALLPAPER.RAW` |

在 Linux 主机上可以这样看：

```bash
lsblk -f
sudo blkid
```

能进 `8sh` 时，`disks` 也能看到角色。不过已安装盘的首扇区是 Zhaoxu 的 MBR，`disks` 可能仍把整块盘标成 `target`。判断是否装好，看串口里有没有 `Kernel: /KERNEL.ELF from ext4.` 和 `[vfs ] /mnt mounted (ext4 "ZHAOXUROOT")`。

---

Zhaoxu OS · Nexsteaduser

Copyright © 2026 Nexsteaduser. All Rights Reserved
