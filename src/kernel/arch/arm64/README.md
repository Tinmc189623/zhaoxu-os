# arm64 移植层

本目录存放 Zhaoxu OS 的 ARM64 平台代码，不参与 x86_64 的 `kernel.elf` 构建。

- `gic.c`：GICv3 分发器、重分发器与 CPU 接口初始化，提供 `intc.h` 声明的
  `gic_*` 接口。x86 构建使用 `kernel/int/intc_x86.c` 中的 APIC/PIC 前端。
- `systick.c`：ARMv7-M/Cortex-M SysTick。ARM64 通用定时器（CNTPCT/CNTVCT）
  在后续移植中以同一节拍接口接入。

启用方式：在 Makefile 的 `C_SRCS` 中加入这些文件，并替换入口与 GDT/IDT 层；
当前 x86_64 主机不会编译它们。
