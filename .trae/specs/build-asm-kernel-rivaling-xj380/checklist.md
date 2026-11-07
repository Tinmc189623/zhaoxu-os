# Checklist

## 构建链

- [ ] `build.ps1` 仅用 PowerShell 7+ 语法，无 bash/PowerShell 5.x 专属写法
- [ ] 缺失 `fasm.exe` 时打印 `https://flatassembler.net/fasmw17335.zip` 与期望路径并以非零码退出，不产出半成品
- [ ] `build.ps1` 与 `run.ps1` 按 `-Headless` 生成 `build/config.inc`，默认与无头两种变体都能汇编通过
- [ ] `tools/find-ovmf.ps1` 返回真实存在的 OVMF CODE/VARS 路径，找不到时报错而非静默继续
- [ ] `run.ps1` 使用 `-M q35 -m 512M -smp 1`、`fat:rw:esp`、`-serial stdio`，VARS 每次从模板复制
- [ ] `./build.ps1` 后 `esp/EFI/BOOT/BOOTX64.EFI` 存在且为 PE32+ EFI 应用

## 引导与内核

- [ ] 源码为纯 FASM 汇编，全仓库无 `.c`/`.cpp`/`.h` 实现文件
- [ ] 主文件使用 `format PE64 EFI` 并含非空 `.reloc` 修复段
- [ ] 固件调用全部遵循 MS x64 ABI（16 字节栈对齐、32 字节影子空间）
- [ ] 退出引导服务前装载自有页表与 GDT，退出后不再回调固件引导服务
- [ ] `ExitBootServices` 失败会重取内存映射并重试，上限 5 次，之后串口报错并 `cli; hlt`
- [ ] 入口交接点为 `KernelMain`，与 XJ380 内核入口命名一致
- [ ] 0–31 号异常均有处理例程并打印向量号/错误码/RIP 后停机
- [ ] 8259 PIC 重映射到 0x20/0x28，仅放行 IRQ1；PIT 100Hz 节拍计数生效
- [ ] 串口与 GOP 控制台双路输出同一文本，GOP 控制台支持换行、滚屏、清屏
- [ ] 8×16 字模为真实可打印 ASCII（0x20–0x7E）点阵数据，非编造的占位字形
- [ ] PS/2 键盘 IRQ1 读取端口 0x60、支持 Shift/Caps、发 EOI、写入环形缓冲

## 内存与交互

- [ ] 页帧分配器基于 UEFI 内存映射初始化，`mem` 能报总帧/已用/空闲
- [ ] 堆分配器 16 字节对齐，越界与耗尽返回失败码而不破坏邻接数据
- [ ] 行编辑支持退格与上/下方向键历史（8 条）
- [ ] Shell 命令 `help`/`about`/`info`/`mem`/`uptime`/`cls`/`echo`/`bench`/`reboot`/`shutdown` 全部真实可用，未知命令不崩溃
- [ ] `reboot` 走端口 0xCF9，`shutdown` 走 q35 端口 0x604 写 0x2000

## 基准与对标

- [ ] `bench` 命令输出含 `tsc_hz`、`idt_roundtrip_cycles`、`page_alloc_free_cycles`、`heap_alloc_free_cycles`、`fb_fill_mbps`、`text_render_cycles` 的 `BENCH_JSON` 行，屏幕与串口同时打印
- [ ] TSC 频率由 PIT 校准，各指标迭代次数 ≥10000
- [ ] `tools/bench.ps1` 支持 `-ImagePath` 与 `-ReadyPattern`，对任意 UEFI 产物可用，未硬编码本项目文件名
- [ ] `bench/results.json` 含 QEMU 机器类型/内存/vCPU/OVMF 路径/镜像路径与两层指标，重复执行为追加而非覆盖
- [ ] 对照镜像不可得时 `reference` 为 `null` 且记录采集命令，未出现编造的 XJ380 数值
- [ ] 本版未对齐项（文件系统、系统调用、进程线程、模块、镜像打包、GUI）在实现中确实未被假实现

## 品牌与代码规范

- [ ] 启动横幅含 `Ravenel`、版本 `0.1.0` 与完整品牌名 `Nexsteaduser`
- [ ] `about` 输出含 `Brand: Nexsteaduser`、`Copyright © 2026 Nexsteaduser. All Rights Reserved.`、`admin@nexsteaduser.com`
- [ ] 版权串仅出现在 `about` 一处，未在文件或界面中批量加水印
- [ ] `about` 与启动横幅不出现团队名或个人名
- [ ] 品牌名 `Nexsteaduser` 未被拆分、简写或替换为其它称谓
- [ ] 每个全局过程有函数级注释，其余注释遵守最小注释原则
- [ ] 全部为真实实现，无占位符、无模拟代码、无玩具级 stub
