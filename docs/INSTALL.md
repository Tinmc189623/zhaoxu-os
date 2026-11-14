# Zhaoxu OS Installation Guide

Applies to Zhaoxu OS 0.6.0 "Yuanshuo", kernel Yuanshuo 0.6.0. The installation medium is one ISO that boots under UEFI and Legacy BIOS. After installation the target disk holds a FAT32 ESP and an ext4 root volume; the GPT layout also carries a BIOS boot partition, and the kernel lives in ext4.

This document covers three paths: physical hardware, virtual machines, and graphics-free environments. Before you start, read "Check before installing"; its limits decide whether a given machine can install at all.

---

## Check before installing

### Hardware requirements

| Item | Requirement |
| --- | --- |
| CPU | x86_64 with long mode and NX |
| Memory | 512 MiB minimum, 1 GiB or more recommended |
| Target disk | Disk on a SATA / AHCI controller, whole-disk installation |
| Input | PS/2 keyboard and mouse |
| Display | UEFI GOP, or a 32bpp VBE linear framebuffer under Legacy BIOS |
| Firmware | UEFI or Legacy BIOS/CSM, Secure Boot must be off |
| Installation medium | USB drive or DVD, at least 1 GiB |

A few points need stating up front:

- Current storage drivers cover only AHCI/IDE/SATA/SCSI. NVMe drives, USB hard disks, and USB flash drives are all invisible once the system boots. A USB drive can serve as installation media because the firmware reads the ISO, but the target disk must sit on a SATA controller.
- Input supports PS/2 keyboard and mouse only. A machine with only USB input may accept a few keystrokes through firmware legacy emulation, with no guarantee after boot. Confirm the board has PS/2 ports, or that USB legacy support is enabled in the firmware.
- Secure Boot must be off. ZhaoxuBootMgr is unsigned, so Secure Boot refuses to boot it.
- Set SATA mode to AHCI. Intel RST, RAID, and Optane modes keep the Ring3 AHCI driver from finding disks. Switching the mode can affect an existing Windows installation, so think it through before changing anything.
- The system has no networking, audio, USB HID, multi-user support, or POSIX compatibility layer yet. Installation steps cannot supply them.

### The target disk is overwritten entirely

The installer does not let you choose a partition. It scans every AHCI disk, picks one that is "blank or already has a Zhaoxu layout", and rewrites from LBA 0: MBR/GPT, stage1/stage2, ESP, and ext4 root are all rebuilt.

This means:

- Existing Windows, Linux, and data partitions on the target disk disappear with it.
- With more than one blank disk, the installer picks the lowest index by default; the GUI has no "disk 0 / disk 1" selector. Unplug unrelated blank disks before installing, or run `disks` first.
- Target detection looks only at the partition table in the first sector, not at filesystem contents. Blank disks, MBR disks whose first partition starts at LBA 2048 with type 0xEF/0x0C/0x0E, and protective MBRs carrying the `ZSLBA` marker all become `target` candidates. Some Linux or Windows disks whose ESP happens to be the first partition starting at LBA 2048 also qualify as reinstall targets. If unsure, unplug the disk or check with `disks` in `8sh`.
- A disk that already holds Zhaoxu OS is recognized as a reinstall target; reinstalling wipes the old system just the same.

### How large the target disk must be

The recognition threshold is 2048 + 70000 + 32768 512-byte sectors, about 52 MiB. Smaller disks never appear in the `target` role.

In practice start at 256 MiB, matching `out/target.img` used by QEMU regression. 1 GiB or more gives room to spare. The ESP is at most 64 MiB, everything else goes to the ext4 root, and fonts plus wallpaper take about 11 MiB.

---

## 1. Get the ISO

### Build from source

`version.cfg` at the repository root is the only version source. The build entry point is Ruby; `tools/build.rb --check` prints the tool matrix for the current host and what is missing.

Linux, macOS, WSL2:

```bash
cd src
ruby tools/build.rb --check
ruby tools/build.rb iso
```

Windows PowerShell 7:

```powershell
cd src
.\build.ps1 -Check
.\build.ps1 iso
```

