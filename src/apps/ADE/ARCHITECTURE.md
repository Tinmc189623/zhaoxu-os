# Aqua Desktop Environment 桌面架构

## 定位

`Aqua Desktop Environment` 是 Zhaoxu OS 的桌面环境，运行在 Ring3，同时承担显示服务器与 Desktop Window Manager
窗口管理器的职责。应用是独立 `.zxe` 进程，不打包进 Aqua Desktop Environment。

## 目录

```
src/apps/ADE/
  main.cpp        进程入口、ade_app 注册表、服务启动、事件循环
  wallpaper.cpp   壁纸（rule 33 图片，整数缩放；无数据盘时 Aqua 渐变回退）
  menubar.cpp     Aqua 菜单栏与 RTC 时钟
  dock.cpp        Dock 绘制、悬停与点击启动器
  icons.cpp       应用图标（内联 SVG，交给系统 SVG 光栅器）
  about.cpp       关于窗口（Aqua Desktop Environment 内部应用）
  winmgr/         Desktop Window Manager：焦点、z 序、拖动/关闭、输入路由、窗口装饰调用
  compositor/     Desktop Window Compositor：后缓冲、损伤提交、软件光标
  display/        命名端口服务、窗口协议、输入路由
  （Z1 计划新增）decoration.cpp  窗口装饰绘制，颜色/度量来自 ZAF Aqua 令牌
```

Desktop Window Compositor（`compositor/`）拥有后缓冲和从后缓冲到帧缓冲的提交路径：Desktop
Window Manager 决定每一层画什么，compositor 决定已完成的矩形何时上屏，并独立维护光标位置。
ZAF（`src/zaf/`）提供图形光栅与主题令牌，模块地图见 `src/zaf/README.md`。

光标形状来自 `/usr/share/cursors/Aqua` 的 PNG + `cursor.theme` 文本描述
（`compositor/cursor.c`，PNG 由 `stb_image` 解码）。Desktop Window Manager 每轮按命中区域
选择形状：菜单栏可点区域、Dock 图标和窗口关闭按钮用 `hand`；窗口内容交给应用的
`ade_app.cursor_at` 回调；客户端用 `ADE_OP_CURSOR`（`ade_cursor()`）登记内容坐标里的
光标区域，最后登记的优先。没有主题时回退到 Built-in 箭头。

## 边界

- Aqua Desktop Environment 不包含任何应用实现，只向外提供客户端 API 与端口协议。
- Desktop Window Manager 不直接写帧缓冲：像素经 ZAF painter（当前转到系统 `render_*` API），
  最终由 Desktop Window Compositor 提交。
- 应用通过端口命令流提交绘制命令；Aqua Desktop Environment 校验并执行。
- 输入事件由 Aqua Desktop Environment 路由到焦点窗口。
- 字体由系统 `libzos` 从 `/mnt` 加载（MiSans / MiSans Global / Inter），
  Aqua Desktop Environment 与应用都不自带字体。

## 窗口协议（概要）

客户端连接命名端口 `ade`，完成握手后：

- `CREATE_WINDOW`：标题、尺寸、图标、命令环共享内存。
- `COMMIT`：序号 + damage 矩形，通知 Aqua Desktop Environment 执行命令流。
- `CONFIGURE / DESTROY`：窗口状态与生命周期。
- `INPUT`：Aqua Desktop Environment → 客户端的按键、点击、焦点事件。
- `CLOSE`：Aqua Desktop Environment → 客户端关闭请求。
- `CURSOR`：客户端 → Aqua Desktop Environment 的光标区域提示，矩形用内容坐标，
  文本字段填主题里的光标名；`CLEAR` 会清空本帧登记的区域。

命令流使用定长命令（opcode + 参数），覆盖填充、圆角、渐变、文本、SVG、
裁剪与位图；Aqua Desktop Environment 用系统渲染接口在窗口表面执行，再合成到帧缓冲。

## 启动

内核完成设备与文件系统初始化后启动 `/bin/aqua`；Aqua Desktop Environment 读取
`/etc/ade/apps/*.desk` 构建 Dock 与启动器，按需 `spawn` 应用进程。
