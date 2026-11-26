#!/bin/sh
# Boot the combined OS_5P image (build/os5p.elf) headless under UEFI and check
# the personality handshake on the serial line.
#
# build/os5p.elf comes from the FreeBSD VM:
#   python3 tools/freebsd-vm.py --link
#   nc -l 8010 > build/os5p.elf        # host receiver
#   guest: nc 10.0.2.2 8010 < /tmp/os5p.elf
#
# Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.
set -eu

cd "$(dirname "$0")/.." || exit 1
ELF=build/os5p.elf
ISO=out/os5p-test.iso
LOG=build/os5p-serial.log
SECONDS_TO_RUN=${OS5P_BOOT_SECONDS:-30}

[ -f "$ELF" ] || { echo "missing $ELF"; exit 1; }
[ -d build/iso ] || { echo "missing build/iso; run make iso once"; exit 1; }
[ -f /usr/share/OVMF/OVMF_CODE_4M.fd ] || { echo "OVMF not installed"; exit 1; }

rm -rf build/iso-os5p
cp -a build/iso build/iso-os5p
cp "$ELF" build/iso-os5p/casper/kernel.elf
cp "$ELF" build/iso-os5p/boot/limine/kernel.elf

xorriso -as mkisofs -o "$ISO" -V ZHAOXU_OS -J -R \
    -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
    -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    build/iso-os5p > build/os5p-iso.log 2>&1

cp /usr/share/OVMF/OVMF_VARS_4M.fd build/OVMF_VARS_os5p.fd
rm -f "$LOG"
timeout "$SECONDS_TO_RUN" qemu-system-x86_64 -M q35 -m 512M -smp 1 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS_os5p.fd \
    -drive if=none,id=zcd,format=raw,media=cdrom,readonly=on,file="$ISO" \
    -device ide-cd,drive=zcd,bus=ide.0 -boot order=d \
    -net none -serial "file:$LOG" -monitor none -display none -no-reboot || true

clean=$(sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$LOG" | tr -d '\r')
echo "$clean" | grep -aE '\[bsd|OS_5P|PANIC|panic' | head -12

status=0
echo "$clean" | grep -q '\[bsd \] handing off' || status=1
echo "$clean" | grep -q 'OS_5P: console table bound' || status=1
if echo "$clean" | grep -qi 'panic'; then
    status=1
fi

if [ "$status" -eq 0 ]; then
    echo "OS5P_BOOT=PASS"
else
    echo "OS5P_BOOT=FAIL"
fi
exit "$status"
