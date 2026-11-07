# Ravenel 汇编操作系统（对标 XJ380）Spec

## Why

OpenXJ380（XJ380）已经开源，是 x86_64 + UEFI 单体内核的一个现成参照物，但它主体是 C/C++ + Clang/LLD。Ravenel 用纯汇编把同一条链路——UEFI 引导、64 位长模式、中断与异常、内存管理、双路输出、PS/2 输入、命令行交互——从头走通，并用可复现的基准把自身位置量化出来，既检验 FASM 单工具链能不能扛住一个真实内核，也给 XJ380 提供一个异构实现的对照组。

## What Changes

- 纯 FASM 汇编源码树（不含任何 C/C++ 源文件），单工具链一次汇编产出 UEFI 可引导镜像 `out/BOOTX64.EFI`。
- Windows 11 原生 PowerShell 7+ 脚本：`build.ps1`（构建）、`run.ps1`（QEMU/OVMF 运行）、`tools/bench.ps1`（基准采集）。
- UEFI 引导层：PE64 EFI 映像、MS x64 ABI 调用固件服务、GOP 图形输出、自建页表/GDT、`ExitBootServices` 后交接 `KernelMain`。
- 内核层：GDT、IDT、异常与 panic、8259 PIC、PIT 100Hz 节拍、16550 串口、GOP 文本控制台（8×16 字模）、PS/2 键盘、页帧分配器、内核堆。
- 交互层：带回退、方向键历史的行编辑 Shell 与内置命令。
- 基准层：内核内 `bench` 子命令（RDTSC 校准）+ 主机侧启动耗时采集，结果写入 `bench/results.json`。
- **非目标（本版不做）**：FAT/XAFS 文件系统读写、用户态与系统调用、多核调度、可加载内核模块、GUI、磁盘镜像打包、BIOS/MBR 引导。这些属于下一阶段，本版只做核心可交互内核。

### 与 XJ380 的对齐边界

XJ380 官方仓库已于 2026-08-04 移除 GUI，当前能力为：UEFI 引导与图形输出初始化、x86_64 单体内核（入口 `KernelMain`）、内建驱动/文件系统/进程线程/内存管理/系统调用、XAPI 用户态与命令行示例、可加载模块、`OPENXJ380CONFIG_CLEAR_RUN` 无头构建、Ninja 构建与 `XJ380.img` 镜像。本版对齐其中的核心子集，未对齐项在下表逐条标注，不在实现里含糊带过：

| XJ380 能力 | 本版状态 | 说明 |
| --- | --- | --- |
| UEFI 引导与图形输出初始化 | 对齐 | GOP 模式取帧缓冲，退出引导服务前完成 |
| x86_64 单体内核，入口 `KernelMain` | 对齐 | 同名入口，交接点在退出引导服务之后 |
| 内存管理 | 对齐（核心） | 页帧位图分配器 + 页映射 + 内核堆 |
| 内建驱动（输入/输出/计时） | 对齐（核心） | 串口、PS/2 键盘、GOP 控制台、PIT |
| 命令行程序 | 对齐 | 内置 Shell 取代其 `cli_shell` 示例 |
| 无头构建开关 | 对齐 | `build.ps1 -Headless` 条件汇编关闭控制台与输入 |
| 系统调用基础设施 | 未对齐 | 需要用户态，列为下一阶段 |
| 进程/线程 | 未对齐 | 需要调度器，列为下一阶段 |
| 文件系统（FAT32/XAFS） | 未对齐 | 列为下一阶段 |
| 可加载内核模块与 Socket ABI | 未对齐 | 列为下一阶段 |
| 磁盘镜像制作（`XJ380.img`） | 未对齐 | 本版用 QEMU VVFAT 目录挂载引导 |
| GUI / GlassUI | 双方均无 | XJ380 已移除，本版不做 |

### 性能基准口径

- 可与 XJ380 直接对比的只有一项外部可观测指标：`boot_to_shell_ms`，即从 QEMU 进程启动到串口出现命令提示符的墙上时间。`tools/bench.ps1` 必须做成通用脚本，接受 `-ImagePath`/`-ReadyPattern` 参数，对任意 UEFI 引导产物在同一组 QEMU/OVMF 参数下测量，不硬编码本项目文件名。
- 其余指标只有本内核内部探针（XJ380 侧没有等价探针，不做跨内核强行换算），以绝对值记录：`idt_roundtrip_cycles`、`page_alloc_free_cycles`、`heap_alloc_free_cycles`、`fb_fill_mbps`、`text_render_cycles`。
- OpenXJ380 官方仓库未发布任何二进制镜像，其对照数据属于可选项：仅当其构建产物或镜像在本机可得时才采集，采集成功才写入 `bench/results.json` 的 `reference` 字段；不可得时该字段写 `null` 并在同条记录里给出采集命令，不允许编造数值。

