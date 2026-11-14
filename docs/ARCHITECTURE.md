# Zhaoxu OS 架构基线

## 固定命名（AGENTS.md 规则 1）

- 内核：`Yuanshuo`
- 操作系统：`Zhaoxu OS`
- Shell：`8sh`
- 桌面：`Aqua Desktop Environment`（风格：Aqua）
- 窗口管理器：`Desktop Window Manager`

## 终态：微内核 + Ring3 服务

内核只保留 `arch`、内存管理、线程/进程调度、IPC、中断路由和引导。驱动与
应用都不进入内核二进制（AGENTS.md 规则 9）。

```
Ring3 服务                          Ring3 应用
  aqua     显示服务器 / Shell / Desktop Window Manager    /bin/apps/*.zxe
  drivers  存储 / 输入 / 显示 / 总线
      │                                    │
      └──────── 命名端口 + 共享内存 ────────┘
                          │
                 ZOS API 1.0 系统调用
                          │
Yuanshuo：地址空间、进程、IPC、IRQ/MMIO 授权、帧缓冲映射
```

### 内核原语

- 每进程独立地址空间（CR3），内核高半区共享且用户不可访问。
- 进程 = 地址空间 + 内核栈 + 一个可抢占线程；`spawn/exit/wait`。
- 命名端口：`port_create/register/connect/send/recv/close`，阻塞唤醒接入调度器。
- 共享内存：`shm_create/map/unmap/close`，用于命令环与像素表面。
- 设备授权：MMIO 区映射、IRQ 绑定到端口、DMA 缓冲分配，供 Ring3 驱动使用。
- 帧缓冲：映射给 Aqua Desktop Environment，Aqua Desktop Environment 独占写；内核不再合成桌面。

### Ring3 边界

- `src/apps/ADE/` 只放桌面环境：`wallpaper.cpp`/`menubar.cpp`/`dock.cpp`（桌面外观）、
  `winmgr/`（Desktop Window Manager）、`display/`（端口服务、窗口协议、输入路由）；Z1 计划新增
  `decoration.cpp`（窗口装饰）。
- `src/zaf/` 是 Zhaoxu Aqua Framework：`core/`（矩形与损伤区域）、`gfx/`（渲染封装）、
  `compositor/`（图层、后缓冲、damage 提交、软件光标）、`wm/`（窗口表、焦点、z 序、
  命中、拖动）、`theme/`（Aqua 令牌）。ZAF 运行在 Ring3，链接进 `/bin/aqua`；内核不含
  ZAF。文件职责表见 `src/zaf/README.md`。
- `src/apps/drivers/` 是 Ring3 驱动服务：AHCI、PS/2、鼠标、RTC、串口；内核通过 `kernel/include/dev.h` 的共享页请求块 I/O，日志与输入走共享环。
- `src/apps/csh/` 是 Ring3 Shell：`main.c`（REPL）、`builtins.c`、`files.c`、
  `run.c`、`install.c`、`io.c`，一个文件只负责一类命令。
- 应用客户端在 `src/user/`（如 `welcome_client.c`、`calc_client.c`），编译为独立
  `.zxe`，只依赖 ZOS API 与客户端 SDK，不 include Aqua Desktop Environment 内部头文件。
- 字体（MiSans、MiSans Global、Inter）属于系统：由 `libzos` 从 `/mnt`
  加载，Aqua Desktop Environment 与应用都不打包字体文件。
- Desktop Window Manager 通过系统渲染接口合成，不直接操作帧缓冲像素，也不绘制应用页面。

## 分阶段

| 期 | 内容 | 验收 | 状态 |
|---|---|---|---|
| P0 | 构建修复、可启动基线 | 干净构建；QEMU 直入 Aqua Desktop Environment | 完成 |
| P1 | 每进程地址空间、spawn/wait、命名端口、共享内存 | 两个 .zxe 进程通信；SMP=4 稳定 | 完成 |
| P2 | Ring3 Aqua Desktop Environment 显示服务器、窗口协议、输入路由、系统渲染库 | 一个 .zxe 客户端窗口可交互 | 完成 |
| P3 | 应用迁移、`.desk` 启动器、init/打包 | Dock 启动全部应用并回收进程 | 进行中（Welcome/Calculator 已迁移） |
| P4 | 驱动外移：存储/输入/显示/总线服务 | 内核二进制不含驱动符号 | 未开始 |
| P5 | 桌面完善：面板、启动器、窗口特效 | Aqua 交互与视觉回归 | 未开始 |

