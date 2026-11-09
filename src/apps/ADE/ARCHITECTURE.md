# ADE 桌面架构

## 定位

`ADE` 是 Zhaoxu OS 的桌面环境，运行在 Ring3，同时承担显示服务器与 DWM
窗口管理器的职责。应用是独立 `.zxe` 进程，不打包进 ADE。

## 目录

```
src/apps/ADE/
  main.cpp        进程入口、服务启动、事件循环
  shell/          壁纸、菜单栏、Dock、启动器
  winmgr/         DWM：焦点、z 序、装饰、拖动、合成
  display/        命名端口服务、窗口协议、输入路由
```

## 边界

- ADE 不包含任何应用实现，只向外提供客户端 API 与端口协议。
- DWM 只通过系统渲染接口合成，不直接写帧缓冲，不绘制应用页面。
- 应用通过共享内存命令环提交绘制命令；ADE 校验并执行。
- 输入事件由 ADE 路由到焦点窗口。
- 字体由系统 `libzos` 从 `/mnt` 加载（MiSans / MiSans Global / Inter），
  ADE 与应用都不自带字体。

## 窗口协议（概要）

客户端连接命名端口 `ade`，完成握手后：

- `CREATE_WINDOW`：标题、尺寸、图标、命令环共享内存。
- `COMMIT`：序号 + damage 矩形，通知 ADE 执行命令流。
- `CONFIGURE / DESTROY`：窗口状态与生命周期。
- `INPUT`：ADE → 客户端的按键、点击、焦点事件。
- `CLOSE`：ADE → 客户端关闭请求。

命令流使用定长命令（opcode + 参数），覆盖填充、圆角、渐变、文本、SVG、
裁剪与位图；ADE 用系统渲染接口在窗口表面执行，再合成到帧缓冲。

## 启动

内核完成设备与文件系统初始化后启动 `/bin/ade`；ADE 读取
`/etc/ade/apps/*.desk` 构建 Dock 与启动器，按需 `spawn` 应用进程。
