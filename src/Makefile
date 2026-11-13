# Zhaoxu OS build system (run inside WSL2)

CC      ?= gcc
CXX     ?= g++
ADA     ?= gcc-14
LD      := ld.lld
OBJCOPY ?= objcopy
FASM    ?= tools/fasm
BOOT_CC ?= clang
BOOT_LD ?= lld-link

BUILD   := build
OUT     := out
ESP     := esp/EFI/BOOT

CFLAGS  := -m64 -std=gnu11 -O2 -g -ffreestanding -fno-pie -fno-pic \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
           -mno-sse -mno-sse2 -mno-mmx -mno-avx \
           -Wall -Wextra -Wno-unused-parameter \
           -Ikernel/include -Ikernel -Ikernel/font
CFLAGS += -MMD -MP
CXXFLAGS := $(filter-out -std=gnu11,$(CFLAGS)) -std=gnu++20 \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics \
            -fno-use-cxa-atexit
PROC_SELFTEST ?= 0
DEBUG_SERIAL ?= 0
SCHED_STATS ?= 0
CFLAGS += -DPROC_SELFTEST=$(PROC_SELFTEST) -DDEBUG_SERIAL=$(DEBUG_SERIAL) -DSCHED_STATS=$(SCHED_STATS)
ADAFLAGS := -c -gnatp -gnat2022 -O2 -g -fno-pie \
            -fno-stack-protector -mno-red-zone -mcmodel=kernel \
            -mgeneral-regs-only -mno-sse -mno-sse2 -mno-mmx -mno-avx \
            -Idrivers
LDFLAGS := -m elf_x86_64 -T kernel/linker.ld -nostdlib -static \
           --build-id=none -z max-page-size=0x1000
# ZhaoxuBootloader UEFI: C loader built as a PE32+ EFI application.
BOOT_CFLAGS = -target x86_64-pc-win32 -m64 -O2 -ffreestanding -fno-builtin \
              -fno-stack-protector -mno-stack-arg-probe -mno-red-zone \
              -fno-asynchronous-unwind-tables -Wall -Wextra \
              -Wno-unused-parameter -DHEADLESS=$(HEADLESS)
BOOT_LDFLAGS := /subsystem:efi_application /entry:efi_start /nodefaultlib \
                /debug:none

C_SRCS := kernel/main.c \
          kernel/arch/x86_64/io.c \
          kernel/arch/x86_64/gdt.c \
          kernel/arch/x86_64/pic.c \
          kernel/arch/x86_64/cpu.c \
          kernel/arch/x86_64/percpu.c \
          kernel/arch/x86_64/apic.c \
          kernel/arch/x86_64/smp.c \
          kernel/int/idt.c \
          kernel/drivers/timer.c \
          kernel/fs/initramfs.c \
          kernel/fs/fat32.c \
          kernel/fs/fat32_write.c \
          kernel/fs/fat32_format.c \
          kernel/fs/vfs.c \
          kernel/fs/block.c \
          kernel/fs/install.c \
          kernel/proc/process.c \
          kernel/proc/zxe_loader.c \
          kernel/proc/syscall.c \
          kernel/proc/dev.c \
          kernel/proc/fsrpc.c \
          kernel/proc/thread.c \
          kernel/ipc/port.c \
          kernel/ipc/shm.c \
          kernel/lib/string.c \
          kernel/lib/printf.c \
          kernel/lib/console.c \
          kernel/mem/pmm.c \
          kernel/mem/vmm.c \
          kernel/mem/heap.c
ASM_SRCS := kernel/arch/x86_64/entry.S \
            kernel/arch/x86_64/gdt_flush.S \
            kernel/arch/x86_64/isr.S \
            kernel/arch/x86_64/user.S \
            kernel/arch/x86_64/switch.S \
            kernel/arch/x86_64/ap_trampoline.S

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
        $(patsubst %.S,$(BUILD)/%.o,$(ASM_SRCS)) \
        $(BUILD)/initramfs.o \
        $(BUILD)/bios_stage1.o \
        $(BUILD)/bios_stage2.o
