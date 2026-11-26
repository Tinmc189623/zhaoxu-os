#!/bin/sh
# Build the ZHAOXU personality kernel inside the FreeBSD VM.
#
# The driver (tools/freebsd-vm.py) fetches this file, uploads the repository
# kernel configuration, and runs it with JOBS set.  Build output stays in a
# guest file instead of the 115200-baud serial console, and only the result
# tail is printed.
#
# Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.
set -u

cd /usr/src || {
    echo "BUILD_RC=2"
    exit 2
}

# OS_5P is a single static image: no kernel modules are loaded, and building
# the module tree only adds unrelated failures (cc_hd, cfiscsi) and time.
make -j"${JOBS:-8}" buildkernel KERNCONF=ZHAOXU NO_MODULES=1 \
    > /tmp/build.log 2>&1
status=$?
tail -30 /tmp/build.log
echo "BUILD_RC=$status"
exit "$status"
