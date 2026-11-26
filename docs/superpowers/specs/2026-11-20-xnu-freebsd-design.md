# OS_5P — XNU-Style Hybrid Kernel: Yuanshuo, FreeBSD 15.1, IO/287

Date: 2026-11-20
Status: Architecture design (M0 milestone). Describes the mandated XNU-style integration; implementation is staged M1–M5 and is not yet an implementation claim.

## 1. Scope and Requirements

### 1.1 What this is

This design integrates the **FreeBSD 15.1-RELEASE** kernel into Zhaoxu OS as an
**auxiliary in-kernel personality**, following the **XNU hybrid-kernel model**:
a small core provides memory, scheduling, IPC and traps, and a richer
personality runs on top of that core **in the same kernel address space and the
same kernel image**.

In XNU the core is Mach and the personality is BSD — and that BSD layer is
itself FreeBSD-lineage code. Here the mapping is direct:

- **Yuanshuo is the Mach-like core.** It keeps ownership of physical and virtual
  memory, the scheduler and kernel threads, the IPC ports, and the trap/syscall
  entry path.
- **FreeBSD is the BSD-like personality.** It provides the mature subsystems —
  vnodes and the file-descriptor table, UFS2/GEOM, the network stack, the POSIX
  process surface — running in kernel space on top of Yuanshuo primitives.
- **IO/287 is the IOKit-like device framework.** It owns device matching and the
  device registry, and every in-kernel driver attaches through it (§5), the way
  XNU's IOKit sits beside Mach and BSD.

The hybrid kernel these three form is **OS_5P**. It is a **single-image hybrid
kernel**, not a virtual machine, not a bhyve guest, not a dual-boot, and not a
userspace FreeBSD process. Those alternatives are explicitly excluded by the
project owner. The core, the personality and IO/287 are compiled and linked into
`KERNEL.ELF` as one image.

### 1.2 Mandated decisions (owner)

- Architecture: **XNU-style single-image hybrid**, FreeBSD as in-kernel
  auxiliary personality. Fixed; not subject to substitution by a "more
  feasible" design.
- FreeBSD version: **15.1-RELEASE** (the 15-STABLE point release on the
  15.0 → 15.2 line, released 2026-06, supported until 2027-03-31). Pinned; the
  build records the source tarball checksum.
- License: **whole project stays GPL-2.0** (GPL-2.0-only) as set by the
  previous milestone. The relicense was originally forced by linking Linux; with
  FreeBSD it is no longer forced, and retaining it is an owner decision (§11).
- Names: the hybrid kernel is **OS_5P** and its device framework is **IO/287**.
  Both are contractual — written exactly that way, never abbreviated, never
  swapped for "IOKit".
- Supersedes: `2026-11-18-xnu-linux-design.md` (Linux 7.4-rc3 personality,
  never implemented beyond M0).

### 1.3 What this is not

- Not a hypervisor/bhyve guest (no separate FreeBSD address space behind EPT).
- Not a FreeBSD kernel booting as a second kernel on the same machine.
- Not a feature-parity exercise. FreeBSD is integrated as a personality; the
  goal is a working hybrid kernel, not a checklist of FreeBSD features.
- Not an invitation to fork FreeBSD. The upstream tree is used as shipped
  (rule 39); all adaptation lives in the glue layer.
- Not a Ring3 device server. IO/287 runs in kernel space (§5); the existing
  `/bin/drivers` set is the migration source, not the target.

## 2. Architectural Principles

1. **One core, one personality, one image.** Yuanshuo owns the hardware and the
   core primitives; FreeBSD consumes them. Both link into `KERNEL.ELF`.
2. **The core stays small and in control.** Memory, scheduling, IPC and trap
   dispatch remain Yuanshuo code. FreeBSD never takes over the boot path, the
   page-table root, or the interrupt vector table directly; it is granted
   services by the core.
3. **FreeBSD is used as shipped (rule 39).** The upstream `15.1-RELEASE` tree is
   vendored unmodified. All Zhaoxu-specific adaptation lives in a separate glue
   / architecture-port layer, never as edits to upstream files.
4. **The personality is bring-up staged.** A minimal FreeBSD (kernel start-up +
   console) is brought up first; VFS, networking and newbus drivers are added
   incrementally. This controls image size and isolates failures.
5. **Honest milestones.** A stage is complete only with a real serial log or
   screenshot from QEMU/hardware (ROADMAP working rules). No simulated results.
6. **IO/287 owns devices.** Device matching and the registry live in IO/287. The
   FreeBSD personality consumes device services through it and never attaches
   hardware behind its back.