DEPS := $(OBJS:.o=.d)

HEADLESS ?= 0
.DEFAULT_GOAL := all
HEADLESS_HEADER := $(BUILD)/headless.h

$(HEADLESS_HEADER): FORCE
	@mkdir -p $(BUILD)
	@printf '#define BUILD_HEADLESS %s\n' '$(HEADLESS)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

FORCE:

.PHONY: all clean run iso run-iso run-install run-bios run-install-bios version fonts FORCE

# The kernel is built for the ISO and the installer; rule 25 keeps it out of
# BOOTX64.EFI and the ESP only carries the loader.
all: $(OUT)/BOOTX64.EFI $(ESP)/BOOTX64.EFI $(OUT)/kernel.elf

# MiSans/MiSans Global are not redistributable, so they are fetched here.
FONT_FILES := initramfs/data/MiSans-Regular.ttf \
              initramfs/data/MiSansLatinVF.ttf \
              initramfs/data/Inter-Regular.otf

fonts:
	$(RUBY) tools/fetch-fonts.rb

$(FONT_FILES):
	$(RUBY) tools/fetch-fonts.rb

# Sync the version from version.php (repo root) into all surfaces.
version:
	$(RUBY) tools/version.rb

# ---- Ring 3 programs -------------------------------------------------
USER_CFLAGS := -m64 -std=gnu11 -O2 -ffreestanding -fpie \
               -fno-stack-protector -fno-asynchronous-unwind-tables \
               -mno-red-zone -mno-sse -mno-sse2 \
               -mno-mmx -mno-avx -Wall -Wextra -Wno-unused-parameter
USER_LDFLAGS := -nostdlib -shared -Wl,-Bsymbolic,--build-id=none \
                -Wl,-T,user/user.ld -Wl,-z,max-page-size=0x1000
RUBY ?= /mnt/c/Ruby40-x64/bin/ruby.exe

# A recipe that dies half way must not leave its target behind: some WSL
# interop builds report exit 0 for a Windows binary that aborted, so the ISO
# rule detects failure through $@.patched instead, and a failed patch has to
# invalidate the half-written ISO rather than look up to date next time.
.DELETE_ON_ERROR:

$(BUILD)/%.uelf: user/%.c user/userlib.c user/userlib.h user/user.ld
	@mkdir -p $(BUILD)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/$*.c user/userlib.c

$(OUT)/%.zxe: $(BUILD)/%.uelf tools/zxe-pack.rb
	@mkdir -p $(OUT)
	$(RUBY) tools/zxe-pack.rb $< $@

# ---- ADE ring 3 display server --------------------------------------
ADE_BUILD := $(BUILD)/ade
ADE_INCLUDES := -Iuser -Iuser/lib/include -Ikernel/include -Ikernel \
                -Ikernel/font
ADE_CFLAGS := $(USER_CFLAGS) $(ADE_INCLUDES) -DZOS_USER -MMD -MP
ADE_CXXFLAGS := $(filter-out -std=gnu11,$(ADE_CFLAGS)) -std=gnu++20 \
                -fno-exceptions -fno-rtti -fno-threadsafe-statics \
                -fno-use-cxa-atexit
ADE_FONT_CFLAGS := $(filter-out -mno-sse -mno-sse2 -mno-mmx -mno-avx,\
                  $(ADE_CFLAGS)) -msse2 -mno-avx -fno-math-errno
ADE_C_SRCS := user/lib/shim.c user/lib/string_extra.c user/userlib.c \
              kernel/gfx/render.c kernel/gfx/svg.c kernel/font/font.c
ADE_FONT_SRCS := kernel/font/ttf.c kernel/font/stbtt.c
ADE_CXX_SRCS := apps/ADE/main.cpp apps/ADE/wallpaper.cpp \
                apps/ADE/menubar.cpp apps/ADE/dock.cpp apps/ADE/about.cpp \
                apps/ADE/icons.cpp apps/ADE/display/server.cpp \
                apps/ADE/winmgr/dwm.cpp
