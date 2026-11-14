# Wayland 合成器 + GBM + DRM/KMS 强制架构

状态：已定稿（2026-11-14）
适用范围：Desktop Window Compositor、Aqua Desktop Environment、Ring3 显示驱动、内核 IPC。

## 1. 合规基线

最终系统必须让 stock `libwayland` 客户端直接连上并工作，不是“类似 Wayland”的自定义协议。

- Wayland 1.x 核心接口：`wl_display`、`wl_registry`、`wl_callback`、`wl_compositor`、
  `wl_surface`、`wl_region`、`wl_shm`、`wl_shm_pool`、`wl_buffer`、`wl_seat`、
  `wl_pointer`、`wl_keyboard`、`wl_output`。
- `xdg-shell`（`xdg_wm_base`、`xdg_surface`、`xdg_toplevel`、`xdg_popup`）。
- 缓冲区：`wl_shm` 与 `linux-dmabuf-unstable-v1`；不接受自定义 buffer 接口作为唯一路径。
- 传输：`AF_UNIX` + `SOCK_STREAM`，socket 路径 `$XDG_RUNTIME_DIR/wayland-0`。
- 文件描述符：`SCM_RIGHTS` 随消息传递；`wl_shm_pool.create_buffer` 拿到的是真实 fd。
- 阻塞与多路复用：`poll`/`ppoll` 至少支持 socket 与 Wayland 内部事件源。
- 消息编码严格按 `wayland.xml`：`sender_id`、`opcode | size << 16`、参数按接口签名
  顺序打包，字符串与数组按 4 字节对齐，fd 走 ancillary data。
- 验收客户端：`weston-simple-shm`、`weston-terminal`、一个 `xdg-shell` 测试客户端。

## 2. 强制呈现管线

```
客户端 wl_surface.commit
        │
        ▼
合成器导入缓冲区（wl_shm / dma-buf）
        │
        ▼
GPU 渲染、合成到 GBM BO
        │
        ▼
KMS 原子提交（plane + framebuffer + crtc）
        │
        ▼
扫描输出，page flip 完成（vsync/fence）后回收旧 BO
```

软件光栅只能作为 GPU 不可用时的回退，且必须写入同一块 GBM BO，走同一条 KMS 提交路径；
不允许再把 `render_blit` 直接写 `fb_map` 作为桌面主路径。

## 3. 当前差距

| 层 | 现状 | 目标 |
| --- | --- | --- |
| 传输 | 命名端口 `sys_port_*`，消息式 | AF_UNIX 流 socket + SCM_RIGHTS |
| fd | 全局 VFS 文件表、端口句柄，无 per-process fd | 统一 fd 表，支持 socket、memfd、dma-buf |
| 缓冲 | `shm_create/map` 共享页，无 fd | memfd + dma-buf export/import |
| 显示 | 固件 GOP 线性帧缓冲，`fb_map` | KMS 服务：CRTC/connector/plane/framebuffer |
| 提交 | `render_blit` 到帧缓冲 | atomic commit + page flip + fence |
| GPU | virtio-gpu 仅 virgl `SUBMIT_3D` | 2D scanout + virgl 合成 + dmabuf |
| GBM | 无 | `gbm_device`/`gbm_bo`/`gbm_surface` |
| 桌面协议 | ADE 自定义端口命令流 | Wayland 服务端 + xdg-shell |

## 4. 分层与文件

### 4.1 内核

- `src/kernel/net/unix.c`：`AF_UNIX` 流 socket、监听、连接、背压、关闭。
- `src/kernel/ipc/fd.c`：统一 fd 表；socket/memfd/dma-buf/shm 句柄都进这张表。
- `src/kernel/ipc/scm.c`：`sendmsg`/`recvmsg` 与 `SCM_RIGHTS`。
- `src/kernel/fs/memfd.c`：匿名可 seek 的共享内存文件，支持多次 mmap。
- `src/kernel/ipc/dmabuf.c`：dma-buf 对象、attach、map、export fd。
- `src/kernel/ipc/poll.c`：`poll`/`ppoll`，等待队列与唤醒。
- 系统调用号追加在 `kernel/proc/syscall.c` 与 `user/userlib.h`，两边同步。

### 4.2 Ring3 显示服务（KMS）

- `src/apps/display/`：独立进程 `/bin/display`，内核启动时以可信身份 spawn，拥有
  0..4 GiB 恒等映射与 IOPL=3。
- `kms.c`：CRTC/connector/plane/framebuffer/dumb buffer 对象与原子提交状态机。
- `virtio_gpu.c`：2D resource、backing、set_scanout、transfer、flush、fence。
- `scanout.c`：双缓冲/三缓冲、flip 队列、完成回执。
- `protocol.h`：与合成器的端口消息（创建 fb、提交 flip、等待完成）。
- 现有 `/bin/drivers` 不再初始化 virtio-gpu，避免两个进程抢同一个控制队列。

### 4.3 GBM

- `src/user/lib/gbm/`：`gbm_create_device`、`gbm_device_get_fd`、`gbm_bo_create`、
  `gbm_bo_map`、`gbm_bo_get_stride`、`gbm_bo_get_fd`、`gbm_surface_*`。