## 3. The Yuanshuo Core (Mach role)

Existing primitives the personality builds on (verified locations):

- **Physical memory:** `src/kernel/mem/pmm.c` — bitmap page allocator,
  `pmm_alloc_page` / `pmm_free_page` / `pmm_reserve_range`, `PMM_MAX_PHYS` 64 GiB,
  irqsave spinlock. API in `src/kernel/include/mem.h:29-35`.
- **Virtual memory:** `src/kernel/mem/vmm.c` — `vmm_map_page` / `vmm_unmap_page`
  (:163/:173), `vmm_space_map_page` (:183), identity map of low 64 GiB with
  2 MiB pages (`identity_map_high` :391), mandatory NX (`enable_nx` :426).
- **Scheduler / threads:** `src/kernel/proc/thread.c` — shared thread table,
  LAPIC-timer preemptive scheduling, `kthread_create`, context switch in
  `arch/x86_64/switch.S`.
- **IPC:** Mach-style ports, syscalls 9–14 / 26–27
  (`port_listen/connect/accept/send/recv/close/try_accept/try_recv`),
  `src/kernel/ipc/`.
- **Traps / syscalls:** `int 0x80` entry, `src/kernel/proc/syscall.c`, numbers
  0–78 (78 = getppid); ABI mirrored in `src/user/userlib.h`.
- **Per-CPU state:** `src/kernel/arch/x86_64/percpu.c`, `struct percpu`
  (`include/percpu.h:10-23`), `MAX_CPUS` 16, indexed by LAPIC ID.

The core exports a **personality API** (§8) over these primitives. The API is
kernel-internal (not the Ring3 `int 0x80` ABI); it is the contract FreeBSD binds to.

## 4. The FreeBSD Personality (BSD role)

FreeBSD runs in kernel space (Ring 0) within the Yuanshuo kernel address space.
Its subsystems are compiled into `KERNEL.ELF`. The binding model mirrors how
XNU's BSD layer consumes Mach:

| FreeBSD need | Yuanshuo primitive | Binding |
| --- | --- | --- |
| Physical pages (`vm_page`, UMA zones) | PMM | core grants a reserved region; FreeBSD allocates within it through the personality VM API |
| Page tables / `pmap` | VMM | FreeBSD keeps its `sys/amd64/pmap.c` logic but the page-table root belongs to Yuanshuo; the core API maps/unmaps and sets NX |
| `proc` / `thread` structures, ULE scheduler | kernel threads | each FreeBSD `kthread` is backed by a Yuanshuo thread; the core runs the switch and the timer tick |
| Interrupt threads (`ithread`) | IDT dispatch | core forwards registered vectors to FreeBSD `intr_event` handlers; ACK ordering stays with the core |
| `callout(9)` timers | LAPIC timer | core ticks the FreeBSD `hardclock`/`callout` wheel |
| Kernel objects, `sysctl`, `eventhandler` | — | pure FreeBSD, no binding needed |
| Boot hand-off (`mi_startup`, SYSINIT) | `struct boot_info` | the glue layer converts the core boot info and calls into FreeBSD initialisation |
| Console (`cn(4)`) | core UART / serial | FreeBSD's `cn` driver writes through the core console |
| System calls | trap entry | a personality selector routes the FreeBSD syscall table to the personality (§10) |
| Device access | IO/287 | disk, network and input drivers attach through IO/287 (§5); the personality consumes the resulting nodes instead of attaching hardware itself |
| VFS (rule 51) | core service table | BSD's vnode layer owns the namespace; the personality registers a `zx_vfs_provider` (`core_abi.h` v2) and the core's `vfs_*` path operations route to it |

FreeBSD is **not** given raw control of the boot CPU, the CR3 root, or the IDT;
the core retains those and services the personality. This is the XNU discipline:
BSD does not own Mach's VM or scheduler, it uses them.

Networking follows rule 51 as well: the network stack is FreeBSD's
`sys/net`/`sys/netinet`/`sys/netinet6`, statically linked into the
OS_5P image, not a Zhaoxu implementation and not a userland stack.
Ring3 programs reach it through socket syscalls routed to the BSD
socket layer once the machine port runs; toybox's INET surface is
therefore declarations today and real sockets later.