ADE_OBJS := $(patsubst %.c,$(ADE_BUILD)/%.o,$(ADE_C_SRCS)) \
            $(patsubst %.c,$(ADE_BUILD)/%.o,$(ADE_FONT_SRCS)) \
            $(patsubst %.cpp,$(ADE_BUILD)/%.o,$(ADE_CXX_SRCS))
ADE_DEPS := $(ADE_OBJS:.o=.d)

$(ADE_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ADE_CFLAGS) -c $< -o $@

$(ADE_BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ADE_CXXFLAGS) -c $< -o $@

$(ADE_BUILD)/kernel/font/ttf.o: kernel/font/ttf.c
	@mkdir -p $(dir $@)
	$(CC) $(ADE_FONT_CFLAGS) -c $< -o $@

$(ADE_BUILD)/kernel/font/stbtt.o: kernel/font/stbtt.c
	@mkdir -p $(dir $@)
	$(CC) $(ADE_FONT_CFLAGS) -c $< -o $@

$(BUILD)/ade.uelf: $(ADE_OBJS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_LDFLAGS) -o $@ $(ADE_OBJS)

# ---- ADE client (welcome window) ------------------------------------
WELCOME_SRCS := user/welcome_client.c user/lib/ade_client.c user/userlib.c
WELCOME_HDRS := user/userlib.h user/lib/include/ade_protocol.h \
                user/lib/include/ade_client.h

