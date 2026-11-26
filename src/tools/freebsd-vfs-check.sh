#!/bin/sh
# Compile-check the FreeBSD vnode-backed VFS provider inside the guest.
# Run from the VM serial console after the host serves src/ on port 8004:
#   fetch -q -o /tmp/vfs-check.sh \
#       http://10.0.2.2:8004/tools/freebsd-vfs-check.sh && sh /tmp/vfs-check.sh
# Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.
set -u

BASE="http://10.0.2.2:8004/kernel/personality/freebsd"

for name in vfs.c vfs.h port.c core_abi.h; do
    fetch -q -o "/tmp/$name" "$BASE/$name" || {
        echo "VFS_FETCH_FAIL=$name"
        exit 1
    }
done

cd /tmp || exit 1
status=0
for source in vfs.c port.c; do
    cc -D_KERNEL -I/usr/src/sys -I/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU \
        -I/tmp -c "$source" -o "${source%.c}.o" || status=1
done
echo "VFS_CC=$status"
exit "$status"