VFS follows rule 51: the namespace and file operations belong to the BSD
vnode layer, not to the core. `kernel/fs/vfs.c` keeps the core's own mount
providers (initramfs, ramfs, tmpfs, FAT32) only as a transitional provider
while the personality boots. `vfs_set_provider()` is the handover point:
the personality calls the service table's `vfs_register` with a
`struct zx_vfs_provider`, and from then on `vfs_stat`, `vfs_read_at`,
`vfs_read_into`, `vfs_read_all`, `vfs_list`, `vfs_write_file` and
`vfs_unlink` route to BSD. The ABI is version 4 (v3 added `mkdir`/`rmdir`,
v4 added `rename`); the v1 table had no VFS slot.

Owner directive (2026-11-25): **VFS, TCP/IP, cdev, pf and NFS are FreeBSD
code**. The BSD vnode layer supplies the namespace; `sys/net`, `sys/netinet`,
`sys/netinet6` supply IP; `sys/sys/cdev.h` plus `sys/kern/subr_cdev.c` supply
character devices through the FreeBSD cdevsw table; `sys/netpfil/pf` supplies
the packet filter; `sys/fs/nfs` (`nfsclient`, `nfsserver`, `nfscommon`) supplies
NFS. None of these may be reimplemented in the core. The existing AF_UNIX
socket layer in `kernel/ipc` is interprocess plumbing, not an IP stack, and the
core VFS providers remain only as the pre-personality transitional path until
the BSD provider registers. `nfsserver`/`nfsd` and pf keep their FreeBSD
licences (BSD-2-Clause/BSD-3-Clause); the CDDL exclusion of §4.2 still applies
to ZFS/DTrace only.

### 4.0 VFS hand-over slices

The provider entry points already exist in `core_abi.h` v4 (`list`, `stat`,
`read_at`, `write_file`, `unlink`, `mkdir`, `rmdir`, `rename`). The hand-over is
built in slices, each verified in the VM before the next:

1. `freebsd_vfs_provider` in `kernel/personality/freebsd/vfs.c` answers every
   provider entry through FreeBSD's kernel helpers (`kern_statat`,
   `kern_openat`, `kern_pread`, `kern_pwrite`, `kern_mkdirat`,
   `kern_frmdirat`, `kern_renameat`, `kern_funlinkat`,
   `kern_getdirentries`).  It compiles inside the 15.1 tree
   (`tools/freebsd-vfs-check.sh`, `VFS_CC=0`).  The handover waits for the
   `vfs_mounted` event with `MNT_ROOTFS`: all SYSINITs run before the init
   kthread calls `vfs_mountroot()`, and 15.1 has no `SI_SUB_MOUNTROOT`.
   Verified at runtime: the combined image boots, prints the handoff and
   `OS_5P: FreeBSD personality executing` / `console table bound`, so FreeBSD
   port code runs inside OS_5P.  The provider registration itself still waits
   for `mi_startup` and the root mount.
2. The remaining slices exercise these entries under the running personality
   and extend `list`/attribute reporting as the port reaches `mountroot`.
3. `devfs` publishes the FreeBSD cdev nodes; device traffic stays on IO/287 at
   the hardware edge.
4. After the machine port reaches `mi_startup`, `sys/netinet` sockets are
   exposed through the syscall table; pf and the NFS client/server are
   configured through FreeBSD's own ioctls and mount path, never through a
   Zhaoxu reimplementation.

### 4.1 Subsystems in scope

`sys/kern`, `sys/vm`, `sys/ufs` (FFS1/FFS2), `sys/geom`, `sys/fs` (`tmpfs`,
`nullfs`, `devfs`, `nfs`), `sys/net`, `sys/netinet`, `sys/netinet6`,
`sys/netpfil/pf`, `sys/cam`, `sys/security` (MAC framework, Capsicum). `newbus` is kept only for
pseudo-devices internal to the personality (§5.3); real hardware attaches
through IO/287.

### 4.2 Subsystems explicitly excluded

`sys/contrib/openzfs` (ZFS) and `sys/cddl` (DTrace, CTF) are **CDDL**, which is
not compatible with GPL-2.0-only in a single linked image. They are not compiled
into the personality and no upstream file is patched to remove them. This is a
licensing boundary, not a technical limitation; a future owner decision to
switch the project licence could revisit it. `sys/compat` (Linux/FreeBSD 32-bit
ABI emulation) is out of scope until M5.

## 5. IO/287 (the IOKit role)

IO/287 is the third pillar of **OS_5P**: the in-kernel C++ device framework,
filling the role IOKit has in XNU. It is original Zhaoxu code, not upstream
FreeBSD and not a modified copy of IOKit.

### 5.1 Responsibilities

- **Matching.** Drivers publish match criteria (bus-specific probe, or
  vendor/device/class IDs). The framework runs them against nodes published by
  bus providers in a fixed probe order and attaches the first driver that
  claims the node.
