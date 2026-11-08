# Zhaoxu OS

Zhaoxu OS 是一个面向 x86_64 + UEFI 的多语言操作系统，内核代号 **Yuanshuo**，当前版本 **0.2.0**。引导层用 FASM 直接生成 PE64 EFI 映像，建立恒等映射与 higher-half 页表；退出 UEFI Boot Services 后进入 C 内核，外设驱动用 Ada，交互 Shell 用 C++20 编写，全部链接进同一个内核镜像。

![Zhaoxu OS shell](docs/screenshot.png)

## 已实现

**引导（FASM）**

- PE64 EFI 应用，位置无关代码，GOP 帧缓冲探测（1280x800 / BGRX）
- 传递 ACPI RSDP 与 UEFI 内存映射
- 前 4 GiB 恒等映射（2 MiB 大页）+ 16 MiB higher-half 内核窗口
- 复制内核镜像到页对齐物理内存，`ExitBootServices` 后切换 CR3，跳转 `0xffffffff80000000`

**内核（C）**

- GDT/TSS（双故障与 NMI 使用独立 IST 栈）、IDT、8259 PIC 重映射
- PIT 100 Hz 节拍、PS/2 键盘 IRQ、COM1 串口轮询输入
- 异常处理：向量、错误码、CR2、RIP/RSP/寄存器现场，随后停机
- 物理页位图分配器（4 GiB 范围）、4 KiB 粒度虚拟映射、4 MiB 内核堆
- `kprintf` / `ksnprintf`、字符串库、图形 + 串口双路控制台
- 8x16 字体渲染、滚动、16 色 VGA 调色板与 ANSI 串口颜色映射

**驱动（Ada）**

- 16550 UART、PIT、PS/2 控制器、GOP 矩形填充 / 字形绘制 / 行复制
- 通过 `pragma Export, Convention => C` 暴露 C 符号，端口 I/O 由 C 侧辅助函数完成

**平台**

- CPUID：厂商、品牌、家族/型号/步进、逻辑 CPU 数与特性位
- CMOS RTC：BCD/二进制、12/24 小时制处理
- PCI：配置空间扫描与设备分类
- ACPI：RSDP 校验、RSDT/XSDT 遍历、FADT 解析、DSDT 中 `_S5` AML 解析
- ACPI 关机 / 重启，并为 QEMU q35、i440fx 与键盘控制器提供回退路径

**Shell（C++20）**

| 命令 | 说明 |
| --- | --- |
| `help` | 命令列表 |
| `clear` | 清屏 |
| `echo <text>` | 输出文本 |
| `version` | 版本、构建时间、工具链 |
| `uname` | 系统标识 |
| `mem` | 物理内存与内核堆统计 |
| `uptime` | 运行时间与节拍数 |
| `cpu` | CPUID 详情与特性 |
| `date` | CMOS 实时时钟 |
| `pci` | PCI 设备枚举 |
| `acpi` | ACPI 电源管理信息 |
| `bench` | memset 带宽与 TSC ALU 基准 |
| `sleep <ms>` | 休眠指定毫秒 |
| `color <fg> <bg>` | 设置控制台颜色（0-15） |
| `neofetch` | 系统摘要 |
| `reboot` | ACPI 重启 |
| `shutdown` | ACPI 关机 |
| `panic` | 触发页故障，验证异常处理 |

行编辑支持退格、`Ctrl+L` 清屏、`Ctrl+C` 取消当前行。

## 构建与运行

构建链运行在 WSL2（Ubuntu 26.04）中，Windows 侧脚本只做转发。需要 PowerShell 7+。

```powershell
# 构建并部署到 esp/EFI/BOOT/BOOTX64.EFI
./src/build.ps1

# 仅串口输出（无图形）的构建变体
./src/build.ps1 -Headless

# QEMU + OVMF 启动，串口接到当前终端
./src/run.ps1

# 使用 WSLg 打开 QEMU 图形窗口
./src/run.ps1 -Display gtk
```

也可以直接在 WSL 中构建：

```bash
cd '/mnt/g/Apps/OS/x86/AMD64/Zhaoxu OS/src'
make -j4
make run                 # 无头 + 串口
make run QEMU_DISPLAY="-display gtk"
```

依赖：

- WSL2 Ubuntu 26.04：`gcc`、`g++`、`gcc-14`（提供 `gnat1`）、`ld.lld`、`objcopy`、`qemu-system-x86_64`、`ovmf`
- Flat Assembler 1.73.34：仓库自带 `src/tools/fasm`；缺失时可执行 `src/tools/install-fasm.sh`

## 目录结构

```
Zhaoxu OS/
├── LICENSE                  Apache License 2.0 全文
├── THIRD_PARTY_NOTICES.md   第三方代码与字体声明
├── AGENTS.md                仓库约定
├── docs/                    截图
└── src/
    ├── Makefile             WSL2 内的编译、链接与 QEMU 规则
    ├── build.ps1            构建入口（转发 WSL2 make）
    ├── run.ps1              QEMU/OVMF 运行入口
    ├── boot/                FASM：UEFI 引导层与 UEFI 常量
    ├── kernel/
    │   ├── arch/x86_64/     入口、GDT/TSS、中断桩、PIC、CPUID、端口 I/O
    │   ├── int/             IDT 与异常/IRQ 分发
    │   ├── drivers/         PIT、键盘、RTC、PCI、ACPI
    │   ├── mem/             PMM、VMM、堆
    │   ├── lib/             字符串、printf、控制台、C++ 运行时
    │   ├── font/            font8x8 字体数据
    │   └── include/         内核公共头文件
    ├── drivers/             Ada：串口、PIT、PS/2、GOP
    ├── apps/                C++：Shell
    ├── esp/                 构建出的 UEFI 引导目录
    └── out/                 构建产物与测试日志
```

## 引导流程

1. UEFI 固件加载 `esp/EFI/BOOT/BOOTX64.EFI`（FASM 生成的 PE64 EFI 映像）。
2. 引导层取得 GOP 帧缓冲、ACPI RSDP 与 UEFI 内存映射。
3. 分配并复制内核 flat binary，建立恒等映射与 higher-half 页表。
4. 调用 `ExitBootServices`，切换 CR3，跳转到 higher-half 内核入口。
5. 内核建立 GDT/IDT，初始化 PMM/VMM/堆、CPUID、ACPI、RTC 与驱动。
6. 启用中断，打印启动日志，进入 C++ Shell 提示符。

内存布局要点：

- 内核虚拟基址：`0xffffffff80000000`，内核栈顶 `0xffffffff80f00000`
- 内核堆虚拟基址：`0xffffffff90000000`
- 物理内存管理范围：前 4 GiB，4 KiB 页

## 当前限制

- 单核、无调度器、无用户态（Shell 运行在内核态）
- 无文件系统、网络协议栈与 USB
- 输入仅 PS/2 键盘与 COM1 串口，显示输出依赖 UEFI GOP 帧缓冲
- ACPI `_S5` 通过 DSDT AML 模式匹配解析，覆盖常见固件；无法匹配时回退探测

## 许可证

本项目采用 [Apache License 2.0](LICENSE)。第三方组件声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

Copyright © 2026 Nexsteaduser. All Rights Reserved. 联系邮箱：admin@nexsteaduser.com
