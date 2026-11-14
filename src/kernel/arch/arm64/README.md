# arm64 port layer

This directory holds the ARM64 platform code for Zhaoxu OS and does not participate in the x86_64 `kernel.elf` build.

- `gic.c`: GICv3 distributor, redistributor, and CPU interface initialization, providing the
  `gic_*` interfaces declared by `intc.h`. The x86 build uses the APIC/PIC front-end in `kernel/int/intc_x86.c`.
- `systick.c`: ARMv7-M/Cortex-M SysTick. The ARM64 generic timer (CNTPCT/CNTVCT)
  will hook into the same tick interface in a later port.

Enabling it: add these files to `C_SRCS` in the Makefile and replace the entry point and GDT/IDT layers;
current x86_64 hosts do not compile them.