- **Registry.** A device tree — root → bus → device → function — with published
  properties, enumerable from the core, the FreeBSD personality and Ring3
  services that hold a device capability.
- **Object model.** A refcounted base object, one root per driver family, and a
  class registry that identifies types without C++ RTTI.
- **Driver families.** Block, network, input, display and bus families. Disks
  reach the personality's GEOM layer and interfaces reach its network stack as
  IO/287 nodes; those subsystems never probe hardware themselves.

### 5.2 Implementation constraints

- **Names.** Every symbol IO/287 exports carries the `io287_` prefix. The core
  uses `yuanshuo_` for the C string family and the other globals FreeBSD also
  defines, so the three parts of `OS_5P` never meet on a plain name; the link
  tool reports any remaining collision instead of letting the linker pick one.
- **Freestanding C++.** No exceptions, no RTTI, no STL, no C++ runtime library —
  the kernel rule that only the Ring3 font sources may use SSE still applies.
  Static constructors do not run automatically in a linked kernel image; IO/287
  runs them explicitly from its init hook before the first probe.
- **Allocation.** Objects come from a reserved physical region through the core
  personality VM API (§8). The framework does not call the FreeBSD allocator
  directly, and the FreeBSD personality does not allocate IO/287 objects.
- **Locking.** One registry lock around tree mutation, per-node locks around
  probe and attach, no sleeping while holding the registry lock, and probe work
  on core kthread context, never in interrupt context.
- **Ordering.** The core initialises first, then IO/287, then the FreeBSD
  subsystems that consume devices. Bus providers attach after the personality's
  memory setup.

### 5.3 Relationship to newbus and the Ring3 driver set

Two in-kernel driver models cannot both own a device. IO/287 is the
device-ownership layer: FreeBSD's `newbus` stays for pseudo-devices that exist
only inside the personality (loopback, memory disks, cloning devices) until each
is bridged to an IO/287 node. The current Ring3 `/bin/drivers` set keeps running
on the microkernel path; migration to IO/287 is per-device and starts only once
M4 lands a working in-kernel driver.

## 6. Source Layout and Vendoring

- `src/third_party/freebsd/` — upstream FreeBSD **15.1-RELEASE** source,
  unmodified (rule 39). Fetched by a Ruby driver (§7); the tarball checksum is
  recorded. Not edited.
- `src/kernel/personality/` — Zhaoxu **glue / architecture-port** layer
  (original code): the personality API implementation, the FreeBSD
  `machine`/`paravirt` binding, trap forwarding, and the core↔FreeBSD bridge.
  This is the only place Zhaoxu-specific adaptation lives, following the
  FreeType / wayland vendor-as-is + glue pattern already used in the tree.
- `src/kernel/include/personality.h` — the core-exported personality API
  contract.
- `src/kernel/io287/` — IO/287: object model, matching, registry and driver
  families. Original code, in-tree, C++, no upstream lineage.

The upstream tree is large (`src.txz` is ~241 MiB packed); it is vendored as a
build input. Default is **fetch-at-build** with a pinned checksum, matching the
newlib/ncurses/nano convention, so the repository does not carry upstream source
while the glue layer stays in-tree.

## 7. Build Integration

- **Ruby driver (rule 6):** `src/tools/freebsd-personality.rb` fetches the
  pinned 15.1-RELEASE source
  (`https://download.freebsd.org/releases/amd64/amd64/15.1-RELEASE/src.txz`, or
  the git tag `release/15.1.0`), verifies its checksum, applies the `ZHAOXU`
  kernel configuration, and drives the cross build.

  Status (2026-11-21): fetch, checksum verification, extraction and overlay are
  implemented and verified (`src.txz` 253,039,000 bytes, SHA-256
  `cf5762da53fd52e1eaf0f9ceee9bf58cbe314c821031d0d9ffa76823185a89e1` from the
  release MANIFEST). `kernel-toolchain` is not yet passing on WSL2: the host
  lacks `flex` and `libssl-dev`, and the bootstrap tools hit the `__unused`
  macro / glibc `struct_mutex.h` conflict.

  The VM fallback (§15) works: inside the FreeBSD 15.1 image,
  `make -j16 buildkernel KERNCONF=ZHAOXU` completed in 162 s and produced
  `kernel` (13,319,904 bytes) and `kernel.full` (52,856,968 bytes) under
  `/usr/obj/usr/src/amd64.amd64/sys/ZHAOXU/`. The image was grown to 16 GiB
  first; the original 6 GiB disk filling up is what stalled the earlier
  attempts.
