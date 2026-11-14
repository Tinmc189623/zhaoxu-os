# Wayland Compositor + GBM + DRM/KMS Mandatory Architecture

Status: Final (2026-11-14)
Scope: Desktop Window Compositor, Aqua Desktop Environment, Ring3 display driver, kernel IPC.

## 1. Compliance baseline

The final system must let stock `libwayland` clients connect and work directly — not a "Wayland-like" custom protocol.

- Wayland 1.x core interfaces: `wl_display`, `wl_registry`, `wl_callback`, `wl_compositor`,
  `wl_surface`, `wl_region`, `wl_shm`, `wl_shm_pool`, `wl_buffer`, `wl_seat`,
  `wl_pointer`, `wl_keyboard`, `wl_output`.
- `xdg-shell` (`xdg_wm_base`, `xdg_surface`, `xdg_toplevel`, `xdg_popup`).
- Buffers: `wl_shm` and `linux-dmabuf-unstable-v1`; a custom buffer interface is not accepted as the only path.
- Transport: `AF_UNIX` + `SOCK_STREAM`, socket path `$XDG_RUNTIME_DIR/wayland-0`.
- File descriptors: `SCM_RIGHTS` travels with messages; `wl_shm_pool.create_buffer` receives a real fd.
- Blocking and multiplexing: `poll`/`ppoll` must support at least sockets and Wayland's internal event sources.
- Message encoding follows `wayland.xml` strictly: `sender_id`, `opcode | size << 16`, arguments packed in
  interface signature order, strings and arrays 4-byte aligned, fds carried in ancillary data.
- Acceptance clients: `weston-simple-shm`, `weston-terminal`, one `xdg-shell` test client.

## 2. Mandatory presentation pipeline

```
Client wl_surface.commit
        │
        ▼
Compositor imports the buffer (wl_shm / dma-buf)
        │
        ▼
GPU renders and composites into a GBM BO
        │
        ▼
KMS atomic commit (plane + framebuffer + crtc)
        │
        ▼
Scan out; recycle the old BO once the page flip completes (vsync/fence)
```

Software rasterization is allowed only as a fallback when the GPU is unavailable, and it must write into the same
GBM BO and take the same KMS commit path; `render_blit` writing straight into `fb_map` is no longer permitted as the
desktop's main path.

## 3. Current gaps

| Layer | Current state | Target |
| --- | --- | --- |
| Transport | Named ports `sys_port_*`, message-based | AF_UNIX stream sockets + SCM_RIGHTS |
| fd | Global VFS file table, port handles, no per-process fd | Unified fd table supporting socket, memfd, dma-buf |
| Buffer | `shm_create/map` shared pages, no fd | memfd + dma-buf export/import |
| Display | Firmware GOP linear framebuffer, `fb_map` | KMS service: CRTC/connector/plane/framebuffer |
| Present | `render_blit` into the framebuffer | atomic commit + page flip + fence |
| GPU | virtio-gpu virgl `SUBMIT_3D` only | 2D scanout + virgl compositing + dmabuf |
| GBM | None | `gbm_device`/`gbm_bo`/`gbm_surface` |
| Desktop protocol | ADE custom port command stream | Wayland server + xdg-shell |

## 4. Layering and files

### 4.1 Kernel

- `src/kernel/net/unix.c`: `AF_UNIX` stream sockets, listen, connect, backpressure, close.
- `src/kernel/ipc/fd.c`: unified fd table; socket/memfd/dma-buf/shm handles all live in this table.
- `src/kernel/ipc/scm.c`: `sendmsg`/`recvmsg` with `SCM_RIGHTS`.
- `src/kernel/fs/memfd.c`: anonymous seekable shared-memory files, mmap-able more than once.
- `src/kernel/ipc/dmabuf.c`: dma-buf objects, attach, map, export fd.
- `src/kernel/ipc/poll.c`: `poll`/`ppoll`, wait queues and wakeups.
- Syscall numbers are appended in `kernel/proc/syscall.c` and `user/userlib.h`, kept in sync on both sides.

### 4.2 Ring3 display service (KMS)

- `src/apps/display/`: a standalone process `/bin/display`, spawned with trusted identity at kernel boot, owning
  the 0..4 GiB identity mapping and IOPL=3.
