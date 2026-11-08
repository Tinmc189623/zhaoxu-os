# Zhaoxu OS build system (run inside WSL2)

CC      ?= gcc
CXX     ?= g++
ADA     ?= gcc-14
LD      := ld.lld
OBJCOPY ?= objcopy
FASM    ?= tools/fasm

BUILD   := build
OUT     := out
ESP     := esp/EFI/BOOT

CFLAGS  := -m64 -std=gnu11 -O2 -g -ffreestanding -fno-pie -fno-pic \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
           -mno-sse -mno-sse2 -mno-mmx -mno-avx \
           -Wall -Wextra -Wno-unused-parameter \
           -Ikernel/include -Ikernel -Ikernel/font
CXXFLAGS := $(filter-out -std=gnu11,$(CFLAGS)) -std=gnu++20 \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics \
            -fno-use-cxa-atexit
ADAFLAGS := -c -gnatp -gnat2022 -O2 -g -fno-pie \
            -fno-stack-protector -mno-red-zone -mcmodel=kernel \
            -mgeneral-regs-only -mno-sse -mno-sse2 -mno-mmx -mno-avx \
            -Idrivers
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib -static \
           --build-id=none -z max-page-size=0x1000

C_SRCS := kernel/main.c \
          kernel/arch/x86_64/io.c \
          kernel/arch/x86_64/gdt.c \
          kernel/arch/x86_64/pic.c \
          kernel/arch/x86_64/cpu.c \
          kernel/int/idt.c \
          kernel/drivers/timer.c \
          kernel/drivers/keyboard.c \
          kernel/drivers/rtc.c \
          kernel/drivers/pci.c \
          kernel/drivers/acpi.c \
          kernel/lib/string.c \
          kernel/lib/printf.c \
          kernel/lib/console.c \
          kernel/font/font.c \
          kernel/mem/pmm.c \
          kernel/mem/vmm.c \
          kernel/mem/heap.c
ASM_SRCS := kernel/arch/x86_64/entry.S \
            kernel/arch/x86_64/gdt_flush.S \
            kernel/arch/x86_64/isr.S
ADA_SRCS := drivers/serial.adb \
            drivers/pit.adb \
            drivers/ps2.adb \
            drivers/gop.adb
CXX_SRCS := kernel/lib/cxx.cpp \
            apps/shell.cpp

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
        $(patsubst %.S,$(BUILD)/%.o,$(ASM_SRCS)) \
        $(patsubst %.cpp,$(BUILD)/%.o,$(CXX_SRCS)) \
        $(patsubst %.adb,$(BUILD)/%.o,$(ADA_SRCS))

HEADLESS ?= 0

.PHONY: all clean run FORCE

all: $(OUT)/BOOTX64.EFI $(ESP)/BOOTX64.EFI

$(BUILD)/config.inc: FORCE
	@mkdir -p $(BUILD)
	@printf 'HEADLESS equ $(HEADLESS)\n' > $@

FORCE:

$(BUILD)/kernel_blob.inc: $(OUT)/kernel.bin
	@printf "kernel_blob:\nfile 'out/kernel.bin'\nkernel_blob_end:\n" > $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) -m64 -c $< -o $@

$(BUILD)/%.o: %.adb
	@mkdir -p $(dir $@)
	$(ADA) $(ADAFLAGS) -o $@ $<

$(OUT)/kernel.elf: $(OBJS) kernel/linker.ld
	@mkdir -p $(OUT)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(OUT)/kernel.bin: $(OUT)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(OUT)/BOOTX64.EFI: boot/main.asm boot/uefi.inc $(BUILD)/config.inc $(BUILD)/kernel_blob.inc
	@mkdir -p $(OUT)
	$(FASM) boot/main.asm $@

$(ESP)/BOOTX64.EFI: $(OUT)/BOOTX64.EFI
	@mkdir -p $(ESP)
	cp $< $@

clean:
	rm -rf $(BUILD) $(OUT)/kernel.elf $(OUT)/kernel.bin $(OUT)/BOOTX64.EFI $(ESP)/BOOTX64.EFI

OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
QEMU_MACHINE ?= q35
QEMU_MEM ?= 512M
QEMU_SMP ?= 1
QEMU_DISPLAY ?= -display none

run: all
	cp $(OVMF_VARS) $(OUT)/OVMF_VARS.fd
	qemu-system-x86_64 -M $(QEMU_MACHINE) -m $(QEMU_MEM) -smp $(QEMU_SMP) \
	  -drive if=pflash,format=raw,unit=0,readonly=on,file=$(OVMF_CODE) \
	  -drive if=pflash,format=raw,unit=1,file=$(OUT)/OVMF_VARS.fd \
	  -drive format=raw,file=fat:rw:esp \
	  -net none -serial stdio -monitor none $(QEMU_DISPLAY) -no-reboot