- **Cross build:** FreeBSD documents building `buildkernel` on non-FreeBSD hosts
  through `tools/build/make.py`, which bootstraps `bmake` and sets `XCC`/`XLD`
  to an external LLVM toolchain. On WSL2 this needs `clang`, `lld`, Python 3 and
  the libarchive/libbz2 headers; the driver checks for all of them before it
  starts. The kernel is built with:
  `MAKEOBJDIRPREFIX=<build> tools/build/make.py TARGET=amd64 TARGET_ARCH=amd64 KERNCONF=ZHAOXU buildkernel`
- **Kernel config:** `ZHAOXU` derives from the tree's `MINIMAL` amd64 config —
  serial console, scheduler and VM in, CAM/network/GEOM staged in by milestone.
- **Makefile:** a `FREEBSD_PERSONALITY ?= 0` flag follows the `M0_BOOT_TEST`
  pattern (`src/Makefile:98-99`, stamp :119-123). When enabled, the personality
  objects link into `KERNEL.ELF`; a stamp forces rebuild on toggle.
- **IO/287:** built by the Zhaoxu Makefile, not by bmake, with clang++ and
  `-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics`; no C++
  runtime, no STL, no implicit static constructors. It links whenever the
  personality is enabled, because the personality's device consumers depend on
  it.
- **Image size:** the flat-kernel cap is 64 MiB (`tools/kernel-flat.rb:18`).
  The kernel must be built without debug symbols and with a minimal config;
  bringing the full GENERIC kernel in does not fit and is not the target.

## 8. M1 — Core Primitive Export

The core grows a kernel-internal personality API (`personality.h`), implemented
in `src/kernel/personality/`:

- **Kernel threads for tasks:** extend `kthread_create` so FreeBSD `kthread`s
  can be created/destroyed and scheduled by the core; per-task core state.
- **In-kernel IPC:** extend the port layer (`src/kernel/ipc/`) with kernel-space
  endpoints so the core and personality exchange messages without Ring3 hops.
- **Personality VM API:** a reserved physical region (early `pmm_reserve_range`
  before fragmentation) plus map/unmap/protect calls backed by `vmm.c`, with NX.
- **Trap forwarding:** `interrupt_dispatch` (`int/idt.c:157`) gains a hook so the
  personality can register handlers for the vectors it owns; the core keeps
  default handling and fault isolation.
- **Timer service:** expose the LAPIC timer to the personality as a periodic tick.

M1 is Yuanshuo-side only; no FreeBSD code is linked yet. It is verified by a
kernel self-test that exercises the API (serial markers), before any FreeBSD
bring-up.

Status (2026-11-21): implemented. `kernel/personality/personality.c` provides
the task, VM, trap and timer services; `kernel/ipc/kport.c` provides the
kernel-space endpoint pairs; `kernel/personality/selftest.c` runs under
`PERSONALITY_SELFTEST=1` and reports `[per ] ... PASS` for VM map/write/unmap,
task thread, kernel IPC, trap forwarding and timer service on QEMU.

## 9. M2 — FreeBSD Architecture Port

- Create the Zhaoxu machine binding for FreeBSD in the glue layer: boot hand-off
  from `struct boot_info` in place of `locore`'s assumptions, memory setup over
  the personality VM API, interrupt hookup through trap forwarding, context
  switch over core threads, and the syscall entry binding.
- Keep FreeBSD's `sys/amd64` as the machine-dependent body where it does not
  fight the core: `pmap`, `trap.c` exception decoding, signal frame layout,
  `cpu_switch` call sites. Replace what the core owns: console, timer, interrupt
  controller, and physical-memory discovery.
- Land IO/287 alongside the port: object model, registry and matching, with one
  synthetic bus provider so the registry enumerates something before real
  hardware drivers exist.
- Build FreeBSD 15.1 with this binding as a personality library and link it into
  `KERNEL.ELF` behind `FREEBSD_PERSONALITY=1`.
- Add the FreeBSD entry to `THIRD_PARTY_NOTICES.md` (BSD-2-Clause, per
  `sys/COPYRIGHT`) once the tree is actually vendored/built — not before
  (rule 42 honesty).

M2 is the largest single engineering step: porting a BSD kernel onto a foreign
core is a major effort, and XNU is the proof that it is a real one. It is staged
and honest about that; the architecture is not changed to avoid it.

