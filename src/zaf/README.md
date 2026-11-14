# src/zaf —— Zhaoxu Aqua Framework

ZAF 是 Aqua Desktop Environment 桌面与 Desktop Window Manager 窗口管理器下面的 GUI 框架。Z1 阶段它是链接进
`/bin/aqua` 的 Ring3 静态代码（不是内核、不是 `.zxe` 共享库）。

- 架构与里程碑：[docs/superpowers/specs/2026-11-13-zaf-architecture-design.md](../../docs/superpowers/specs/2026-11-13-zaf-architecture-design.md)
- Z1 设计：[docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md](../../docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md)
- Z1 计划：[docs/superpowers/plans/2026-11-14-zaf-z1-compositor.md](../../docs/superpowers/plans/2026-11-14-zaf-z1-compositor.md)

## 分层

```
apps/ADE           桌面外壳（壁纸/菜单栏/Dock/装饰）+ 显示服务器 + Desktop Window Manager 适配
        │
src/zaf/wm         窗口表、焦点、z 序、命中、拖动状态机（不画像素）
src/zaf/compositor 图层、损伤、后缓冲、damage-only 提交、软件光标
src/zaf/theme      Aqua 令牌（颜色/度量；Z3 扩成主题引擎）
src/zaf/gfx        painter：对 ZOS 渲染 API 的唯一封装
src/zaf/core       矩形与有界损伤区域（无系统依赖，可 host 测试）
        │
render.c           ZOS API 1.0 软件渲染（内核与 Ring3 同一份）
```

`zaf_gfx.h` / `zaf_gfx_soft.c`（Z0 的便携光栅与 fps demo）不在上面这条链里：
它们是 Skia/其他宿主后端的占位实现，Z1 不引用，也不扩张其职责。

## 目录与文件职责（规则 23）

| 文件 | 只做一件事 |
|---|---|
| `core/zaf_rect.h/.c` | 矩形数据与交/并/面积 |
| `core/zaf_damage.h/.c` | 有界损伤区域（最多 16 个矩形，2px 内合并，溢出取包围盒） |
| `gfx/zaf_painter.h/.c` | 把 `render_*` 封装成 `zaf_paint_*`；唯一直接调用 `render_*` 的 ZAF 文件 |
| `compositor/zaf_compositor.h/.c` | 图层表与一帧的绘制顺序 |
| `compositor/zaf_present.c` | 后缓冲 → 屏幕的 damage 提交 |
| `compositor/zaf_cursor.c` | 软件光标的保存、恢复、提交 |
| `wm/zaf_wm.h/.c` | 窗口表、焦点、z 序、命中、拖动（不绘制） |
| `theme/zaf_theme.h` | 主题令牌结构 |
| `theme/zaf_theme_aqua.c` | Aqua 默认令牌（数值必须与现有 `dwm.cpp` 常量一致） |

## 边界规则

1. `core/` 不 include 任何系统头（可被 host 测试直接编译）。
2. `wm/` 不 include `render.h`，不摸像素。
3. ZAF 新代码（含装饰）只通过 `zaf_paint_*` 画；`gfx/zaf_painter.c` 是唯一直接调
   `render_*` 的 ZAF 文件。现有 Aqua Desktop Environment 外壳（wallpaper/menubar/dock/icons）在 Z1 期间
   保留直接 `render_*` 调用，迁移到 painter 放在 Z4。
4. `compositor/` 不认识“窗口”“标题栏”“Dock”，只认识带包围盒和绘制回调的图层。
5. `src/apps/ADE/` 不 include `zaf/*/` 私有头之外的文件；ZAF 不反向 include
   `apps/ADE/`。
6. 一个文件只做一件事：新增能力先在这张表里加一行，再写代码。

## 构建接入

`src/Makefile` 的 `ZAF_C_SRCS` 把 ZAF 源文件加进 `ADE_C_SRCS`，用 `-I.` 让
`#include "zaf/core/zaf_rect.h"` 这类路径成立。`ZAF_STATS=1` 打开每帧损伤统计
（默认 0，不输出）。

## 状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| Z0 | `zaf_gfx` 后端中立光栅 + 软件实现 + fps demo | 已实现 |
| Z1 | 合成器 + 窗口管理 + Aqua 令牌 + 装饰拆分 | 设计完成，待实现（计划已就绪） |
| Z2 | 控件系统、事件分发、布局 | 未开始 |
| Z3 | 动画引擎、主题包 | 未开始 |
| Z4 | Aqua Desktop Environment/Desktop Window Manager 全面迁移、ZAF 作为 `.zxe` 共享库 | 未开始 |