- dumb 后端：memfd + KMS dumb buffer。
- gpu 后端：virgl resource + dmabuf；接口不变。

### 4.4 合成器与 Wayland 服务端

- `src/apps/ADE/wayland/`：对象表、`wl_display` 事件循环、资源生命周期。
- `interfaces/`：按 XML 生成的接口表（生成器用 Ruby，遵守规则 6）。
- `compositor.c`：surface 状态、damage、frame callback、subsurface。
- `xdg_shell.c`：toplevel/popup 状态机、configure/ack_configure。
- `seat.c`：pointer/keyboard 焦点、enter/leave、按键映射。
- `output.c`：`wl_output`、KMS connector 绑定。
- `renderer/`：软件光栅与 virgl 两条实现，输出到 GBM BO。
- `output_kms.c`：GBM surface → KMS atomic commit → flip 完成回收。

### 4.5 客户端

- `src/user/lib/wayland/`：Zhaoxu 的 `libwayland-client` 实现，API 与 stock 库一致，
  传输走 AF_UNIX + SCM_RIGHTS。
- 现有 welcome/calc/install 迁移为 Wayland 客户端，删除 `ADE_OP_*` 绘制命令流。

## 5. 里程碑与验收

| 里程碑 | 内容 | 验收 |
| --- | --- | --- |
| M0 | AF_UNIX、fd 表、SCM_RIGHTS、memfd、poll | 已验证。`kernel/ipc/zfd.c`、`unix_socket.c`、`memfd.c`、`proc/io_syscall.c`；`/data/NET_TEST.ZXE` 在 OVMF 下输出 `[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS`，退出码 0 |
| M1 | `/bin/display` KMS 服务 + virtio-gpu 2D + GBM dumb | 已验证：modern virtio-pci、memfd 扫描缓冲、双缓冲 flip；QEMU 1280x800 两帧不同（`out/m1e.png` / `out/m1f.png`） |
| M2 | Wayland 核心 + wl_shm + wl_compositor | 已验证到 KMS：`/bin/wayland` 提供 wl_display/registry/compositor/surface/wl_shm/wl_shm_pool/wl_buffer，`/bin/wldemo` 经 AF_UNIX + SCM_RIGHTS 提交 320x240 memfd；服务端把帧合成到 `/bin/display` 的 1280x800 扫描缓冲并翻页，`out/m2frame.png` 可见居中蓝块。stock `weston-simple-shm` 仍缺 libwayland/epoll/eventfd |
| M3 | xdg-shell + wl_seat | 已验证：xdg configure/ack、wl_seat/wl_pointer、wl_keyboard keymap/enter、服务端标题栏/关闭按钮/拖动、demo toplevel 合成；按键事件与 welcome/calc 迁移待续 |
| M4 | virgl GPU 合成 + linux-dmabuf | 进行中：modern virtio 已协商 VIRGL 并发 `CTX_CREATE`；QEMU `virtio-gpu-gl-pci` 实测 `ctrl 0x103 error 0x1203`，GPU 合成与 dmabuf 尚未完成 |
| M5 | stock 客户端与一致性回归 | `weston-terminal`、xdg-shell 测试套件全绿 |

依赖关系：M0 是 M2 的前提；M1 是 M2 呈现的前提；M4 可以并行于 M3。

### M0 验证记录（2026-11-14）

`M0_BOOT_TEST=1` 构建的内核通过 OVMF + vvfat ESP 启动，内核直接 spawn
`/data/NET_TEST.ZXE`；串口输出：

```
[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS
[m0 ] NET_TEST exit 0
```

验证过程中修掉一个已有的堆破坏：`kernel/fs/blockfs.c` 的 `read_block_raw()` 按
`stride` 分配 bounce buffer，但 `block_disk_read()` 总是传 `sectors * 512` 字节；
当逻辑块为 2352 字节（5 个 512 扇区）时越界 208 字节，踩坏内核堆块头并导致
后续 `kmalloc` 全部失败。现在按 `sectors * 512` 分配。

## 6. 风险与约束

- 固件 GOP 已经初始化显示控制器。KMS 接管前必须读取当前模式并做一次干净的
  modeset，不能假设设备处于复位状态。
- legacy virtio-pci 只有 I/O BAR，没有 MSI-X；fence 完成用控制队列 used ring
  轮询，不能假装有中断。
- 真实硬件需要 i915/amdgpu 级别的 KMS 驱动；首期只承诺 virtio-gpu，
  其他 GPU 走软件渲染回退。
- `SCM_RIGHTS` 与 dma-buf 是安全边界：fd 只能传给同一用户/会话，显示服务是
  唯一持有 scanout 能力的进程。
- 当前开发机缺 `xorriso`、`dosfstools`、`mtools`、`squashfs-tools`、PATH 中的
  QEMU，以及 `x86_64-elf-gcc`（Ada 驱动）。这些不补齐，M0/M1 无法做运行验证。

## 7. 非目标

XWayland、多 GPU、HDR、可变刷新率、分数缩放、远程桌面、无障碍树。