Status (2026-11-21): slice 1 adds the `FREEBSD_PERSONALITY` gate and a weak
boot seam; slice 2 measured the pmap linkage surface. `pmap.o` has 171
undefined symbols: 154 resolve inside the FreeBSD kernel objects, and the 17
left are linker-generated (15 `__start_set_*`/`__stop_set_*`, `_end`,
`brsection`). The core linker script now provides `_end`, `brsection` and the
five `set_*` sections. Slice 3 adds `personality_freebsd.h`, a versioned
`freebsd_core_services` table the port binds through (both trees define
`kmalloc`, `printf`, scheduler primitives, so direct linking would clash), and
`freebsd_pmap_init()`, which maps, writes and reads back a probe word in the
granted VM window. Slice 4 moves that contract into `core_abi.h` with portable
types so both kernels compile it, and `probe.c` builds inside the 15.1 tree
with the pmap surface visible (`PROBE=0`). Slice 5 adds `pmap_glue.c`, the
first port-side source: it takes the service table, checks version and page
size, and relocates with `pmap.o` (`ld.lld -r`, `RELOC=0`) without changing the
171-symbol dependency surface. Slice 6 merges the whole FreeBSD kernel object
set with the glue into one relocatable object; only 64 symbols stay undefined,
all linker-generated (`__start_set_*`/`__stop_set_*`, `__build_id_*`,
`_DYNAMIC`, `_end`, `_etext`, `brsection`, undefined-weak ZSTD hooks), and the
core linker script now provides them. Slice 7 settles symbol isolation: of
13,287 FreeBSD globals and 546 Yuanshuo globals, exactly 19 collide (the C
library string family, `shm_*`, `tty_ioctl`); `llvm-objcopy
--prefix-symbols=bsd_` moves every FreeBSD global into the `bsd_` namespace
without touching the 64 linker symbols. Slice 8 shows why prefixing is the
wrong tool: it also renames references to linker-provided symbols, so the
strategy becomes `--redefine-syms` on the 19 collisions only. After that, the
merged set plus the Yuanshuo objects link down to three names: the build-id
pair resolves with `--defsym`, `set_*` symbols materialise from orphan
sections, and `brsection` remains unresolved against every definition tried so
far. Slice 9 shows the name is not even present in the merged object's symbol
table, so lld is reporting a reference it cannot have read from that table;
the same diagnostic survives a script `PROVIDE`, `--defsym`, a defining object
and an objcopy rename. Slice 10 links the original objects directly and narrows
the remainder to that single name. Slice 11 finds the cause: the identifier is
`brwsection` in `pmap.c`, and the misspelling `brsection` in the script was
the whole problem. With `PROVIDE(brwsection = .)` the direct link of 1,069
FreeBSD objects and 80 Yuanshuo objects completes with zero undefined symbols
(a 73 MB image with debug info); 19 duplicate globals are resolved with
`--allow-multiple-definition` until per-object renames replace it. Slice 12
sizes the real image and makes the window fit it: text 5,588,168 B, rodata
802,104 B, data 702,024 B and bss 84,161,920 B, ending at `__kernel_end`
`0xffffffff85885580`. `KERNEL_STACK_TOP` moves to `0xffffffff87f00000`, which
gives the image a 128 MiB window with the ASSERT holding it 2 MiB below the
boot-stack mapping, and the same 1,069 + 80 objects now link with `LINK=0`
under the shipped script and no warnings. The first layout used a 2 MiB-aligned
top (`0xffffffff88000000`) and the ISO stopped at Limine: `limine_boot` maps
the 2 MiB page that holds the stack top, and an aligned top selects the page
above it, so the first push faulted. The mapping now derives the window from
the top's last byte. The sysinit sets resolve as
`__start_set_sysinit_set`/`__stop_set_sysinit_set` around the FreeBSD table.
The image no longer needs a split layout; the machine port itself — pmap
ownership implemented, interrupts, timer, `cn(4)` console backend — is what
still blocks booting it. Slice 13 removes the provisional
`--allow-multiple-definition`: `tools/freebsd-link.sh` derives the collision
set from the objects themselves, renames the core side's copies with a `core_`
prefix through `llvm-objcopy --redefine-syms` into a mirror directory, and
links the renamed set with the shipped script — `19 collision(s)`, `renamed 80
core object(s)`, `LINK=0`, 73,185,056 bytes, only weak ZSTD trace hooks
undefined. FreeBSD's objects stay byte-identical to its build; `--rename-side
bsd` exists for completeness but touches 1,069 objects and is far slower. Slice
14 moves the rename into the source: the core's 14 C string functions and
`lapic_eoi`, `kthread_exit`, `shm_map`, `shm_unmap`, `tty_ioctl` are
`yuanshuo_*`, and `string.h` applies the aliases only to kernel translation
units, so Ring3 code that links the same `string.c` keeps the plain names. The
tool now reports `0 collision(s)` and links with `LINK=0` (73,186,264 bytes)
without rewriting an object; FreeBSD's IFUNC `memcpy` and `yuanshuo_memcpy`
coexist. The first port-side console backend, `port.c`, registers a cn(4)
device whose `cn_putc` calls the service table's `console_write`, and
`freebsd_personality_main` is a strong symbol in the image; executing it still
waits for the machine port.

## 10. Bring-up Staging (M3–M5)

- **M3 — minimal boot + console.** Bring the personality to `mi_startup` and a
  `cn(4)` console on the core-provided UART. Verify with a real serial log
  showing FreeBSD boot messages emitted through the Yuanshuo console.

  Status (2026-11-21): the core half is implemented — `personality_start()`
  hands over `struct boot_info`, `personality_console_write()` routes output
  through the core console — and `PERSONALITY_SELFTEST=1` reports
  `[per ] boot handoff PASS` and `[per ] console service PASS`. The stage stays
  open until the personality is linked and reaches `mi_startup`, which needs
  M2.
- **M4 — subsystems.** Add UFS2/GEOM over IO/287 block nodes, then the network
  stack over IO/287 network nodes, then the first real hardware driver in
  IO/287, incrementally, each with serial/screenshot evidence. Device access
  follows the r9 resolution (§12).
- **M5 — user-facing personality.** A personality selector at the syscall entry
  routes FreeBSD syscalls (`sys/kern/syscalls.master` numbering) to FreeBSD and
  Yuanshuo syscalls to the core. FreeBSD ELF binaries run against the
  personality; the Zhaoxu desktop remains native and does not depend on a
  FreeBSD GUI component (zaf-architecture constraint).

## 11. Licensing

- The **whole project is GPL-2.0** (GPL-2.0-only), set at the previous
  milestone. `LICENSE` carries the canonical GPL-2.0 text.
- FreeBSD kernel source is **BSD-2-Clause** (some files BSD-3-Clause or ISC;
  both are GPL-2.0-compatible). The source tree also carries CDDL code under
  `sys/contrib/openzfs` and `sys/cddl`, which is why §4.2 excludes both.
- Unlike the earlier Linux plan, **BSD-2-Clause imposes no copyleft obligation**
  on the combined image. Staying GPL-2.0 is therefore an owner choice, not a
  legal requirement; reverting to a permissive licence would be a separate
  owner decision and is not implied by this document.
- FreeBSD's `COPYRIGHT` notice must be preserved in `THIRD_PARTY_NOTICES.md`
  when the tree is vendored (M2).
- The rule-35 copyright footers (`All Rights Reserved`) are retained; they are
  copyright notices, not license terms, and do not conflict with the GPL grant.

## 12. Rule Dependencies (owner action; rule 41)

- **r9 (clean microkernel, drivers/apps in Ring3).** This design puts both the
  FreeBSD subsystems and **IO/287**, an in-kernel C++ device framework, inside
  the kernel image. That contradicts r9 as written on two counts: the in-kernel
  personality, and the in-kernel driver framework that replaces Ring3 driver
  attachment. **The owner must amend r9** (or define an explicit "in-kernel
  auxiliary personality + IO/287" exception). Per rule 41 the AI does not edit
  `RULES.md`; this is flagged as an owner action and blocks M4 device
  integration by rule, not by code.
- **r5 (languages).** r5 assigns C to the kernel, C++ to applications and SPARK
  to drivers. IO/287 is in-kernel C++ with C++ driver families, so it needs an
  owner amendment on two points: "kernel = C" and "drivers = SPARK". Flagged,
  not edited (rule 41).
- **r39 (third party as-is).** Satisfied: FreeBSD vendored unmodified, glue
  separate.
- **r14 / r42 (real port, no fake third party).** Satisfied: FreeBSD is a
  genuine upstream port; the glue is original and not presented as upstream.
- **r6 (Ruby scripts), r5 (WSL2 host).** Satisfied by the build driver (§7).
- **r31 (boot chain).** The boot chain stays firmware → Limine → `KERNEL.ELF`;
  the personality is initialised by the core after boot, Limine is unchanged (r4).

## 13. Testing and Verification

Each stage requires real evidence (ROADMAP working rules), in the AGENTS.md
serial-smoke style:

- **M0:** licence consistency grep; LICENSE/README/CONTRIBUTING/THIRD_PARTY
  notices agree; no lingering Linux-personality claims outside the changelog
  history.
- **M1:** kernel self-test serial markers exercising the personality API
  (threads, in-kernel IPC, VM region, trap forwarding) — no FreeBSD linked yet.
  Shipped: `PERSONALITY_SELFTEST=1` prints `[per ]` markers and ends with
  `[per ] personality API PASS`.
- **M2:** `FREEBSD_PERSONALITY=1` build links cleanly; `nm` shows the personality
  symbols and IO/287's registry; image within the 64 MiB flat cap;
  `nm -u out/kernel.elf` still empty.
- **M3:** serial log shows FreeBSD 15.1 `mi_startup` / console output through the
  Yuanshuo console to a minimal init.
- **M4/M5:** per-subsystem serial/screenshot evidence; FreeBSD syscall routing
  verified with a real FreeBSD ELF binary.

## 14. Milestones

| Stage | Deliverable | Evidence |
| --- | --- | --- |
| M0 | FreeBSD 15.1 design (this file); supersedes the Linux plan | docs; licence grep |
| M1 | Core personality API (threads, IPC, VM, traps, timer) — delivered | kernel self-test serial (`[per ] personality API PASS`) |
| M2 | FreeBSD machine port; 15.1 and IO/287 linked into KERNEL.ELF | clean link; nm; size cap |
| M3 | Minimal FreeBSD boot + console | serial boot log |
| M4 | UFS2/GEOM and networking over IO/287 nodes; first hardware driver | per-subsystem serial/shots |
| M5 | Syscall personality routing; FreeBSD binaries run | real FreeBSD binary run |

The goal is the **complete** XNU-style integration (M5). Staging reflects
engineering order, not a reduction of the target.

## 15. Risks

- **FreeBSD architecture port effort.** M2 is a major kernel port; it is the
  dominant cost and schedule risk. Staged bring-up isolates it.
- **Image size.** A full GENERIC kernel exceeds the 64 MiB flat cap; the
  `ZHAOXU` minimal config and staged subsystems manage this. A larger cap or
  split loading is an owner decision if needed.
- **r9/r5 conflict.** In-kernel drivers require the owner's r9 amendment, and
  in-kernel C++ plus C++ drivers require the r5 amendment (§12); until then M4
  device integration is blocked by rule, not by code.
- **Build-host dependency.** FreeBSD cross-builds are documented on Linux/macOS
  via `tools/build/make.py` but are less exercised than native builds; a
  FreeBSD VM under Hyper-V is the fallback host.
- **Core/personality fault isolation.** A personality fault must not take down
  the core; trap forwarding and VM isolation need careful design (M1).
- **SMP.** FreeBSD SMP bring-up on the Yuanshuo scheduler is deferred until the
  single-core personality is stable.
- **IO/287 scope creep.** A device framework with a registry and driver families
  is a kernel project of its own. The boundary is fixed here: IO/287 matches and
  owns devices, it does not become a second VFS or network stack, and the
  FreeBSD personality keeps those subsystems.

## 16. Explicit Decisions

1. **Architecture is XNU-style single-image hybrid.** Owner-mandated. Yuanshuo =
   Mach-like core; FreeBSD = BSD-like in-kernel personality; **IO/287** =
   IOKit-like in-kernel device framework. The three form **OS_5P**, one image.
   No VM, no bhyve, no dual-boot, no userspace FreeBSD.
2. **FreeBSD version is 15.1-RELEASE**, pinned by checksum.
3. **Whole project stays GPL-2.0** (GPL-2.0-only); FreeBSD's BSD-2-Clause is
   compatible and CDDL parts stay unlinked (§4.2).
4. **FreeBSD is vendored as-is** (rule 39); all adaptation is in the separate
   glue / machine-port layer.
5. **The boot chain and Limine are unchanged** (r4/r31); the core initialises the
   personality after boot.
6. **r9 amendment is an owner action** (rule 41); the AI does not edit RULES.md.
7. **The target is full integration (M5).** Staging is engineering order; the
   architecture is never substituted to reduce effort.
8. **IO/287 is in-kernel, C++, and owns devices.** It is original code in
   `src/kernel/io287/`, linked into `KERNEL.ELF`, freestanding with no C++
   runtime. The existing Ring3 driver set migrates per-device; it is not deleted
   on the day IO/287 lands.

---

Copyright (C) 2026 Nexsteaduser, Nexlyh, Tinmc189623. All Rights Reserved.