## Impact

- Affected specs: 全新项目，无既有 spec 受影响。
- Affected code: 新增 `boot/`、`kernel/`、`tools/`、`esp/`、`bench/` 与仓库根部 PowerShell 脚本；当前仓库仅含 `.vscode/`，无既有源码被修改。

## ADDED Requirements

### Requirement: Windows 原生构建链

系统 SHALL 在 Windows 11 + PowerShell 7+ 下完成从源码到可引导镜像的全链路构建，不依赖 WSL、bash 或 PowerShell 5.x。

#### Scenario: 正常构建

- **WHEN** 在仓库根执行 `./build.ps1`
- **THEN** 脚本定位 `fasm.exe`，用 `format PE64 EFI` 一次性汇编全部源码，产出 `out/BOOTX64.EFI`，并把该文件布置到 `esp/EFI/BOOT/BOOTX64.EFI`，任一步失败则以非零退出码中止并打印真实原因。

#### Scenario: 工具缺失

- **WHEN** PATH 与仓库 `tools/` 中都找不到 `fasm.exe`，或找不到 `qemu-system-x86_64.exe`
- **THEN** 脚本打印该工具的真实官方下载地址（FASM 为 `https://flatassembler.net/fasmw17335.zip`）与期望路径，然后以非零退出码退出，不生成任何半成品产物。

### Requirement: UEFI 引导与内核交接

系统 SHALL 以 UEFI 应用形式引导，在退出引导服务前完成自有页表与 GDT 的装载，并把控制权交给 `KernelMain`。

#### Scenario: 启动成功

- **WHEN** QEMU 以 OVMF 固件、`-M q35` 启动并加载 `esp/EFI/BOOT/BOOTX64.EFI`
- **THEN** 引导代码按 MS x64 调用规范（16 字节栈对齐、32 字节影子空间）调用 `GetSystemTable` 侧的 `ConOut`/`GOP`/`BootServices`，取到帧缓冲地址与分辨率，构建覆盖固件与帧缓冲的页表，装载自有 GDT，调用 `ExitBootServices`，随后调用 `KernelMain` 且不再返回固件。

#### Scenario: 引导服务退出失败

- **WHEN** `ExitBootServices` 返回 `EFI_INVALID_PARAMETER`
- **THEN** 代码重新获取内存映射并最多重试 5 次，仍失败则通过串口打印失败码并 `cli; hlt` 死循环，不继续执行未受控的内核代码。

### Requirement: 内核基础设施与双路输出

系统 SHALL 在退出引导服务后立即具备可用的中断、异常与输出能力。

#### Scenario: 中断与异常就位

- **WHEN** `KernelMain` 开始执行
- **THEN** 系统装载自有 IDT，0–31 号异常全部绑定处理例程（打印向量号、错误码、RIP 后进入 `panic` 死循环），8259 PIC 重映射到 0x20/0x28 并仅放行 IRQ1，PIT 配置为 100Hz 并开始计节拍，随后 `sti`。

#### Scenario: 双路输出一致

- **WHEN** 内核任何位置调用打印例程
- **THEN** 同一段文本同时写入 COM1 串口（115200 8N1）与 GOP 帧缓冲文本控制台；帧缓冲控制台使用嵌入的 8×16 可打印 ASCII 字模，支持换行、滚屏与整屏清空。

### Requirement: 内存管理

系统 SHALL 在退出引导服务时保存 UEFI 内存映射，并基于它提供页帧与堆两级分配。

#### Scenario: 页帧分配

- **WHEN** 内核请求分配 N 个 4KiB 页帧
- **THEN** 分配器从位图中找出连续的空闲帧并返回物理地址，标记为已用；释放时还原为可复用；`mem` 命令能报出总帧数、已用帧数与空闲帧数。

#### Scenario: 堆分配

