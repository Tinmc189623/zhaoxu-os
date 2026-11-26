# Driver model

This is the driver and extension model RULES.md v1.14.3+ (rules 48–53) sets
for Zhaoxu OS, written against what the tree implements today. Status is
stated per item; nothing unbuilt is described as working.

## FHS 3.0+ root (rule 48)

The live root is the initramfs lower plus a ramfs copy-up. The Makefile
creates the FHS top-level directories in the staged root even when nothing
populates them yet:

```text
/            root
/bin         essential commands and the Ring3 programs
/boot        boot assets mirrored from the ESP
/dev         device nodes (the devfs view is served from /proc and /sys today)
/etc         configuration
/home        user home directories
/lib         dependencies loaded by plain filename
/lib64       reserved for 64-bit-only libraries
/media       removable media mount points
/mnt         data volume mount point (ZHAOXUDATA)
/opt         optional packages
/proc        kernel process information
/root        root home directory
/run         runtime state (/run/os-release is published at boot)
/sbin        system binaries
/srv         service data
/sys         kernel and device information
/tmp         temporary files
/usr         user hierarchy (bin, lib, share)
/var         variable data
```

`/bin`, `/boot`, `/etc`, `/proc`, `/run`, `/sys`, `/tmp`, `/usr` and `/mnt`
are populated already; the rest exist as empty directories until a service
uses them.

## Userland Driver: `.udrv` (rule 49)

A userland driver is a bundle, not a bare executable:

```text
drivers.udrv/
├── manifest        # key=value: name, version, abi=ZXE-1, executable
└── executable      # ZXE v1 image
```

- The executable inside is a ZXE v1 image; the bundle itself is the unit that
  gets installed and named.
- `tools/udrv-pack.py --name drivers --version 1 out/drivers.zxe out/drivers.udrv`
  builds the bundle; `tools/udrv-check.py out/drivers.udrv` validates the
  manifest keys and the ZXE magic.
- `process_spawn(path)` resolves a bundle: when the path is a directory it
  reads `path/executable`. The kernel therefore starts
  `/bin/drivers.udrv` without a separate loader format.
- The Ring3 driver set (Ada + C, `/bin/drivers.udrv`) shares four physical
  pages through `kernel/include/dev.h`: block I/O, RTC, input rings and kernel
  logs. `/bin/ext4fs` holds the trusted block capability and `/bin/display`
  the display capability.
- The ISO carries the same image as `casper/vga10h.kext` because the media layout
  of RULES.md names that file; the build artifact is the `.udrv` bundle.
- `/bin/ext4fs.udrv` (filesystem service) and `/bin/display.udrv` (KMS
  service) ship as bundles too; the kernel grants the trusted block and
  display capabilities by matching the bundle path or its `executable`.

## Kernel Extension: kext (rule 50)

A kext is an OS_5P kernel extension: a dynamic module that is loaded into the
kernel address space and runs in ring 0.

- The format, ABI and bundle layout are **OS_5P's own design**. They are not
  compatible with the macOS XNU kext format, ABI or bundle layout.
- A kext is a bundle of its own, with a manifest that names the payload and
  its entry symbol; payload code uses the `yuanshuo_` and `io287_` namespaces
  and the versioned core service table, never raw core symbols.
- Status: not implemented. There is no kext loader, no kext ABI version and
  no in-kernel module relocation yet. The definition above is the contract
  the loader will implement; do not ship a kext before it exists.

## NKE: Network Kernel Extension (rule 52)

An NKE is a kext subtype that attaches to the BSD network stack inside the
personality — packet filters, socket filters and protocol extensions. It is
loaded into the kernel like any kext and is not an IO/287 driver.

- Status: not implemented; it depends on the kext loader and on the network
  stack being brought up (M4).

## Statically linked BSD drivers (rule 51)

The BSD-native subsystems — VFS, TCP/IP, cdev, pf, NFS — are compiled into the
single OS_5P kernel image. They are not separate kexts, are not loaded later,
and exist in the kernel address space from boot.

- Current state: the FreeBSD 15.1 object set is linked into the same image as
  the Yuanshuo core (`tools/freebsd-link.sh`, `LINK=0`, zero undefined
  symbols), which is exactly this rule's shape. Nothing is loaded on demand.
- The machine port (pmap ownership, interrupts, timer, `cn(4)` backend) is
  still what keeps that image from running the BSD subsystems; the linker
  side is done.

## Ownership summary

| Component | Runs in | Loaded | Namespace |
| --- | --- | --- | --- |
| Yuanshuo core | ring 0 | built into the image | `yuanshuo_` |
| IO/287 framework | ring 0 | built into the image | `io287_` |
| BSD subsystems (VFS, TCP/IP, cdev, pf, NFS) | ring 0 | built into the image | FreeBSD's own names |
| kext / NKE | ring 0 | dynamic, later | `yuanshuo_`/`io287_` + manifest |
| Userland driver (`.udrv`) | ring 3 | started by the core | ZXE image inside the bundle |

The architecture follows XNU (rule 53): a Mach-role core (Yuanshuo), a BSD
role (FreeBSD 15.1 personality) and an IOKit-role framework (IO/287), in one
kernel image.
