# XNU-Style Hybrid Kernel — Linux 7.4-rc3 Auxiliary Personality

Date: 2026-11-18
Status: Architecture design (M0 milestone). Describes the mandated XNU-style integration; implementation is staged M1–M5 and is not yet an implementation claim.

## 1. Scope and Requirements

### 1.1 What this is

This design integrates the Linux kernel **7.4-rc3** into Zhaoxu OS as an
**auxiliary in-kernel personality**, following the **XNU hybrid-kernel model**:
a small core provides memory, scheduling, IPC and traps, and a richer
personality runs on top of that core **in the same kernel address space and the
same kernel image**.

In XNU the core is Mach and the personality is BSD. Here:

- **Yuanshuo is the Mach-like core.** It keeps ownership of physical and virtual
  memory, the scheduler and kernel threads, the IPC ports, and the trap/syscall
  entry path.
- **Linux is the BSD-like personality.** It provides the mature subsystems —
  VFS, networking, the driver model, the POSIX process surface — running in
  kernel space on top of Yuanshuo primitives.

This is a **single-image hybrid kernel**, not a virtual machine, not a
dual-boot, not a separate guest, and not a userspace Linux process. Those
alternatives are explicitly excluded by the project owner. The Linux code is
compiled and linked into `KERNEL.ELF` alongside the Yuanshuo core.

### 1.2 Mandated decisions (owner)

- Architecture: **XNU-style single-image hybrid**, Linux as in-kernel auxiliary
  personality. Fixed; not subject to substitution by a "more feasible" design.
- Linux version: **7.4-rc3** (kernel.org mainline, released 2026-11-15). Pinned;
  the build records the tarball checksum. RC instability is accepted by the owner.
- License: **whole project GPL-2.0** (GPL-2.0-only). See §10.

### 1.3 What this is not

- Not a hypervisor/VMX guest (no separate Linux address space behind EPT).
- Not User-Mode Linux (Linux does not run as a Ring3 process on a host ABI).
- Not coLinux-style two-binary cooperative switching (the target is one image).
- Not a feature-parity exercise. Linux is integrated as a personality; the goal
  is a working hybrid kernel, not a checklist of Linux features.

## 2. Architectural Principles

1. **One core, one personality, one image.** Yuanshuo owns the hardware and the
   core primitives; Linux consumes them. Both link into `KERNEL.ELF`.
2. **The core stays small and in control.** Memory, scheduling, IPC and trap
   dispatch remain Yuanshuo code. Linux never takes over the boot path, the
   page-table root, or the interrupt vector table directly; it is granted
   services by the core.
3. **Linux is used as shipped (rule 39).** The upstream Linux 7.4-rc3 tree is
   vendored unmodified. All Zhaoxu-specific adaptation lives in a separate glue
   / architecture-port layer, never as edits to upstream files.
4. **The personality is bring-up staged.** A minimal Linux (boot + console) is
   brought up first; VFS, networking and drivers are added incrementally. This
   controls image size and isolates failures.
5. **Honest milestones.** A stage is complete only with a real serial log or
   screenshot from QEMU/hardware (ROADMAP working rules). No simulated results.

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
  0–77 (77 = stat); ABI mirrored in `src/user/userlib.h`.
- **Per-CPU state:** `src/kernel/arch/x86_64/percpu.c`, `struct percpu`
  (`include/percpu.h:10-23`), `MAX_CPUS` 16, indexed by LAPIC ID.

The core exports a **personality API** (§7) over these primitives. The API is
kernel-internal (not the Ring3 `int 0x80` ABI); it is the contract Linux binds to.

## 4. The Linux Personality (BSD role)

Linux runs in kernel space (Ring 0) within the Yuanshuo kernel address space.
Its subsystems are compiled into `KERNEL.ELF`. The binding model mirrors how
XNU's BSD consumes Mach:

| Linux need | Yuanshuo primitive | Binding |
| --- | --- | --- |
| Physical pages | PMM | core grants a reserved region; Linux allocates within it via the personality VM API |
| Page tables / mapping | VMM | Linux manages its own tables inside the granted region; core API maps/unmaps and sets NX |
| Tasks / scheduling | kernel threads | Linux tasks are backed by Yuanshuo threads; the core schedules them |
| Interrupts / exceptions | IDT dispatch | core forwards relevant vectors to registered Linux handlers |
| Timers | LAPIC timer | core exposes timer services to the personality |
| Core ↔ personality IPC | ports | in-kernel port messages (extended for kernel-space endpoints) |
| Device access | Ring3 drivers / core | see §11 rule dependency (r9) |
| System calls | trap entry | a personality selector routes Linux syscalls to the Linux handler (§9) |

Linux is **not** given raw control of the boot CPU, the CR3 root, or the IDT;
the core retains those and services the personality. This is the XNU discipline:
BSD does not own Mach's VM or scheduler, it uses them.

## 5. Source Layout and Vendoring

