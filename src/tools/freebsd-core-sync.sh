#!/bin/sh
# Refresh the core objects and FreeBSD-side glue objects inside the build VM
# so the combined OS_5P link sees the current kernel.
#
# Host side, from src/:
#   make -j4 FREEBSD_PERSONALITY=1 out/kernel.elf
#   tar -cf build/core-obj.tar -C build kernel initramfs.o
#   python3 -m http.server 8004 --bind 0.0.0.0
# Guest side:
#   fetch -q -o /tmp/core-sync.sh \
#       http://10.0.2.2:8004/tools/freebsd-core-sync.sh && sh /tmp/core-sync.sh
#
# Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.
set -u

BASE="http://10.0.2.2:8004"
GLUE="$BASE/kernel/personality/freebsd"

fetch -q -o /tmp/core-obj.tar "$BASE/build/core-obj.tar" || {
    echo "CORE_SYNC_FAIL=tar"
    exit 1
}
tar -xf /tmp/core-obj.tar -C /tmp/kobjs/build || {
    echo "CORE_SYNC_FAIL=extract"
    exit 1
}

for name in vfs.c vfs.h port.c core_abi.h; do
    fetch -q -o "/tmp/$name" "$GLUE/$name" || {
        echo "CORE_SYNC_FAIL=$name"
        exit 1
    }
done
cd /tmp || exit 1
cc -D_KERNEL -I/usr/src/sys -I/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU \
    -I/tmp -c vfs.c -o vfs.o || exit 1
cc -D_KERNEL -I/usr/src/sys -I/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU \
    -I/tmp -c port.c -o port.o || exit 1

# Rebuild the core half of the object list from the synced tree: the old list
# was written before klog/pty/panic existed, and a missing object shows up as
# an undefined symbol at link time (klog_clear was the first).
{
    grep -v '^/tmp/kobjs/' /tmp/fs_objs.txt | grep -v '^/tmp/port\.o$' |
        grep -v '^/tmp/vfs\.o$'
    find /tmp/kobjs/build -name '*.o' | sort
    echo /tmp/port.o
    echo /tmp/vfs.o
} > /tmp/fs_objs.new
mv /tmp/fs_objs.new /tmp/fs_objs.txt

echo "CORE_SYNC_OK objects=$(wc -l < /tmp/fs_objs.txt | tr -d ' ')"