The product is `src/out/zhaoxu_0.6.0_installer.iso`. The version in the filename comes from `version.cfg`; do not edit it by hand.

Build dependencies vary by host:

- Linux/WSL: `gcc`, `g++`, `gcc-14`/`gnat-14`, `ld.lld`, `objcopy`, `clang`, `lld-link`, `qemu-system-x86_64`, OVMF, `mkfs.vfat`, `mtools`, `tar`, `xorriso`, `squashfs-tools`.
- macOS: Homebrew `llvm`, `lld`, `qemu`, `xorriso`, `mtools`, `dosfstools`, `squashfs`; the Ada driver needs x86_64-elf GNAT, selected with `ADA=`.
- Native Windows: Ruby, MSYS2 `make`, `mingw-w64-x86_64-clang`, `lld`, `llvm`, `dosfstools`, `mtools`, `xorriso`, `squashfs-tools`, `qemu`; use a host build of FASM and point to it with `FASM=`, or put `fasm.exe` in `src/tools/`.

The output of `ruby tools/build.rb --check` is more accurate than this list; when a tool is missing, prefer the install hints it prints.

### Download from the release page

If you use a published ISO, its filename should also be `zhaoxu_0.6.0_installer.iso`. Verify the size and checksum after downloading, then write it to the medium.

---

## 2. Write the ISO to a USB drive or disc

### USB drive

The ISO is a hybrid image with dual El Torito entries plus a GPT hybrid layout; both the UEFI and Legacy BIOS boot records are inside. The writing tool must write it raw and must not rebuild the filesystem.

Rufus is recommended on Windows:

1. Select `zhaoxu_0.6.0_installer.iso`.
2. Set partition type to GPT and target system to UEFI; machines that only use Legacy BIOS select MBR.
3. Click "Start". When Rufus asks "ISO Image mode / DD Image mode", choose DD.
4. Existing data on the USB drive will be erased.

On Linux/macOS use `dd` directly:

```bash
sudo dd if=zhaoxu_0.6.0_installer.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

`of=/dev/sdX` is the whole USB drive, not `/dev/sdX1`. Writing the wrong device destroys another disk; check twice with `lsblk` before running.

Do not copy the ISO file onto a FAT32 USB drive. That leaves an 80-odd MB file that the firmware will not treat as a boot disk.

### Disc

In the burning tool choose "Burn image", not "Create data disc". A DVD has enough capacity, and the El Torito dual entries let both UEFI and Legacy BIOS recognize the disc.

---

## 3. Firmware settings

Enter BIOS/UEFI setup at boot and check each item:

- **Secure Boot**: off. Names vary by board; it may sit under Security, Boot, or Authentication.
- **SATA mode**: AHCI. Do not use RAID, RST, or Optane.
- **Boot mode**: keep UEFI for UEFI; enable CSM/Legacy Boot for Legacy BIOS. Both work, but pair UEFI with GPT and Legacy BIOS with MBR to avoid firmware compatibility trouble.
- **Boot order**: USB drive or optical drive first.
- **USB legacy support**: on. PS/2 input can skip this; machines with only USB input need it until the system starts.
- **Fast Boot**: turn it off for now, so boot devices and error messages stay visible.

In a virtual machine the matching options are "Enable EFI", "Secure Boot", and "SATA/AHCI controller"; the next section covers them separately.

---

## 4. Boot from the ISO

The serial log for the UEFI path looks roughly like this:

```text
ZhaoxuBootMgr (UEFI x86_64)
BootMgr: zhaoxu.cfg loaded
BootMgr: starting OSLDRX64.efi
Kernel: /install/KERNEL.ELF from the ISO.
Font: /install/WQY.TTC from the ISO.
```

Legacy BIOS path:

```text
ZhaoxuBootloader stage1 (legacy BIOS)
ZhaoxuBootloader stage2 (legacy BIOS)
A20 gate enabled.
VESA framebuffer selected.
E820 memory map collected.
ACPI RSDP located.
Reading kernel image.
Kernel image read.
Kernel image loaded, entering long mode.
```

After the kernel starts it launches the Aqua Desktop Environment desktop; the menu bar, Dock, welcome window, and calculator window all appear. Chinese text displays correctly, which means the font was read from `/install/WQY.TTC` on the ISO.

Whether the installer window opens by itself depends on two conditions:

1. No data volume or root volume is currently mounted.
2. `disks` lists at least one disk in the `target` role.

When booting from the ISO with a blank target disk, the window appears automatically. With a `ZHAOXUDATA` data disk already mounted it does not; click "File" in the menu bar to force the installer open. If the window does not appear, don't blame the installer yet; check target disk detection in section 9.

---

## 5. Run the installation

### Check the target disk

The installer window title is "Install Zhaoxu OS", and the body reads "Single ISO installation, no extra media" and "Installation writes the single blank target disk". The window has no disk list and no control for "select disk N".

To confirm disk roles, use `8sh` in a graphics-free environment:

```text
8sh$ disks
Idx Port Size Label Role Model
 0 0 256 MiB - target QEMU HARDDISK
