# Tasks

- [ ] Task 1: 搭建 Windows 原生构建与运行脚本
  - [ ] 1.1 编写 `build.ps1`（PowerShell 7+）：定位 `fasm.exe`（PATH 与 `tools/`），缺失时打印 `https://flatassembler.net/fasmw17335.zip` 与期望路径并以非零码退出；按 `-Headless` 参数生成 `build/config.inc`（`HEADLESS = 1/0`）；汇编主源产出 `out/BOOTX64.EFI` 并布置到 `esp/EFI/BOOT/BOOTX64.EFI`
  - [ ] 1.2 编写 `tools/find-ovmf.ps1`：在 QEMU 安装目录递归查找 `OVMF_CODE*.fd`，返回 CODE/VARS 一对真实路径，找不到时给出 QEMU 安装指引与非零码
  - [ ] 1.3 编写 `run.ps1`：用 CODE/VARS（VARS 每次从模板复制到 `out/`）以 `-M q35 -m 512M -smp 1` 启动 QEMU，`-drive format=raw,file=fat:rw:esp` 挂载引导目录，`-serial stdio`
- [ ] Task 2: UEFI 引导层
  - [ ] 2.1 编写 `boot/uefi.inc`：UEFI 结构偏移与常量（`EFI_SYSTEM_TABLE`、`EFI_BOOT_SERVICES`、`EFI_GRAPHICS_OUTPUT_PROTOCOL`、状态码），并附 `format PE64 EFI` 主文件骨架与有效 `.reloc` 段
  - [ ] 2.2 编写 `boot/entry.asm`：按 MS x64 ABI 保存 `RCX/RDX`，初始化 COM1 串口，取 `ConOut` 与 GOP 模式（帧缓冲地址/分辨率/像素步长），打印启动打点
  - [ ] 2.3 编写 `boot/paging.asm` 与 GDT：用 2MiB 大页构建覆盖 0–4GiB 及帧缓冲的页表，装载自有 GDT 与 CR3，随后 `GetMemoryMap → ExitBootServices`（失败重试 ≤5 次），交接到 `KernelMain`，并在串口打出各阶段 `BENCH_PHASE` 的 TSC 打点
- [ ] Task 3: 内核基础设施与双路输出
  - [ ] 3.1 编写 `kernel/drivers/serial.asm`（16550，115200 8N1）与 `kernel/lib/print.asm`（统一写串口 + 控制台的双路分发）
  - [ ] 3.2 引入真实 8×16 可打印 ASCII（0x20–0x7E）字模数据到 `kernel/font/font8x16.inc`（取自公开的 IBM VGA 8×16 字模，不得手编假字形），并实现 `kernel/drivers/gop_console.asm`：按像素步长绘制、换行、滚屏、整屏清空
  - [ ] 3.3 编写 `kernel/int/idt.asm`、`gdt.asm` 与 `exceptions.asm`：IDT 装载、0–31 异常例程（打印向量号/错误码/RIP 后 `cli; hlt`）、8259 PIC 重映射并只放行 IRQ1、PIT 100Hz 与节拍计数、`sti`
- [ ] Task 4: 内存管理
  - [ ] 4.1 编写 `kernel/mem/phys.asm`：以 `KernelMain` 保存的 UEFI 内存映射初始化页帧位图，提供 alloc/free 与总量统计
  - [ ] 4.2 编写 `kernel/mem/paging.asm`：页映射/解映射与 TLB 刷新
  - [ ] 4.3 编写 `kernel/mem/heap.asm`：空闲链表 + 首次适配，16 字节对齐，越界与耗尽返回失败码
- [ ] Task 5: PS/2 键盘输入
  - [ ] 5.1 编写 `kernel/drivers/ps2kbd.asm`：初始化控制器、IRQ1 例程读端口 0x60、扫描码 set1 解码（Shift/Caps 修饰）、EOI、环形缓冲，以及阻塞式 `key_read`
- [ ] Task 6: 命令行 Shell
  - [ ] 6.1 编写 `kernel/shell/line.asm`：行缓冲、退格重绘、上/下方向键历史（8 条）、回车提交
  - [ ] 6.2 编写 `kernel/shell/shell.asm`：提示符 `ravenel>`、启动横幅（系统名/版本/完整品牌名）、命令表分发与未知命令提示
  - [ ] 6.3 实现命令 `help`、`about`（品牌 + 规定版权串 + 邮箱，仅此处出现版权）、`info`（CPUID/内存映射/GOP/固件）、`mem`、`uptime`、`cls`、`echo`、`reboot`（端口 0xCF9）、`shutdown`（q35 端口 0x604 写 0x2000）
- [ ] Task 7: 性能基准
  - [ ] 7.1 编写 `kernel/bench/bench.asm`：PIT 校准 TSC 频率，测量 `idt_roundtrip_cycles`、`page_alloc_free_cycles`、`heap_alloc_free_cycles`、`fb_fill_mbps`、`text_render_cycles`，输出 `BENCH_JSON {...}`（含 `tsc_hz`），挂到 `bench` 命令
  - [ ] 7.2 编写 `tools/bench.ps1`：启动 QEMU 并计时至串口匹配 `-ReadyPattern`，得到 `boot_to_shell_ms`，合并环境信息与解析出的 `BENCH_JSON` 后追加写入 `bench/results.json`
  - [ ] 7.3 用 `-ImagePath`/`-ReadyPattern` 在同一 QEMU/OVMF 参数下跑通外部镜像对照路径；对照镜像（OpenXJ380 构建产物）在本机可得时采集并写入 `reference`，不可得时写 `null` 并记录采集命令
- [ ] Task 8: 无头构建变体
  - [ ] 8.1 用条件汇编实现 `HEADLESS`：关闭 GOP 控制台与键盘初始化、保留串口，确认 `build.ps1 -Headless` 与默认构建都能独立汇编通过

# Task Dependencies

- Task 2 依赖 Task 1（需要构建与运行链验证引导产物）
- Task 3 依赖 Task 2（`KernelMain` 与帧缓冲参数来自引导层）
- Task 4 依赖 Task 3（内存映射保存与中断上下文）
- Task 5 依赖 Task 3（IRQ1 与 IDT 就位）
- Task 6 依赖 Task 4、Task 5（堆/统计与输入）
- Task 7 依赖 Task 3、Task 4、Task 6（计时、分配探针、命令入口）
- Task 8 依赖 Task 3（控制台与输入模块已存在才有开关意义）
- Task 4 与 Task 5 可并行；Task 1 内 1.1/1.2/1.3 可并行编写