$(BUILD)/welcome_client.uelf: $(WELCOME_SRCS) $(WELCOME_HDRS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Iuser -Iuser/lib/include -Ikernel/include \
	  $(USER_LDFLAGS) -o $@ $(WELCOME_SRCS)

CALC_SRCS := user/calc_client.c user/lib/ade_client.c user/userlib.c

$(BUILD)/calc_client.uelf: $(CALC_SRCS) $(WELCOME_HDRS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Iuser -Iuser/lib/include -Ikernel/include \
	  $(USER_LDFLAGS) -o $@ $(CALC_SRCS)

INSTALL_SRCS := user/install_client.c user/lib/ade_client.c user/userlib.c

$(BUILD)/install_client.uelf: $(INSTALL_SRCS) $(WELCOME_HDRS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Iuser -Iuser/lib/include -Ikernel/include \
	  $(USER_LDFLAGS) -o $@ $(INSTALL_SRCS)

# ---- ZAF graphics (Z0) ----------------------------------------------
ZAF_GFX_HDRS := user/lib/include/zaf_gfx.h user/userlib.h
ZAF_GFX_SRCS := user/zaf_gfx_demo.c user/lib/zaf_gfx_soft.c user/userlib.c

$(BUILD)/zaf_gfx_demo.uelf: $(ZAF_GFX_SRCS) $(ZAF_GFX_HDRS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Iuser -Iuser/lib/include -Ikernel/include \
	  $(USER_LDFLAGS) -o $@ $(ZAF_GFX_SRCS)

# ---- csh: the ring 3 shell ------------------------------------------
CSH_SRCS := apps/csh/main.c apps/csh/io.c apps/csh/commands.c \
            apps/csh/builtins.c apps/csh/files.c apps/csh/run.c \
            apps/csh/write.c apps/csh/install.c user/userlib.c
CSH_HDRS := apps/csh/csh.h user/userlib.h

$(BUILD)/csh.uelf: $(CSH_SRCS) $(CSH_HDRS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Iapps/csh -Iuser -Ikernel/include \
	  $(USER_LDFLAGS) -o $@ $(CSH_SRCS)

# ---- ext4fs: the Ring3 ext4 service --------------------------------
# Owns the OS-independent ext4 core (fs/ext4) and talks to the kernel over
# the fs_rpc page; the kernel keeps its own copy until the VFS is forwarded.
EXT4FS_SRCS := apps/ext4fs/main.c apps/ext4fs/service.c \
               apps/ext4fs/blk.c apps/ext4fs/arena.c \
               fs/ext4/ext4_env.c fs/ext4/ext4_mount.c \
               fs/ext4/ext4_extent.c fs/ext4/ext4_dir.c \
               fs/ext4/ext4_read.c fs/ext4/ext4_alloc.c \
               fs/ext4/ext4_write.c fs/ext4/ext4_format.c \
               user/userlib.c kernel/lib/string.c kernel/lib/printf.c
EXT4FS_HDRS := $(wildcard fs/ext4/*.h) apps/ext4fs/ext4fs.h \
               kernel/include/fs_rpc.h user/userlib.h

$(BUILD)/ext4fs.uelf: $(EXT4FS_SRCS) $(EXT4FS_HDRS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Ifs/ext4 -Iapps/ext4fs -Iuser -Ikernel/include \
	  $(USER_LDFLAGS) -o $@ $(EXT4FS_SRCS)

# ---- Ring3 driver service -------------------------------------------
DRV_BUILD := $(BUILD)/drv
DRV_ADAFLAGS := -c -gnatp -gnat2022 -O2 -fpie -fno-stack-protector \
                -mno-red-zone -mgeneral-regs-only \
                -mno-sse -mno-sse2 -mno-mmx -mno-avx -Idrivers
DRV_ADA_SRCS := drivers/ahci.adb drivers/ps2.adb drivers/mouse.adb \
                drivers/serial.adb
DRV_ADA_OBJS := $(patsubst drivers/%.adb,$(DRV_BUILD)/ada/%.o,$(DRV_ADA_SRCS))
DRV_C_SRCS := apps/drivers/main.c apps/drivers/glue.c apps/drivers/keyboard.c \
              apps/drivers/rtc.c kernel/drivers/ahci_log.c \
              kernel/lib/printf.c user/userlib.c
DRV_OBJS := $(patsubst %.c,$(DRV_BUILD)/%.o,$(DRV_C_SRCS)) $(DRV_ADA_OBJS)
DRV_DEPS := $(DRV_OBJS:.o=.d)

$(DRV_BUILD)/ada/%.o: drivers/%.adb
	@mkdir -p $(dir $@)
	$(ADA) $(DRV_ADAFLAGS) -o $@ $<

$(DRV_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Iapps/drivers -Iuser -Ikernel/include -MMD -MP \
	  -c $< -o $@

$(BUILD)/drivers.uelf: $(DRV_OBJS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_LDFLAGS) -o $@ $(DRV_OBJS)

-include $(DRV_DEPS)

# ---- initramfs -------------------------------------------------------
RAMFS := $(BUILD)/initramfs
RAMFS_FILES := $(RAMFS)/bin/init $(RAMFS)/bin/ade $(RAMFS)/bin/csh \
               $(RAMFS)/bin/drivers $(RAMFS)/bin/ext4fs \
               $(RAMFS)/bin/apps/welcome.zxe $(RAMFS)/bin/apps/calc.zxe \
               $(RAMFS)/bin/apps/install.zxe \
               $(RAMFS)/boot/BOOTX64.EFI \
               $(RAMFS)/etc/motd $(RAMFS)/etc/version \
               $(RAMFS)/mnt \
               $(RAMFS)/data/HELLO.TXT $(RAMFS)/data/HELLO.ZXE \
               $(RAMFS)/data/HELLO.ELF \
               $(RAMFS)/data/DOCS/INFO.TXT \
               $(RAMFS)/data/PING.ZXE $(RAMFS)/data/PONG.ZXE

$(RAMFS)/bin/init: $(OUT)/init.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/bin/ade: $(OUT)/ade.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/bin/csh: $(OUT)/csh.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/bin/drivers: $(OUT)/drivers.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/bin/ext4fs: $(OUT)/ext4fs.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/boot/BOOTX64.EFI: $(OUT)/BOOTX64.EFI
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/bin/apps/welcome.zxe: $(OUT)/welcome_client.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/bin/apps/calc.zxe: $(OUT)/calc_client.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/bin/apps/install.zxe: $(OUT)/install_client.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/etc/%: initramfs/etc/%
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/mnt:
	@mkdir -p $@

# Embedded mirror of the FAT32 data volume: /mnt falls back to /data when
# no ZHAOXUDATA disk is present, so disk-less (ISO-only) boots still open
# the sample files. The CJK font is NOT mirrored here on purpose: OVMF
# refuses to boot a BOOTX64.EFI of that size; disk-less boots degrade to
# '?' glyphs until the installer milestone ships data inside the ISO.
$(RAMFS)/data/HELLO.TXT: initramfs/data/hello.txt
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/data/HELLO.ZXE: $(OUT)/hello.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/data/HELLO.ELF: $(BUILD)/hello.uelf
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/data/PING.ZXE: $(OUT)/ping.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/data/PONG.ZXE: $(OUT)/pong.zxe
	@mkdir -p $(dir $@)
	cp $< $@

$(RAMFS)/data/DOCS/INFO.TXT: initramfs/data/info.txt
	@mkdir -p $(dir $@)
	cp $< $@

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

$(DATA_IMG): $(OUT)/hello.zxe $(BUILD)/hello.uelf initramfs/data/hello.txt initramfs/data/info.txt initramfs/data/wqy.ttc $(FONT_FILES)
	@mkdir -p $(OUT)
	dd if=/dev/zero of=$@ bs=1M count=64 status=none
	mkfs.vfat -F 32 -n ZHAOXUDATA $@ >/dev/null
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::/DOCS
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/hello.txt ::/HELLO.TXT
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(OUT)/hello.zxe ::/HELLO.ZXE
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(BUILD)/hello.uelf ::/HELLO.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/MiSans-Regular.ttf ::/
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/MiSansLatinVF.ttf ::/
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/Inter-Regular.otf ::/
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/info.txt ::/DOCS/INFO.TXT

$(BUILD)/efi_main.o: boot/efi_main.c boot/uefi.h $(HEADLESS_HEADER)
	@mkdir -p $(BUILD)
	$(BOOT_CC) $(BOOT_CFLAGS) -c $< -o $@

$(OUT)/BOOTX64.EFI: $(BUILD)/efi_main.o
	@mkdir -p $(OUT)
	$(BOOT_LD) $(BOOT_LDFLAGS) $(BUILD)/efi_main.o /out:$@

# The installer warns when the build is headless: the loader and stage 2 it
# copies to the target would then boot straight to csh.  The flag only lives
# in -DHEADLESS, so this object must follow $(HEADLESS_HEADER) too.
$(BUILD)/kernel/fs/install.o: kernel/fs/install.c $(HEADLESS_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DHEADLESS=$(HEADLESS) -c $< -o $@

# ---- ZhaoxuBootloader legacy BIOS chain ------------------------------
# bios.img is a no-emulation El Torito image holding only stage1 + stage2;
# the kernel is an ordinary ISO9660 file (install/KERNEL.BIN) whose extent is
# patched into stage2 by tools/iso-bios.rb after xorriso laid the ISO out.
BIOS_S1 := $(OUT)/bios_stage1.bin
BIOS_S2 := $(OUT)/bios_stage2.bin
BIOS_IMG := $(OUT)/bios.img
BIOS1_INC := $(BUILD)/bios_stage1.inc

$(BIOS_S2): boot/bios_stage2.asm $(HEADLESS_HEADER)
	@mkdir -p $(OUT)
	$(FASM) -dHEADLESS=$(HEADLESS) $< $@

$(BIOS1_INC): $(BIOS_S2)
	@mkdir -p $(BUILD)
	@size=`stat -c %s $<`; \
	 printf 'STAGE2_SECTORS = %u\n' $$(( (size + 511) / 512 )) > $@

$(BIOS_S1): boot/bios_stage1.asm $(BIOS1_INC)
	@mkdir -p $(OUT)
	$(FASM) $< $@

$(BIOS_IMG): $(BIOS_S1) $(BIOS_S2) tools/bios-img.rb
	$(RUBY) tools/bios-img.rb $(BIOS_S1) $(BIOS_S2) $@

$(BUILD)/bios_stage1.o: $(BIOS_S1)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.biosblob,alloc,load,readonly,data,contents \
	  --redefine-sym _binary_out_bios_stage1_bin_start=bios_stage1_start \
	  --redefine-sym _binary_out_bios_stage1_bin_end=bios_stage1_end \
	  --redefine-sym _binary_out_bios_stage1_bin_size=bios_stage1_size \
	  $< $@

$(BUILD)/bios_stage2.o: $(BIOS_S2)
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	  --rename-section .data=.biosblob,alloc,load,readonly,data,contents \
	  --redefine-sym _binary_out_bios_stage2_bin_start=bios_stage2_start \
	  --redefine-sym _binary_out_bios_stage2_bin_end=bios_stage2_end \
	  --redefine-sym _binary_out_bios_stage2_bin_size=bios_stage2_size \
	  $< $@

# stb_truetype needs scalar float math (SSE2); the rest of the kernel is
# built -mgeneral-regs-only. cpu_init() clears CR0.EM/TS and sets
# CR4.OSFXSR/OSXMMEXCPT before the font engine runs.
FONT_CFLAGS := $(filter-out -mgeneral-regs-only -mno-sse -mno-sse2 \
                 -mno-mmx -mno-avx,$(CFLAGS)) \
               -msse2 -mno-avx -fno-math-errno

$(BUILD)/kernel/font/ttf.o: kernel/font/ttf.c
	@mkdir -p $(dir $@)
	$(CC) $(FONT_CFLAGS) -c $< -o $@

$(BUILD)/kernel/font/stbtt.o: kernel/font/stbtt.c
	@mkdir -p $(dir $@)
	$(CC) $(FONT_CFLAGS) -c $< -o $@

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

$(ESP)/BOOTX64.EFI: $(OUT)/BOOTX64.EFI
	@mkdir -p $(ESP)
	cp $< $@

clean:
	rm -rf $(BUILD) $(OUT)/kernel.elf $(OUT)/kernel.bin $(OUT)/BOOTX64.EFI \
	       $(OUT)/init.zxe $(OUT)/hello.zxe $(OUT)/initramfs.tar \
	       $(OUT)/data.img $(OUT)/efi.img $(OUT)/zhaoxu_*_installer.iso \
	       $(OUT)/*.patched \
	       $(OUT)/bios_stage1.bin $(OUT)/bios_stage2.bin $(OUT)/bios.img \
	       $(OUT)/target.img $(ESP)/BOOTX64.EFI
	rm -f $(ESP)/KERNEL.ELF

OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
QEMU_MACHINE ?= q35
QEMU_MEM ?= 512M
QEMU_SMP ?= 4
QEMU_DISPLAY ?= -display none
QEMU_MONITOR ?= none

# Installer plumbing: out/efi.img is the El Torito ESP holding only the UEFI
# loader; out/target.img is the blank disk the graphical installer formats.
TARGET_IMG := $(OUT)/target.img
EFI_IMG := $(OUT)/efi.img

$(TARGET_IMG):
	@mkdir -p $(OUT)
	dd if=/dev/zero of=$@ bs=1M count=256 status=none

# ISO-only testing (rule 27): the CD carries loader, kernel and assets, so no
# data disk is attached.  run-install adds one blank target for the installer.
run: run-iso

# ---- UEFI ISO --------------------------------------------------------
# Rule 11: ISO files are named zhaoxu_{version}_installer.iso.
ZHAOXU_VERSION := $(shell $(RUBY) -e 'v=File.read("kernel/include/version.h")[/ZHAOXU_VERSION\s+"([^"]+)"/,1]; print v')
ISO := $(OUT)/zhaoxu_$(ZHAOXU_VERSION)_installer.iso
ISO_ROOT := $(BUILD)/iso
ISO_ESP_MB ?= 64

# The El Torito ESP only holds the loader; the kernel is read from the CD
# root by the loader's ISO9660 reader (not from the ESP).
$(EFI_IMG): $(OUT)/BOOTX64.EFI
	@mkdir -p $(OUT)
	dd if=/dev/zero of=$@ bs=1M count=$(ISO_ESP_MB) status=none
	mkfs.vfat -F 32 -n ZHAOXUEFI $@ >/dev/null
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::/EFI
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::/EFI/BOOT
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(OUT)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI

$(ISO): $(OUT)/BOOTX64.EFI $(OUT)/kernel.elf $(EFI_IMG) $(BIOS_IMG) \
        initramfs/data/wqy.ttc tools/kernel-flat.rb tools/iso-bios.rb
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/install
	cp $(EFI_IMG) $(ISO_ROOT)/efi.img
	cp $(BIOS_IMG) $(ISO_ROOT)/bios.img
	$(RUBY) tools/kernel-flat.rb $(OUT)/kernel.elf $(ISO_ROOT)/install/KERNEL.BIN
	cp $(OUT)/kernel.elf $(ISO_ROOT)/install/KERNEL.ELF
	cp initramfs/data/wqy.ttc $(ISO_ROOT)/install/WQY.TTC
	xorriso -as mkisofs -o $@ -V ZHAOXU_OS -J -R \
	  -b bios.img -no-emul-boot -eltorito-alt-boot \
	  -e efi.img -no-emul-boot -isohybrid-gpt-basdat $(ISO_ROOT)
	$(RUBY) tools/iso-bios.rb $@
	test -f $@.patched
	rm -f $@.patched

iso: $(ISO)

# The CD lives on ide.0; run-install adds one blank target on ide.1.
QEMU_CD := -drive if=none,id=zcd,format=raw,media=cdrom,readonly=on,file=$(ISO) \
           -device ide-cd,drive=zcd,bus=ide.0
QEMU_TARGET_DISK := \
	-drive if=none,id=zdst,format=raw,file=$(TARGET_IMG) \
	-device ide-hd,drive=zdst,bus=ide.1

run-iso: $(ISO)
	cp $(OVMF_VARS) $(OUT)/OVMF_VARS.fd
	qemu-system-x86_64 -M $(QEMU_MACHINE) -m $(QEMU_MEM) -smp $(QEMU_SMP) \
	  -drive if=pflash,format=raw,unit=0,readonly=on,file=$(OVMF_CODE) \
	  -drive if=pflash,format=raw,unit=1,file=$(OUT)/OVMF_VARS.fd \
	  $(QEMU_CD) -boot order=d \
	  -net none -serial stdio -monitor $(QEMU_MONITOR) $(QEMU_DISPLAY) -no-reboot

# Installer test: the same CD plus one blank target disk.
run-install: $(ISO) $(TARGET_IMG)
	cp $(OVMF_VARS) $(OUT)/OVMF_VARS.fd
	qemu-system-x86_64 -M $(QEMU_MACHINE) -m $(QEMU_MEM) -smp $(QEMU_SMP) \
	  -drive if=pflash,format=raw,unit=0,readonly=on,file=$(OVMF_CODE) \
	  -drive if=pflash,format=raw,unit=1,file=$(OUT)/OVMF_VARS.fd \
	  $(QEMU_CD) $(QEMU_TARGET_DISK) -boot order=d \
	  -net none -serial stdio -monitor $(QEMU_MONITOR) $(QEMU_DISPLAY) -no-reboot

# Legacy BIOS: SeaBIOS boots the El Torito image (ZhaoxuBootloader stage 1).
run-bios: $(ISO)
	qemu-system-x86_64 -M $(QEMU_MACHINE) -m $(QEMU_MEM) -smp $(QEMU_SMP) \
	  $(QEMU_CD) -boot order=d \
	  -net none -serial stdio -monitor $(QEMU_MONITOR) $(QEMU_DISPLAY) -no-reboot

run-install-bios: $(ISO) $(TARGET_IMG)
	qemu-system-x86_64 -M $(QEMU_MACHINE) -m $(QEMU_MEM) -smp $(QEMU_SMP) \
	  $(QEMU_CD) $(QEMU_TARGET_DISK) -boot order=d \
	  -net none -serial stdio -monitor $(QEMU_MONITOR) $(QEMU_DISPLAY) -no-reboot

-include $(DEPS)
-include $(ADE_DEPS)