- `src/third_party/linux/` — upstream Linux **7.4-rc3**, unmodified (rule 39).
  Fetched by a Ruby driver (§6); the tarball checksum is recorded. Not edited.
- `src/kernel/personality/` — Zhaoxu **glue / architecture-port** layer (original
  code): the personality API implementation, the Linux `arch/zhaoxu` (or
  paravirt) binding, trap forwarding, and the core↔Linux bridge. This is the
  only place Zhaoxu-specific adaptation lives, following the FreeType / wayland
  vendor-as-is + glue pattern already used in the tree.
- `src/kernel/include/personality.h` — the core-exported personality API contract.

The upstream Linux tree is large; it is vendored as a build input. Whether it is
committed or fetched at build time follows the same convention as
newlib/ncurses/nano (fetched by `tools/posix-ports.rb`-style Ruby drivers); the
default is **fetch-at-build** with a pinned checksum to avoid committing ~150 MB
of upstream source, while keeping the glue layer in-tree.

## 6. Build Integration

- **Ruby driver (rule 6):** `src/tools/linux-personality.rb` fetches the pinned
  7.4-rc3 tarball (`https://git.kernel.org/torvalds/t/linux-7.4-rc3.tar.gz`),
  verifies its checksum, configures a minimal guest-personality defconfig, and
  builds the Linux subsystem objects with the Zhaoxu arch/paravirt layer.
- **Host (rule 5):** Linux is cross-built in WSL2 (the preferred Windows host),
  using the existing clang/gcc cross toolchain pattern. Required host deps
  (flex, bison, libelf, bc, perl, python3, cpio) are checked by the driver.
- **Makefile:** a `LINUX_PERSONALITY ?= 0` flag follows the `M0_BOOT_TEST`
  pattern (`src/Makefile:98-99`, stamp :119-123). When enabled, the Linux
  personality objects link into `KERNEL.ELF`; a stamp forces rebuild on toggle.
- **Image size:** the flat-kernel cap is 64 MiB (`tools/kernel-flat.rb:18`).
  Bring-up is staged minimal (§9) so the linked personality stays within budget;
  subsystems are added as the image allows.

## 7. M1 — Core Primitive Export

The core grows a kernel-internal personality API (`personality.h`), implemented
in `src/kernel/personality/`:

- **Kernel threads for tasks:** extend `kthread_create` so Linux tasks can be
  created/destroyed and scheduled by the core; per-task core state.
- **In-kernel IPC:** extend the port layer (`src/kernel/ipc/`) with kernel-space
  endpoints so the core and personality exchange messages without Ring3 hops.
- **Personality VM API:** a reserved physical region (early `pmm_reserve_range`
  before fragmentation) plus map/unmap/protect calls backed by `vmm.c`, with NX.
- **Trap forwarding:** `interrupt_dispatch` (`int/idt.c:157`) gains a hook so the
  personality can register handlers for the vectors it owns; the core keeps
  default handling and fault isolation.
- **Timer service:** expose the LAPIC timer to the personality.

M1 is Yuanshuo-side only; no Linux code is linked yet. It is verified by a
kernel self-test that exercises the API (serial markers), before any Linux
bring-up.

## 8. M2 — Linux Architecture Port

- Create the Zhaoxu arch/paravirt binding for Linux in the glue layer: boot
  handoff from the core, memory setup over the personality VM API, interrupt
  hookup via trap forwarding, context switch over core threads, and the syscall
  entry binding.
- Build Linux 7.4-rc3 with this binding as a personality library and link it into
  `KERNEL.ELF` behind `LINUX_PERSONALITY=1`.
- Add the Linux entry to `THIRD_PARTY_NOTICES.md` (GPL-2.0 WITH Linux-syscall-note)
  once the tree is actually vendored/built — not before (rule 42 honesty).

M2 is the largest single engineering step: a Linux architecture port is a major
kernel effort. It is staged and honest about that; the architecture is not
changed to avoid it.

## 9. Bring-up Staging (M3–M5)

- **M3 — minimal boot + console.** Bring the personality up to a serial console
  (`console=` on the core-provided UART), `init=` minimal. Verify with a real
  serial log showing Linux boot messages emitted through the Yuanshuo console.
- **M4 — subsystems.** Add VFS, then networking, then the driver model,
  incrementally, each with serial/screenshot evidence. Device access follows the
  r9 resolution (§11).
- **M5 — user-facing personality.** A personality selector at the syscall entry
  routes Linux syscalls to Linux and Yuanshuo syscalls to the core (XNU's
  Mach-trap vs BSD-syscall split). Linux binaries run against the personality;
  the Zhaoxu desktop remains native and does not depend on a Linux GUI component
  (zaf-architecture constraint).

## 10. Licensing

- The **whole project is GPL-2.0** (GPL-2.0-only). `LICENSE` carries the canonical
  GPL-2.0 text; the Apache `NOTICE` was removed; README/CONTRIBUTING updated.
