# Zhaoxu OS

Zhaoxu OS 是一个面向 x86_64 + UEFI 的操作系统，内核代号 **Yuanshuo**，当前版本 0.1.0。系统在 UEFI 固件上完成引导，进入自建页表的 64 位内核，提供中断与异常处理、两级内存管理、PS/2 输入、双路输出（串口 + GOP 帧缓冲）和可交互的命令行 Shell。

## 技术栈

多种语言各司其职，不做单一语言教条：

| 语言 | 职责 |
| --- | --- |
| FASM | UEFI 引导层、GDT/IDT 装载、中断桩等紧贴硬件的底层代码 |
| C | 内核核心：内存管理、中断分发、控制台、内核服务 |
| Ada | 驱动：16550 串口、PS/2 键盘、PIT 定时器、GOP 图形输出 |
| C++ | 上层应用：Shell、行编辑器、内置命令 |

## 构建与运行

构建链跑在 WSL2（Ubuntu 26.04）中，Windows 侧只做入口。需要 PowerShell 7+。

```powershell
# 构建出 out/BOOTX64.EFI 并布置到 esp/EFI/BOOT/
./src/build.ps1

# 无头变体（关闭控制台与输入，仅保留串口）
./src/build.ps1 -Headless

# 用 QEMU + OVMF 启动，串口接到当前终端
./src/run.ps1
```

依赖的工具链：

- WSL2 Ubuntu 26.04：`gcc`（15.x）、`g++`、`gnat`/`gprbuild`（GNAT 14）、`lld`、`qemu-system-x86_64`、`ovmf`
- `fasm` 1.73.34（仓库自带 `src/tools/fasm`，缺失时执行 `src/tools/install-fasm.sh` 安装）

Windows 侧脚本会把源码路径换算成 WSL 路径后调用 WSL 内的 `make`，产物仍落在仓库 `src/out/` 与 `src/esp/` 下。

## 目录结构

```
Zhaoxu OS/
├── LICENSE                  Apache License 2.0 全文
├── AGENTS.md                仓库约定
├── src/
│   ├── build.ps1            构建入口（转发 WSL2 make）
│   ├── run.ps1              QEMU/OVMF 运行入口
│   ├── Makefile             WSL2 内的编译与链接规则
│   ├── boot/                FASM：UEFI 引导层
│   ├── arch/x86_64/         FASM：GDT/IDT/中断桩
│   ├── kernel/              C：内核核心
│   ├── drivers/             Ada：外设驱动
│   ├── apps/                C++：Shell 等上层应用
│   ├── tools/               fasm 与辅助脚本
│   ├── esp/                 构建出的 UEFI 引导目录
│   └── out/                 构建产物
└── .trae/specs/             设计规格与任务清单
```

## 引导流程

1. UEFI 固件加载 `esp/EFI/BOOT/BOOTX64.EFI`（FASM 引导层，PE64 EFI 映像）；
2. 引导层取得 GOP 帧缓冲与 UEFI 内存映射，建立覆盖物理内存的页表并装载 GDT；
3. 调用 `ExitBootServices` 退出固件服务，之后不再回调固件；
4. 跳转到内核入口 `KernelMain`，装载 IDT、重映射 8259 PIC、启动 PIT 100Hz 节拍；
5. 初始化驱动与内存管理，进入 Shell 提示符。

## 当前状态

已就位：Windows/WSL2 构建与运行脚本、PE 头校验、OVMF 固件探测、仓库内 fasm。

开发中：UEFI 引导层、内核核心、Ada 驱动、Shell 与性能基准，任务拆解见 [.trae/specs/](.trae/specs/)。

## 许可证

本项目采用 [Apache License 2.0](LICENSE)。

Copyright © 2026 Nexsteaduser. All Rights Reserved. 联系邮箱：admin@nexsteaduser.com
