# M2 machine port: from handshake to mountroot

Status 2026-11-26: the combined OS_5P image boots under Limine and runs
FreeBSD-compiled code (`port.c`) inside OS_5P; the handshake markers are
`[bsd ] handing off to the FreeBSD personality`, `OS_5P: FreeBSD personality
executing`, `OS_5P: console table bound`.  The next gate is FreeBSD's
`mi_startup()` and the root mount, after which `vfs.c` registers through the
`vfs_mounted` event and the namespace belongs to the vnode layer.

## What the port owns today

- `kernel/personality/freebsd/boot.c` fills the v4 service table, proves the
  pmap window and calls the port entry.
- `port.c` validates the table, stores it for the VFS provider, and writes
  through the core console.
- `vfs.c` implements `zx_vfs_provider` with `kern_*` helpers and compiles in
  the guest (`tools/freebsd-vfs-check.sh`, `VFS_CC=0`).
- `tools/freebsd-link.sh` links the FreeBSD object set with the core objects;
  `tools/os5p-boot-test.sh` boots the result headless.

## Why mi_startup is not called yet

FreeBSD's `mi_startup()` runs the sysinit table and later the init kthread
calls `vfs_mountroot()`.  Both need machine state that the native amd64 boot
establishes in `hammer_time()`:

1. `thread0`/`proc0` exist and `PCPU_SET(curthread, &thread0)` is set
   (`sys/amd64/amd64/machdep.c:1205`); `proc0_init` runs at `SI_SUB_INTRINSIC`.
2. FreeBSD's pmap is bootstrapped; our `pmap_glue.c` only records the service
   table, it does not provide `pmap_bootstrap`.
3. Mutexes, the message buffer and the console list are initialised; `printf`
   and `cnadd` are used by early sysinits.

Calling `mi_startup()` before these exist faults inside the first sysinit.

## Slices

0. **FreeBSD runtime probe.**  `port.c` calls FreeBSD's `printf` after the
   console-table handshake.  If the line appears, libkern and the printf path
   execute before any sysinit; if the image stops at `console table bound`,
   the console mutex/msgbuf and thread0 are prerequisites for even the first
   FreeBSD log line.
1. **thread0/proc0 fabrication.**  Allocate FreeBSD `struct thread`/`struct
   proc` from the core heap in the granted window, initialise the fields the
   early sysinits touch (`td_proc`, `td_ucred`, `td_critnest`, `td_intr_nesting_level`,
   `td_pcb`), set `curthread` for the boot CPU, and run `mi_startup()` on a
   core personality task rather than on the core's boot thread.  Verify: the
   sysinit trace reaches `SI_SUB_VFS` without a fault.
2. **pmap bootstrap agreement.**  `pmap_glue.c` must supply the page-table
   primitives `pmap_bootstrap`/`pmap_growkernel` expect, or the port must adopt
   FreeBSD's pmap for the personality window while the core keeps its own for
   kernel memory.  Verify: `pmap_bootstrap` returns and `vm_page` arrays exist.
3. **console and message buffer.**  `cnadd(&zx_cons)` after the console mutex
   exists; `printf` output then lands on the core console.  Verify: a
   `printf("OS_5P: mi_startup reached\n")` line on the serial log.
4. **mountroot.**  Provide a root device or memory root the vnode layer can
   mount (the initramfs can be exported as a memory disk), let
   `vfs_mountroot()` run, and confirm `vfs_mounted` fires with `MNT_ROOTFS`;
   `vfs_set_provider()` then switches the core's path operations to FreeBSD.
5. **Ring3 exposure.**  After the machine port, route the socket syscalls to
   `sys/netinet`, configure pf and NFS through FreeBSD's own mount/ioctl paths,
   and expose cdev nodes through devfs.

## Verification loop

```sh
make -j4 FREEBSD_PERSONALITY=1 out/kernel.elf
tar -cf build/core-obj.tar -C build kernel initramfs.o
# serve src/ on 8004, then in the VM:
sh tools/freebsd-core-sync.sh
python3 tools/freebsd-vm.py --link
# host: nc -l 8010 > build/os5p.elf ; guest: nc 10.0.2.2 8010 < /tmp/os5p.elf
sh tools/os5p-boot-test.sh
```

Each slice is done only when `OS5P_BOOT=PASS` plus its own new marker appears
in `build/os5p-serial.log`.

Copyright © 2026 Nexsteaduser. All Rights Reserved.