- **WHEN** 内核请求分配或释放一段堆内存
- **THEN** 堆分配器（空闲链表 + 首次适配）返回 16 字节对齐的可用区域，越界与耗尽场景返回失败而非写坏邻接数据。

### Requirement: PS/2 键盘输入

系统 SHALL 通过 IRQ1 接收扫描码并提供可阻塞的字符读取。

#### Scenario: 按键进入缓冲区

- **WHEN** 用户在 QEMU 窗口敲下任意可打印键
- **THEN** IRQ1 处理例程读取端口 0x60、解析扫描码 set1（含 Shift/Caps 修饰）、发送 EOI，并把对应 ASCII 字符写入环形缓冲区，读取侧阻塞等待直到有字符可读。

### Requirement: 命令行 Shell

系统 SHALL 提供一个可交互的行编辑 Shell 与内置命令集。

#### Scenario: 命令执行

- **WHEN** 用户输入 `help`、`about`、`info`、`mem`、`uptime`、`cls`、`echo <文本>`、`bench`、`reboot`、`shutdown` 之一并回车
- **THEN** Shell 分发到对应处理例程并在提示符下输出结果；未知命令打印 `unknown command` 与该命令原文，不崩溃。

#### Scenario: 行编辑

- **WHEN** 用户输入过程中按下退格、上方向键或下方向键
- **THEN** 退格删除光标前一个字符并刷新行；上/下方向键在最近 8 条历史命令间切换回填，回车执行后该命令进入历史。

### Requirement: 性能基准

系统 SHALL 提供内核内与主机侧两层基准，结果为机器可读 JSON。

#### Scenario: 内核内基准

- **WHEN** 用户输入 `bench`
- **THEN** 内核用 RDTSC 测量 `idt_roundtrip_cycles`（软件中断经 IDT 分发到 `iret` 返回的平均周期，≥10000 次）、`page_alloc_free_cycles`（≥10000 次配对）、`heap_alloc_free_cycles`（≥10000 次配对）、`fb_fill_mbps`、`text_render_cycles`，TSC 频率先由 PIT 校准，然后在屏幕与串口同时打印一行 `BENCH_JSON {...}`，键名固定且含 `tsc_hz`。

#### Scenario: 主机侧启动耗时

- **WHEN** 执行 `./tools/bench.ps1`
- **THEN** 脚本启动 QEMU、以高精度计时器计时、等待串口出现 `-ReadyPattern`（默认为本项目提示符）、得到 `boot_to_shell_ms`，并把环境信息（QEMU 机器类型、内存、vCPU 数、OVMF 路径、镜像路径）与两层指标合并写入 `bench/results.json`；已有记录则追加而非覆盖。

#### Scenario: 对照采集

- **WHEN** 执行 `./tools/bench.ps1 -ImagePath <路径> -ReadyPattern <正则>`
- **THEN** 脚本用完全相同的 QEMU/OVMF 参数测量该外部 UEFI 产物，结果作为 `reference` 记录写入同一文件；当对照镜像不可得时，`reference` 写 `null` 并同时记录触发该结果的采集命令。

### Requirement: 无头构建

系统 SHALL 支持关闭控制台与输入的构建变体，对齐 XJ380 的 `OPENXJ380CONFIG_CLEAR_RUN`。

#### Scenario: 无头构建

- **WHEN** 执行 `./build.ps1 -Headless`
- **THEN** 构建脚本生成 `build/config.inc`（`HEADLESS = 1`）后再汇编，控制台渲染、帧缓冲打印与键盘初始化被条件汇编剔除，串口输出保留；不带 `-Headless` 时生成 `HEADLESS = 0`，两种变体都必须能独立汇编通过。

### Requirement: 品牌与版权呈现

系统 SHALL 在合适位置完整呈现品牌名 `Nexsteaduser` 与规定的版权串，且不在多处重复轰炸。

#### Scenario: 启动横幅与 about

- **WHEN** 内核启动完成打印横幅，或用户输入 `about`
- **THEN** 横幅显示系统名 `Ravenel` 与版本号 `0.1.0` 及完整品牌名 `Nexsteaduser`；`about` 输出包含 `Brand: Nexsteaduser`、`Copyright © 2026 Nexsteaduser. All Rights Reserved.` 与 `admin@nexsteaduser.com`，不出现团队名或个人名；版权串全系统仅出现在 `about` 一处。

## MODIFIED Requirements

无，全新项目。

## REMOVED Requirements

无，全新项目。