- `kms.c`: CRTC/connector/plane/framebuffer/dumb buffer objects and the atomic commit state machine.
- `virtio_gpu.c`: 2D resource, backing, set_scanout, transfer, flush, fence.
- `scanout.c`: double/triple buffering, flip queue, completion acknowledgements.
- `protocol.h`: port messages with the compositor (create fb, submit flip, wait for completion).
- The existing `/bin/drivers` no longer initializes virtio-gpu, so two processes never fight over the same control queue.

### 4.3 GBM

- `src/user/lib/gbm/`: `gbm_create_device`, `gbm_device_get_fd`, `gbm_bo_create`,
  `gbm_bo_map`, `gbm_bo_get_stride`, `gbm_bo_get_fd`, `gbm_surface_*`.
- dumb backend: memfd + KMS dumb buffer.
- gpu backend: virgl resource + dmabuf; the interface is unchanged.

### 4.4 Compositor and Wayland server

- `src/apps/ADE/wayland/`: object table, `wl_display` event loop, resource lifetimes.
- `interfaces/`: interface tables generated from the XML (the generator is Ruby, per rule 6).
- `compositor.c`: surface state, damage, frame callbacks, subsurfaces.
- `xdg_shell.c`: toplevel/popup state machine, configure/ack_configure.
- `seat.c`: pointer/keyboard focus, enter/leave, keymap.
- `output.c`: `wl_output`, KMS connector binding.
- `renderer/`: software raster and virgl implementations, both writing into a GBM BO.
- `output_kms.c`: GBM surface → KMS atomic commit → recycle on flip completion.

### 4.5 Clients

- `src/user/lib/wayland/`: Zhaoxu's `libwayland-client` implementation, API-identical to the stock library,
  with AF_UNIX + SCM_RIGHTS transport.
- The existing welcome/calc/install migrate to Wayland clients, and the `ADE_OP_*` drawing command stream is deleted.

## 5. Milestones and acceptance

| Milestone | Content | Acceptance |
| --- | --- | --- |
| M0 | AF_UNIX, fd table, SCM_RIGHTS, memfd, poll | Validated. `kernel/ipc/zfd.c`, `unix_socket.c`, `memfd.c`, `proc/io_syscall.c`; under OVMF `/data/NET_TEST.ZXE` prints `[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS` and exits with code 0 |
| M1 | `/bin/display` KMS service + virtio-gpu 2D + GBM dumb | Validated: modern virtio-pci, memfd scanout buffer, double-buffered flip; two QEMU frames at 1280x800 differ (`out/m1e.png` / `out/m1f.png`) |
| M2 | Wayland core + wl_shm + wl_compositor | Validated through to KMS: `/bin/wayland` provides wl_display/registry/compositor/surface/wl_shm/wl_shm_pool/wl_buffer, and `/bin/wldemo` submits a 320x240 memfd over AF_UNIX + SCM_RIGHTS; the server composites the frame into `/bin/display`'s 1280x800 scanout buffer and flips it, with the centered blue block visible in `out/m2frame.png`. Stock `weston-simple-shm` still needs libwayland/epoll/eventfd |
| M3 | xdg-shell + wl_seat | Validated: xdg configure/ack, wl_seat/wl_pointer, wl_keyboard keymap/enter, server-side title bar/close button/dragging, demo toplevel compositing; key events and the welcome/calc migration are pending |
| M4 | virgl GPU compositing + linux-dmabuf | In progress: modern virtio control queues accept 256/64 per the device values, `CTX_CREATE`, 3D resource creation (72-byte command including padding) and `CTX_ATTACH_RESOURCE` succeed on `virtio-gpu-gl-pci`; virglrenderer under Windows QEMU registers neither 2D nor 3D resources, `SET_SCANOUT` returns `0x1203`, and GPU compositing plus dmabuf are unfinished |
| M5 | Stock clients and consistency regression | Kernel prerequisites validated: `eventfd`, `timerfd`, and `epoll` (syscalls 61–67) with per-descriptor object types, and `/data/M5_TEST.ZXE` prints `[m5] eventfd, timerfd, epoll PASS` plus `[m5 ] M5_TEST exit 0` under OVMF. The stock libwayland 1.23.1 client core is now ported (see M6); unmodified stock applications and the xdg-shell test suite are still pending |
| M6 | Wayland client stack and migration | In progress: upstream `wayland-client.c`, `wayland-util.c`, `connection.c`, generated core/xdg-shell bindings and the libffi 3.4.6 SysV `ffi_call` path build for ZOS; `user/lib/wayland/compat.c` supplies libc/pthread/socket/errno; `/data/WLSTOCK.ZXE` binds `wl_compositor`/`wl_shm`, commits a 320x240 shm frame through SCM_RIGHTS and exits 0. Remaining: unmodified stock clients (`weston-simple-shm`, `weston-terminal`), `shm_open`/wider POSIX surface, welcome/calc migration and the xdg-shell conformance suite |

