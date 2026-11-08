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
          kernel/drivers/ahci.c \
          kernel/fs/initramfs.c \
          kernel/fs/fat32.c \
          kernel/fs/vfs.c \
          kernel/proc/process.c \
          kernel/lib/string.c \
          kernel/lib/printf.c \
          kernel/lib/console.c \
          kernel/font/font.c \
          kernel/mem/pmm.c \
          kernel/mem/vmm.c \
          kernel/mem/heap.c
ASM_SRCS := kernel/arch/x86_64/entry.S \
            kernel/arch/x86_64/gdt_flush.S \
            kernel/arch/x86_64/isr.S \
            kernel/arch/x86_64/user.S
ADA_SRCS := drivers/serial.adb \
            drivers/pit.adb \
            drivers/ps2.adb \
            drivers/gop.adb
CXX_SRCS := kernel/lib/cxx.cpp \
            apps/shell.cpp

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
        $(patsubst %.S,$(BUILD)/%.o,$(ASM_SRCS)) \
        $(patsubst %.cpp,$(BUILD)/%.o,$(CXX_SRCS)) \
        $(patsubst %.adb,$(BUILD)/%.o,$(ADA_SRCS)) \
        $(BUILD)/initramfs.o

HEADLESS ?= 0

.PHONY: all clean run iso run-iso FORCE

all: $(OUT)/BOOTX64.EFI $(ESP)/BOOTX64.EFI

$(BUILD)/config.inc: FORCE
	@mkdir -p $(BUILD)
	@printf 'HEADLESS equ $(HEADLESS)\n' > $@

FORCE:

# ---- Ring 3 programs -------------------------------------------------
USER_CFLAGS := -m64 -std=gnu11 -O2 -ffreestanding -fno-pie -fno-pic \
               -fno-stack-protector -fno-asynchronous-unwind-tables \
               -mno-red-zone -mcmodel=large -mno-sse -mno-sse2 \
               -mno-mmx -mno-avx -Wall -Wextra -Wno-unused-parameter
USER_LDFLAGS := -nostdlib -static -no-pie -Wl,--build-id=none \
                -Wl,-T,user/user.ld -Wl,-z,max-page-size=0x1000

$(OUT)/%.elf: user/%.c user/userlib.c user/userlib.h user/user.ld
	@mkdir -p $(OUT)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/$*.c user/userlib.c

# ---- initramfs -------------------------------------------------------
RAMFS := $(BUILD)/initramfs
RAMFS_FILES := $(RAMFS)/bin/init $(RAMFS)/etc/motd $(RAMFS)/etc/version \
               $(RAMFS)/mnt

$(RAMFS)/bin/init: $(OUT)/init.elf
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/etc/%: initramfs/etc/%
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/mnt:
	@mkdir -p $@

$(OUT)/initramfs.tar: $(RAMFS_FILES)
	@mkdir -p $(OUT)
	tar --format=ustar --owner=0 --group=0 --numeric-owner \
	    -cf $@ -C $(RAMFS) .

$(BUILD)/initramfs.o: $(OUT)/initramfs.tar
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.initramfs,alloc,load,readonly,data,contents \
	  --redefine-sym _binary_out_initramfs_tar_start=initramfs_start \
	  --redefine-sym _binary_out_initramfs_tar_end=initramfs_end \
	  --redefine-sym _binary_out_initramfs_tar_size=initramfs_size \
	  $< $@

# ---- FAT32 data volume ----------------------------------------------
DATA_IMG := $(OUT)/data.img

$(DATA_IMG): $(OUT)/hello.elf initramfs/data/hello.txt initramfs/data/info.txt
	@mkdir -p $(OUT)
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
	mkfs.vfat -F 32 -n ZHAOXUDATA $@ >/dev/null
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::/DOCS
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/hello.txt ::/HELLO.TXT
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(OUT)/hello.elf ::/HELLO.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/info.txt ::/DOCS/INFO.TXT

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
	rm -rf $(BUILD) $(OUT)/kernel.elf $(OUT)/kernel.bin $(OUT)/BOOTX64.EFI \
	       $(OUT)/init.elf $(OUT)/hello.elf $(OUT)/initramfs.tar \
	       $(OUT)/data.img $(OUT)/efi.img $(OUT)/zhaoxu-os.iso \
	       $(ESP)/BOOTX64.EFI

OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
QEMU_MACHINE ?= q35
QEMU_MEM ?= 512M
QEMU_SMP ?= 1
QEMU_DISPLAY ?= -display none

run: all $(DATA_IMG)
	cp $(OVMF_VARS) $(OUT)/OVMF_VARS.fd
	qemu-system-x86_64 -M $(QEMU_MACHINE) -m $(QEMU_MEM) -smp $(QEMU_SMP) \
	  -drive if=pflash,format=raw,unit=0,readonly=on,file=$(OVMF_CODE) \
	  -drive if=pflash,format=raw,unit=1,file=$(OUT)/OVMF_VARS.fd \
	  -drive format=raw,file=fat:rw:esp \
	  -drive if=none,id=zdata,format=raw,file=$(DATA_IMG) \
	  -device ide-hd,drive=zdata,bus=ide.1 \
	  -net none -serial stdio -monitor none $(QEMU_DISPLAY) -no-reboot

# ---- UEFI ISO --------------------------------------------------------
ISO := $(OUT)/zhaoxu-os.iso
EFI_IMG := $(OUT)/efi.img
ISO_ROOT := $(BUILD)/iso
ISO_ESP_MB ?= 16

$(EFI_IMG): $(OUT)/BOOTX64.EFI
	@mkdir -p $(OUT)
	dd if=/dev/zero of=$@ bs=1M count=$(ISO_ESP_MB) status=none
	mkfs.vfat -F 16 -n ZHAOXUEFI $@ >/dev/null
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::/EFI
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::/EFI/BOOT
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $< ::/EFI/BOOT/BOOTX64.EFI

$(ISO): $(OUT)/BOOTX64.EFI $(EFI_IMG)
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/EFI/BOOT
	cp $(OUT)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	cp $(EFI_IMG) $(ISO_ROOT)/efi.img
	xorriso -as mkisofs -o $@ -V ZHAOXU_OS -J -R \
	  -e efi.img -no-emul-boot -isohybrid-gpt-basdat $(ISO_ROOT)

iso: $(ISO)

run-iso: $(ISO) $(DATA_IMG)
	cp $(OVMF_VARS) $(OUT)/OVMF_VARS.fd
	qemu-system-x86_64 -M $(QEMU_MACHINE) -m $(QEMU_MEM) -smp $(QEMU_SMP) \
	  -drive if=pflash,format=raw,unit=0,readonly=on,file=$(OVMF_CODE) \
	  -drive if=pflash,format=raw,unit=1,file=$(OUT)/OVMF_VARS.fd \
	  -cdrom $(ISO) \
	  -drive if=none,id=zdata,format=raw,file=$(DATA_IMG) \
	  -device ide-hd,drive=zdata,bus=ide.1 \
	  -net none -serial stdio -monitor none $(QEMU_DISPLAY) -no-reboot