- Linux is `GPL-2.0 WITH Linux-syscall-note` (version 2 only, verified from
  upstream `COPYING`). A single-image hybrid that links Linux code is a derivative
  work of Linux and must be GPL-2.0 — which the project now is. GPL-3.0 cannot
  cover such a combined image; that is why the project moved to GPL-2.0.
- Other third-party components keep their own licenses and are aggregated, not
  relicensed (`THIRD_PARTY_NOTICES.md`). bash (GPL-3.0+) is a separate Ring3
  program (aggregation); FreeType uses its GPLv2 option (compatible).
- The rule-35 copyright footers (`All Rights Reserved`) are retained; they are
  copyright notices, not license terms, and do not conflict with the GPL grant.

## 11. Rule Dependencies (owner action; rule 41)

- **r9 (clean microkernel, drivers/apps in Ring3).** An XNU-style in-kernel Linux
  personality places Linux subsystems (including its driver model) inside the
  kernel image, which conflicts with r9 as written. **The owner must amend r9**
  (or define an explicit "in-kernel auxiliary personality" exception). Per rule
  41 the AI does not edit `RULES.md`; this is flagged as an owner action and is a
  dependency for M4 device integration, not an objection to the architecture.
- **r39 (third party as-is).** Satisfied: Linux vendored unmodified, glue separate.
- **r14 / r42 (real port, no fake third party).** Satisfied: Linux is a genuine
  upstream port; the glue is original and not presented as upstream.
- **r6 (Ruby scripts), r5 (WSL2 host).** Satisfied by the build driver (§6).
- **r31 (boot chain).** The boot chain stays firmware → Limine → `KERNEL.ELF`;
  the personality is initialized by the core after boot, Limine is unchanged (r4).

## 12. Testing and Verification

Each stage requires real evidence (ROADMAP working rules), in the AGENTS.md
serial-smoke style:

- **M0:** license consistency grep (no residual Apache project-license claim
  outside third-party components); LICENSE/README/CONTRIBUTING agree on GPL-2.0.
- **M1:** kernel self-test serial markers exercising the personality API
  (threads, in-kernel IPC, VM region, trap forwarding) — no Linux linked yet.
- **M2:** `LINUX_PERSONALITY=1` build links cleanly; `nm` shows the personality
  symbols; image within the 64 MiB flat cap.
- **M3:** serial log shows Linux 7.4-rc3 boot messages through the Yuanshuo
  console to a minimal init.
- **M4/M5:** per-subsystem serial/screenshot evidence; Linux syscall routing
  verified with a real Linux binary.

## 13. Milestones

| Stage | Deliverable | Evidence |
| --- | --- | --- |
| M0 | GPL-2.0 relicense + this design + ROADMAP entry | license grep; docs |
| M1 | Core personality API (threads, IPC, VM, traps, timer) | kernel self-test serial |
| M2 | Linux arch/zhaoxu port; 7.4-rc3 linked into KERNEL.ELF | clean link; nm; size cap |
| M3 | Minimal Linux boot + console | serial boot log |
| M4 | VFS / networking / driver model | per-subsystem serial/shots |
| M5 | Syscall personality routing; Linux binaries run | real Linux binary run |

The goal is the **complete** XNU-style integration (M5). Staging reflects
engineering order, not a reduction of the target.

## 14. Risks

- **Linux architecture port effort.** M2 is a major kernel port; it is the
  dominant cost and schedule risk. Staged bring-up isolates it.
- **Image size.** Full Linux exceeds the 64 MiB flat cap; minimal defconfig and
  incremental subsystems manage this. A larger cap or split loading is an owner
  decision if needed.
- **r9 conflict.** In-kernel drivers require the owner's r9 amendment (§11);
  until then M4 device integration is blocked by rule, not by code.
- **RC instability.** 7.4-rc3 is a release candidate; the owner accepts this.
  The pinned checksum makes the build reproducible; bumping is a deliberate act.
- **Core/personality fault isolation.** A personality fault must not take down
  the core; trap forwarding and VM isolation need careful design (M1).
- **SMP.** Linux SMP bring-up on the Yuanshuo scheduler is deferred until the
  single-core personality is stable.

## 15. Explicit Decisions

1. **Architecture is XNU-style single-image hybrid.** Owner-mandated. Yuanshuo =
   Mach-like core; Linux = BSD-like in-kernel personality. No VM, no dual-boot,
   no userspace Linux, no "spirit-only" approximation.
2. **Linux version is 7.4-rc3**, pinned by checksum.
3. **Whole project is GPL-2.0** (GPL-2.0-only), matching Linux and legal for the
   single-image merge.
4. **Linux is vendored as-is** (rule 39); all adaptation is in the separate glue
   / arch-port layer.
5. **The boot chain and Limine are unchanged** (r4/r31); the core initializes the
   personality after boot.
6. **r9 amendment is an owner action** (rule 41); the AI does not edit RULES.md.
7. **The target is full integration (M5).** Staging is engineering order; the
   architecture is never substituted to reduce effort.

---

Copyright © 2026 Nexsteaduser. All Rights Reserved.