Dependencies: M0 is a prerequisite for M2; M1 is a prerequisite for M2 presentation; M4 can run in parallel with M3.

### M0 verification record (2026-11-14)

A kernel built with `M0_BOOT_TEST=1` booted through OVMF + a vvfat ESP and spawned
`/data/NET_TEST.ZXE` directly; serial output:

```
[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS
[m0 ] NET_TEST exit 0
```

The verification run fixed a pre-existing heap corruption: `read_block_raw()` in
`kernel/fs/blockfs.c` sized its bounce buffer from `stride`, but `block_disk_read()` always
passes `sectors * 512` bytes; with a 2352-byte logical block (5 × 512 sectors) that ran 208
bytes past the end, trampled the kernel heap chunk header, and made every later `kmalloc`
fail. It now allocates `sectors * 512`.

### M5 verification record (2026-11-14)

A kernel built with `M5_BOOT_TEST=1` booted through OVMF and spawned
`/data/M5_TEST.ZXE` directly; serial output:

```
[m5 ] running /data/M5_TEST.ZXE
[m5] eventfd PASS
[m5] epoll PASS
[m5] timerfd PASS
[m5] eventfd, timerfd, epoll PASS
[m5 ] M5_TEST exit 0
```

The test covers the Linux-compatible event ABI: `eventfd` counter reads and writes,
`EFD_NONBLOCK` returning `-1` on an empty counter, `timerfd` one-shot expiration,
`epoll_ctl` registration, level-triggered `epoll_wait`, and a timeout with no ready
descriptors. Descriptors are stored in the existing per-process zfd table above
`ZFD_BASE`; `epoll` entries are fixed at 32 per object, and the wait path sleeps in
1 ms slices like the existing `poll` syscall.

### M6 stock libwayland verification record (2026-11-14)

A kernel built with `WLSTOCK_BOOT_TEST=1` starts `/bin/wayland`, then spawns
`/data/WLSTOCK.ZXE`. The client is linked from upstream libwayland 1.23.1 and
prints:

```
[wlstock] ffi 42
[wlstock] connected
[wlstock] registry requested
[wlstock] globals bound
[wlstock] frame committed
[wlstock] WLSTOCK exit 0
```

The server logs `[wayland] commit 320x240 stride=1280 first=0x0x00000000ff3366cc`.
The verification uncovered two integration defects: the client's `recvmsg`
carries `MSG_DONTWAIT` while the descriptor itself is blocking, so the kernel now
derives nonblocking behavior from the flag per call; and the freestanding build
must not put `kernel/include` on the include path for the stock sources because
`kernel/include/poll.h` would shadow the libc poll definitions.

## 6. Risks and constraints

- Firmware GOP has already initialized the display controller. Before KMS takes over it must
  read the current mode and perform one clean modeset; the device cannot be assumed to be in
  a reset state.
- Legacy virtio-pci has only an I/O BAR and no MSI-X; fence completion polls the control
  queue's used ring instead of pretending an interrupt exists.
- Real hardware needs i915/amdgpu-class KMS drivers; the first phase commits only to
  virtio-gpu, and other GPUs go through the software rendering fallback.
- `SCM_RIGHTS` and dma-buf are security boundaries: fds may only be passed within the same
  user/session, and the display service is the only process holding scanout capability.
- The current development machine lacks `xorriso`, `dosfstools`, `mtools`, `squashfs-tools`,
  QEMU on PATH, and `x86_64-elf-gcc` (for the Ada drivers). Until those are supplied, M0/M1
  cannot be run-verified.

## 7. Non-goals

XWayland, multiple GPUs, HDR, variable refresh rate, fractional scaling, remote desktop, accessibility tree.