```

Role meanings:

- `target`: a blank disk, a legacy Zhaoxu layout whose first partition starts at LBA 2048 with type 0xEF/0x0C/0x0E, or a Zhaoxu GPT with the `ZSLBA` marker. Eligible for installation.
- `media`: an ESP labeled `ZHAOXUEFI`. The installer never selects it.
- `data`: a data disk labeled `ZHAOXUDATA`. The installer never selects it.
- `other`: another partition table or filesystem. The installer never selects it.

### Choose MBR or GPT

The window highlights GPT by default. The two buttons are "Install using MBR" and "Install using GPT".

Clicking starts writing immediately, with no second confirmation. Before your finger leaves the mouse, make sure the target disk holds nothing you want to keep.

| Layout | Use case | What is written |
| --- | --- | --- |
| MBR | Legacy BIOS boot | stage1 at LBA0, stage2 at LBA1, FAT32 ESP from LBA2048 (64 MiB max), ext4 root after it |
| GPT | UEFI boot | protective MBR, GPT header and partition table at LBA1/2, stage2 from LBA34 (occupying the GPT BIOS boot area), FAT32 ESP from LBA2048 (64 MiB max), ext4 root after it, backup GPT at the end of the disk |

Neither layout places the kernel in the ESP or in boot sectors. The kernel exists only on the ext4 root:

- `/KERNEL.BIN`: read directly by legacy BIOS stage2 through a contiguous block list.
- `/KERNEL.ELF`: parsed by the UEFI loader.
- `/WQY.TTC`: Chinese vector font.
- `/WALLPAPER.RAW`: raw frame of the default wallpaper.

The ESP label is `ZHAOXUEFI`, its type is FAT32, and it holds only the UEFI boot files under `EFI/BOOT/`. The ext4 root label is `ZHAOXUROOT`.

### Let it finish

The interface shows no progress bar during the write; only the status text changes from "Click a button to start the installation" to installing. Actual time depends on disk speed: most SATA SSDs take a few seconds to a dozen, while spinning disks and virtual disks take longer.

On success the window shows:

```text
安装完成，请重启并从目标盘启动
```

On failure it shows:

```text
安装失败：请确认存在空白目标盘
```

The matching success log on serial is:

```text
[install] esp 131072 sectors, ext4 root ... @ 133120 (target ..., GPT)
[ext4] formatted ... label ZHAOXUROOT
[ext4] write /KERNEL.ELF ...
[ext4] write /KERNEL.BIN ...
[ext4] write /WQY.TTC ...
[install] complete
```

Wait for `[install] complete` before cutting power. If it does not appear, troubleshoot per section 9 instead of clicking the button repeatedly.

### Before rebooting

1. Shut down.
2. Remove the installation USB drive, or eject the disc.
3. Enter firmware setup and put the target disk first in the boot order.
4. Keep UEFI mode on UEFI machines; keep Legacy/CSM on Legacy BIOS machines.

---

## 6. First boot after installation

After a successful UEFI boot, serial should show:

```text
ZhaoxuBootMgr (UEFI x86_64)
BootMgr: starting OSLDRX64.efi
Kernel: /KERNEL.ELF from ext4.
Font: /WQY.TTC from ext4.
...
[fs  ] ext4 service ready
[vfs ] /mnt mounted (ext4 "ZHAOXUROOT")
Boot complete. Starting Aqua Desktop Environment.
```

After a successful Legacy BIOS boot:

```text
ZhaoxuBootloader stage1 (legacy BIOS)
ZhaoxuBootloader stage2 (legacy BIOS)
Reading kernel image.
Kernel image read.
Kernel image loaded, entering long mode.
...
[ext4] mounted "ZHAOXUROOT" on /mnt: ...
[vfs ] /mnt mounted (ext4 "ZHAOXUROOT")
Boot complete. Starting Aqua Desktop Environment.
```

`/` is still initramfs, and the ext4 root is mounted at `/mnt`. System programs start from initramfs, while fonts, wallpaper, and installation assets are read from `/mnt`. The installed desktop shows the wallpaper and Chinese automatically, with no further dependency on the ISO.

### Known issue after UEFI installation

The 0.6.0 installer, when writing the ESP, copies only `EFI/BOOT/BOOTX64.EFI` and does not write the second-stage loader `EFI/BOOT/OSLDRX64.efi` alongside it. The result: installation completes and Legacy BIOS boots normally, but a UEFI reboot stops at:

```text
BootMgr: cannot read OSLDRX64.efi
```

The ISO itself contains the file; copy it in.

On Windows, use `diskpart` to assign a temporary drive letter to the ESP:

```text
diskpart
list disk
select disk N
list partition
select partition M
assign letter=S
exit
```

Mount the ISO in Explorer and copy `EFI\BOOT\OSLDRX64.efi` to `S:\EFI\BOOT\`. Then remove the drive letter:

```text
diskpart
select disk N
select partition M
remove letter=S
exit
```

On Linux:

```bash
lsblk -f
sudo mkdir -p /mnt/zhaoxu-esp
sudo mount /dev/sdX1 /mnt/zhaoxu-esp
sudo cp /media/$USER/ZHAOXU_OS/EFI/BOOT/OSLDRX64.efi /mnt/zhaoxu-esp/EFI/BOOT/
sync
sudo umount /mnt/zhaoxu-esp
```

Replace `/dev/sdX1` with the FAT32 partition labeled `ZHAOXUEFI`, usually 64 MiB. Boot again after adding the file; `BootMgr: zhaoxu.cfg missing, using defaults` is normal output, and it falls back to the default path to find `OSLDRX64.efi`.

If you only need a quick run, Legacy BIOS installation is unaffected by this issue.

---

## 7. Install in a virtual machine

### QEMU

The repository ships install regression targets. They automatically attach a 256 MiB blank `out/target.img` as the `ide.1` disk:

```bash
cd src
ruby tools/build.rb iso
ruby tools/run.rb --display gtk run-install
```

Legacy BIOS variant:

```bash
ruby tools/run.rb --display gtk run-install-bios
```

`--display gtk` is required; the default `-display none` shows no window, and the install button cannot be clicked.

In Aqua Desktop Environment click "Install using GPT" or "Install using MBR", wait for "Installation complete. Reboot and boot from the target disk", then close QEMU. Boot with only the target disk attached.

UEFI:

```bash
qemu-system-x86_64 -M q35 -m 512 -smp 4 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
  -drive if=pflash,format=raw,unit=1,file=out/OVMF_VARS.fd \
  -drive if=none,id=zdst,format=raw,file=out/target.img \
  -device ide-hd,drive=zdst,bus=ide.0 \
  -boot order=c -serial stdio -display gtk
