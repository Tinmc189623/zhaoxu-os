# Zhaoxu OS Architecture Baseline

## Fixed names (AGENTS.md rule 1)

- Kernel: `Yuanshuo`
- Operating system: `Zhaoxu OS`
- Shell: `8sh`
- Desktop: `Aqua Desktop Environment` (style: Aqua)
- Window manager: `Desktop Window Manager`

## End state: microkernel + Ring3 services

The kernel keeps only `arch`, memory management, thread/process scheduling, IPC, interrupt routing, and boot. Drivers and applications never enter the kernel binary (AGENTS.md rule 9).

```
Ring3 services                      Ring3 applications
  aqua     display server / Shell / Desktop Window Manager    /bin/apps/*.zxe
  drivers  storage / input / display / bus
      │                                    │
      └──────── named ports + shared memory ────────┘
                          │
                 ZOS API 1.0 system calls
                          │
Yuanshuo: address spaces, processes, IPC, IRQ/MMIO grants, framebuffer mapping
```

### Kernel primitives

- Per-process address space (CR3); the kernel high half is shared and inaccessible to user mode.
- Process = address space + kernel stack + one preemptible thread; `spawn/exit/wait`.
- Named ports: `port_create/register/connect/send/recv/close`; blocking and wakeup are integrated with the scheduler.
- Shared memory: `shm_create/map/unmap/close`, used for command rings and pixel surfaces.
- Device grants: MMIO region mapping, IRQs bound to ports, DMA buffer allocation, all for Ring3 drivers.
- Framebuffer: mapped to Aqua Desktop Environment, which has exclusive write access; the kernel no longer composites the desktop.

### Ring3 boundary

- `src/apps/ADE/` holds only the desktop environment: `wallpaper.cpp`/`menubar.cpp`/`dock.cpp` (desktop appearance),
  `winmgr/` (Desktop Window Manager), `display/` (port service, window protocol, input routing); Z1 plans to add
  `decoration.cpp` (window decorations).
- `src/zaf/` is Zhaoxu Aqua Framework: `core/` (rectangles and damage regions), `gfx/` (rendering wrapper),
  `compositor/` (layers, back buffer, damage present, software cursor), `wm/` (window table, focus, z-order,
  hit testing, dragging), `theme/` (Aqua tokens). ZAF runs in Ring3 and links into `/bin/aqua`; the kernel does
  not include ZAF. See `src/zaf/README.md` for the file responsibility table.
- `src/apps/drivers/` holds Ring3 driver services: AHCI, PS/2, mouse, RTC, serial; the kernel requests block I/O
  through the shared pages in `kernel/include/dev.h`, and logs and input use shared rings.
- `src/apps/csh/` is the Ring3 Shell: `main.c` (REPL), `builtins.c`, `files.c`,
  `run.c`, `install.c`, `io.c`; each file handles one class of command.
- Application clients live in `src/user/` (such as `welcome_client.c` and `calc_client.c`), build into standalone
  `.zxe` files, depend only on ZOS API and the client SDK, and do not include Aqua Desktop Environment internal headers.
- Fonts (MiSans, MiSans Global, Inter) belong to the system: `libzos` loads them from `/mnt`,
  and neither Aqua Desktop Environment nor applications bundle font files.
- Desktop Window Manager composites through the system rendering interface; it neither touches framebuffer pixels
  directly nor draws application pages.

## Phases

| Phase | Content | Acceptance | Status |
|---|---|---|---|
| P0 | Build fixes, bootable baseline | Clean build; QEMU boots straight into Aqua Desktop Environment | Done |
| P1 | Per-process address space, spawn/wait, named ports, shared memory | Two .zxe processes communicate; stable with SMP=4 | Done |
| P2 | Ring3 Aqua Desktop Environment display server, window protocol, input routing, system rendering library | One .zxe client window is interactive | Done |
| P3 | Application migration, `.desk` launchers, init/packaging | Dock launches all applications and reaps processes | In progress (Welcome/Calculator migrated) |
| P4 | Driver move-out: storage/input/display/bus services | Kernel binary contains no driver symbols | Not started |
| P5 | Desktop polish: panel, launcher, window effects | Aqua interaction and visual regression | Not started |