### P1 已落地

- `vmm` 地址空间对象：内核高半区共享，用户窗口独占 PML4 slot 2。
- 进程 = 独立 CR3 + 内核栈 + 抢占线程；`spawn/exit/wait`，调度器切换 CR3 与 TSS rsp0。
- 调度交接用 `on_cpu` 标记，只有下一个上下文完成切换后才释放旧线程。
- 命名端口：`listen/connect/accept/send/recv/close`，每通道双向队列。
- 共享内存：`create/map/unmap/close`，跨进程映射同一物理页。
- 字体引擎改用 `third_party/stb/stb_truetype.h` v1.26 原文，系统从 `/mnt` 加载。

### P2 已落地

- `/bin/aqua` 作为 Ring3 显示服务器启动；内核 `spawn` 后 `wait`，失败则打印日志并停机（内核不再包含 Aqua Desktop Environment 回退）。
- 无帧缓冲时内核启动 Ring3 `/bin/8sh`。
- 系统调用扩充：`fb_map`、`mouse`、`key`、`sleep`、`cpus`、`rtc`、`read_path`、
  `port_try_accept`、`port_try_recv`、`readdir`、`disks`、`install`。
- `libzos` 复用 `kernel/gfx/render.c`、`svg.c`、`font/ttf.c` 与 stb_truetype，
  在 Ring3 提供同一套系统渲染 API；字体从 `/mnt` 读入共享页。
- 窗口协议：`user/lib/include/ade_protocol.h`，客户端通过 `ade` 端口发送
  `CREATE/CLEAR/FILL/ROUND/TEXT/COMMIT`，Aqua Desktop Environment 用命令流重放并合成。
- 输入路由：Aqua Desktop Environment 把按键/点击事件回送焦点客户端；Desktop Window Manager 保留焦点、z 序、拖动、
  关闭。
- `user/welcome_client.c` + `user/lib/ade_client.c` 是首个独立 `.zxe` 客户端；
  点击按钮后客户端重绘并提交新的 damage。

### P3 已落地（部分）

- Welcome 与 Calculator 都是独立 `.zxe` 客户端，经 `ade` 端口提交命令流；
  Calculator 支持点击与键盘输入（已验证 `7 + 5 = 12`）。
- `TEXT` 命令支持 `CENTER`/`RIGHT`/`SCALE2` 标志；`CREATE` 携带客户端
  glyph 与 accent，Dock 直接显示客户端图标。
- 关闭窗口后客户端进程退出，Aqua Desktop Environment 检测通道断开、注销应用并强制重绘 Dock。
- Aqua Desktop Environment 运行期日志走 `serial_write` 系统调用，不再写入内核 FB 控制台。
- 待办：Files/Monitor 客户端、`.desk` 启动器按需 spawn、自动 spawn 移除。

## ZAF 里程碑（规则 32）

ZAF（Zhaoxu Aqua Framework）是 Aqua Desktop Environment/Desktop Window Manager 之下的 Ring3 GUI 框架。总设计见
`docs/superpowers/specs/2026-11-13-zaf-architecture-design.md`。

| 阶段 | 内容 | 状态 |
|---|---|---|
| Z0 | `zaf_gfx` 后端中立光栅 + 软件实现 + Ring3 fps demo | 完成 |
| Z1 | 合成器（图层/损伤/提交/光标）+ 窗口管理 + Aqua 令牌 + 装饰拆分 | 设计定稿，待实现 |
| Z2 | 控件系统、事件分发、布局 | 未开始 |
| Z3 | 动画引擎、主题包与运行时切换 | 未开始 |
| Z4 | Aqua Desktop Environment/Desktop Window Manager 全面迁移；ZAF 作为 ZXE 共享库发布 | 未开始 |

Z1 设计：`docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md`；
Z1 实施计划：`docs/superpowers/plans/2026-11-14-zaf-z1-compositor.md`。

## 驱动外移约束

- 驱动以 Ring3 服务运行，通过端口暴露请求，不链接进内核。
- 内核只提供 MMIO 映射、IRQ 转发、DMA 缓冲与端口原语。
- 块设备、输入、显示在 P4 完成迁移；迁移期间保留内核路径作为回退。