```

`OVMF_CODE` points to the OVMF code file on the host; in PowerShell write `$env:OVMF_CODE`, or replace it with the actual path. `out/OVMF_VARS.fd` has already been generated when `run-install` runs.

Legacy BIOS:

```bash
qemu-system-x86_64 -M q35 -m 512 -smp 4 \
  -drive if=none,id=zdst,format=raw,file=out/target.img \
  -device ide-hd,drive=zdst,bus=ide.0 \
  -boot order=c -serial stdio -display gtk
```

### VirtualBox

1. Create an x86_64 virtual machine with at least 512 MiB of memory and 2 to 4 CPUs.
2. Set the storage controller to SATA with type AHCI. Do not use an NVMe controller.
3. Attach the ISO to the optical drive, and create a blank virtual hard disk attached to SATA.
4. Enabling EFI in system settings means a UEFI install; leaving it off means a Legacy BIOS install. Disable Secure Boot under EFI mode.
5. Start the virtual machine, install, power it off, remove the ISO from the optical drive, then start it again.

VirtualBox emulates PS/2 keyboard and mouse by default, so the installer window is clickable. A UEFI install still needs `OSLDRX64.efi` added per section 6.

### VMware Workstation

1. Create a custom virtual machine; choose "Other Linux 6.x 64-bit" or "Other 64-bit" as the guest system.
2. Choose UEFI or BIOS firmware matching the layout selected during installation. Disable Secure Boot under UEFI.
3. Use a SATA disk; do not add an NVMe controller.
4. Attach the ISO to the optical drive and boot the installer.
5. Disconnect the ISO after installation and boot from the virtual hard disk.

VMware's PS/2 keyboard and mouse emulation is sufficient; if the mouse cannot click the window, check whether the virtual machine captured input.

---

## 8. Graphics-free installation

When the machine has no usable framebuffer, the kernel starts Ring3 `8sh` instead of Aqua Desktop Environment. Install through serial or headless QEMU. QEMU `-display none` only hides the window; the GOP framebuffer remains and the kernel still starts Aqua Desktop Environment. To use 8sh, disable the graphics layer at build time:

```bash
ruby tools/build.rb --headless iso
```

Then boot the ISO built with `HEADLESS=1`; `8sh$ ` appears on serial. Note: the installer copies the bootloader and stage2 currently running, so a headless install still boots only into `8sh` after reboot and will not start Aqua Desktop Environment. For a desktop, go back to section 5 and install with the graphical installer.

```text
8sh$ disks
8sh$ install gpt
```

Or:

```text
8sh$ install mbr
8sh$ install 0 gpt
```

`install` without arguments defaults to MBR and picks the first `target` disk automatically. With an index, run `disks` first and check the `Idx` column; indexing starts at 0.

The output should be:

```text
Installing GPT + ext4 root; this may take a while...
Install complete. Boot the target disk directly.
```

On failure:

```text
8sh: no suitable blank target disk
```

Or:

```text
8sh: install failed while writing the target disk
```

In the second case serial contains a specific error beginning with `[install]`, such as `target is not a blank Zhaoxu target disk`, `ext4 service could not format the root volume`, or `KERNEL.BIN is not contiguous inside ext4`. Match the error text against the next section.

---

## 9. Common issues

### No installer window after booting the ISO

Check the `disks` output for `target`. If it is absent, common causes are:

- The target disk is NVMe or USB, invisible to the AHCI driver.
- The target disk already has a Windows/Linux partition table, and the first sector is neither blank nor Zhaoxu-like.
- The target disk is smaller than about 52 MiB.
- The target disk is a `ZHAOXUEFI` or `ZHAOXUDATA` volume, which the installer deliberately excludes.
- The motherboard SATA mode is not AHCI.

If the target disk is present but the window did not open automatically, a data volume or root volume is usually mounted. Click "File" in the menu bar to force the installer open.

### The install button does nothing, or the window disappears

The installer is a separate process, `/bin/apps/install.zxe`. A closed window does not come back on its own; click "File" in the menu bar to reopen it. After a failed install the window shows red text, "Installation failed: confirm a blank target disk exists", and serial prints the specific reason.

### Stuck at `BootMgr: cannot read OSLDRX64.efi`

A known issue after UEFI installation; add `OSLDRX64.efi` to the ESP per section 6. Legacy BIOS never takes this path.

### Stuck after `BootMgr: zhaoxu.cfg missing`

That line is not an error by itself; the next line is what matters. If it is followed by `cannot read OSLDRX64.efi`, it is still the missing-file problem above. If it is followed by `starting OSLDRX64.efi`, the boot manager has handed over control.

### UEFI boot goes straight to firmware setup or a black screen

- With Secure Boot enabled, the firmware rejects the unsigned `BOOTX64.EFI`. Turn Secure Boot off.
- The target disk was installed for Legacy boot but started in UEFI mode, or the reverse. Make sure the layout chosen at install time matches the firmware mode.
- A pure NVMe machine has no usable SATA target disk, so the system cannot install or boot.

### Legacy BIOS reports `stage2: disk read failed`

The boot stage cannot read the kernel. Common causes are an incomplete install, power loss on the target disk, or booting from a different disk. Reinstall once and confirm `[install] complete` appears on serial.

### Keyboard and mouse dead after boot

The system drives PS/2 only. USB keyboard and mouse may work during the firmware stage, but there is no USB HID driver after boot. Switch to PS/2 input, or try enabling USB legacy support in the BIOS. Virtual machines emulate PS/2 by default, so this rarely comes up.

### The desktop starts but the resolution or image is wrong

UEFI uses GOP, Legacy BIOS uses 32bpp VBE. Some discrete GPUs provide no usable VBE linear framebuffer in Legacy mode; switch to integrated graphics, enable CSM, or boot in UEFI mode instead. In a virtual machine set the display adapter to standard VGA/VMware SVGA, leave 3D acceleration off, and do not use a PCI passthrough GPU.

### Installation stuck at `KERNEL.BIN is not contiguous inside ext4`

The ext4 service could not allocate contiguous blocks for the kernel. Reinstalling once usually gets past it; if it keeps failing, change the target disk, or first confirm the disk has no bad sectors and its capacity is not truncated by the controller.

### The target disk has an old system and the installer refuses it

The installer accepts only a blank disk or an existing Zhaoxu layout. To reinstall the whole disk, clear the partition table first. This step destroys all data on the disk; confirm the disk number more than once before running it. A disk whose ESP is the first partition starting at LBA 2048 also looks like a Zhaoxu layout, so confirm the disk is truly expendable first.

Windows:

```text
diskpart
list disk
select disk N
clean
exit
```

Linux:

```bash
lsblk
sudo wipefs -a /dev/sdX
```

After clearing, boot the ISO again; `disks` should list `target`.

---

## 10. Uninstall and reclaim the disk

The system has no uninstaller. To return to Windows or Linux, overwrite the target disk with installation media, or clear the partition table and repartition in a PE/Linux environment. The installer only modifies the disk and does not write UEFI NVRAM on its own; if the firmware remembers a Zhaoxu boot entry, delete it in firmware setup.

---

## 11. Verify the installation

After installation the target disk should have two visible partitions. The GPT layout also has a BIOS boot area holding stage2, with no filesystem:

| Partition | Filesystem | Label | Contents |
| --- | --- | --- | --- |
| BIOS boot (GPT only), from LBA34 | none | none | stage2 boot code |
| ESP, from LBA 2048, 64 MiB max | FAT32 | `ZHAOXUEFI` | `EFI/BOOT/BOOTX64.EFI`, plus `EFI/BOOT/OSLDRX64.efi` added manually when needed |
| ext4 root, after the ESP up to the backup GPT | ext4 | `ZHAOXUROOT` | `/KERNEL.ELF`, `/KERNEL.BIN`, `/WQY.TTC`, `/WALLPAPER.RAW` |

On a Linux host you can inspect it like this:

```bash
lsblk -f
sudo blkid
```

When `8sh` is available, `disks` also shows roles. However, the first sector of an installed disk is the Zhaoxu MBR, so `disks` may still mark the whole disk as `target`. To judge whether installation succeeded, check serial for `Kernel: /KERNEL.ELF from ext4.` and `[vfs ] /mnt mounted (ext4 "ZHAOXUROOT")`.

---

Zhaoxu OS · Nexsteaduser

Copyright © 2026 Nexsteaduser. All Rights Reserved