### P1 landed

- `vmm` address space object: the kernel high half is shared, the user window owns PML4 slot 2.
- Process = independent CR3 + kernel stack + preemptible thread; `spawn/exit/wait`, and the scheduler switches CR3 and TSS rsp0.
- Scheduling handoff uses an `on_cpu` flag; the old thread is released only after the next context finishes switching.
- Named ports: `listen/connect/accept/send/recv/close`, with a bidirectional queue per channel.
- Shared memory: `create/map/unmap/close`, mapping the same physical pages across processes.
- Font engine switched to the pristine `third_party/stb/stb_truetype.h` v1.26, loaded by the system from `/mnt`.

### P2 landed

- `/bin/aqua` starts as the Ring3 display server; the kernel `spawn`s it and then `wait`s, and prints a log and halts on failure (the kernel no longer contains an Aqua Desktop Environment fallback).
- Without a framebuffer the kernel starts Ring3 `/bin/8sh`.
- Syscalls expanded: `fb_map`, `mouse`, `key`, `sleep`, `cpus`, `rtc`, `read_path`,
  `port_try_accept`, `port_try_recv`, `readdir`, `disks`, `install`.
- `libzos` reuses `kernel/gfx/render.c`, `svg.c`, `font/ttf.c`, and stb_truetype to
  provide the same system rendering API in Ring3; fonts are read from `/mnt` into shared pages.
- Window protocol: `user/lib/include/ade_protocol.h`; clients send
  `CREATE/CLEAR/FILL/ROUND/TEXT/COMMIT` through the `ade` port, and Aqua Desktop Environment replays the command stream and composites it.
- Input routing: Aqua Desktop Environment sends key/click events back to the focused client; Desktop Window Manager keeps focus, z-order, dragging,
  and close.
- `user/welcome_client.c` + `user/lib/ade_client.c` form the first standalone `.zxe` client;
  clicking a button makes the client redraw and commit new damage.

### P3 landed (partial)

- Welcome and Calculator are standalone `.zxe` clients that submit command streams through the `ade` port;
  Calculator supports clicks and keyboard input (verified with `7 + 5 = 12`).
- The `TEXT` command supports `CENTER`/`RIGHT`/`SCALE2` flags; `CREATE` carries the client
  glyph and accent, and the Dock displays the client icon directly.
- Closing a window exits the client process; Aqua Desktop Environment detects the channel disconnect, unregisters the application, and forces a Dock redraw.
- Aqua Desktop Environment runtime logs go through the `serial_write` syscall instead of the kernel FB console.
- TODO: Files/Monitor clients, `.desk` launcher spawning on demand, removal of the automatic spawn.

## ZAF milestones (rule 32)

ZAF (Zhaoxu Aqua Framework) is the Ring3 GUI framework beneath Aqua Desktop Environment/Desktop Window Manager. See
`docs/superpowers/specs/2026-11-13-zaf-architecture-design.md` for the overall design.

| Stage | Content | Status |
|---|---|---|
| Z0 | `zaf_gfx` backend-neutral rasterizer + software implementation + Ring3 fps demo | Done |
| Z1 | Compositor (layers/damage/present/cursor) + window management + Aqua tokens + decoration split | Design final, implementation pending |
| Z2 | Widget system, event dispatch, layout | Not started |
| Z3 | Animation engine, theme packs, runtime switching | Not started |
| Z4 | Full Aqua Desktop Environment/Desktop Window Manager migration; ZAF shipped as a ZXE shared library | Not started |

Z1 design: `docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md`;
Z1 implementation plan: `docs/superpowers/plans/2026-11-14-zaf-z1-compositor.md`.

## Driver move-out constraints

- Drivers run as Ring3 services, expose requests through ports, and are not linked into the kernel.
- The kernel provides only MMIO mapping, IRQ forwarding, DMA buffers, and port primitives.
- Block, input, and display devices complete migration in P4; kernel paths remain as a fallback during migration.
