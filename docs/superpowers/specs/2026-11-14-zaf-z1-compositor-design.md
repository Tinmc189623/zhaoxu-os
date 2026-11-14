# ZAF Z1：合成器与窗口管理设计

日期：2026-11-14
状态：设计定稿；实现待工具链（当前机器没有 WSL2，见第 10 节）
关联：
- 上位设计：[2026-11-13-zaf-architecture-design.md](2026-11-13-zaf-architecture-design.md)
- 实施计划：[2026-11-14-zaf-z1-compositor.md](../plans/2026-11-14-zaf-z1-compositor.md)
- 现有实现：`src/apps/ADE/winmgr/dwm.cpp`、`src/kernel/gfx/render.c`、`src/user/lib/include/zaf_gfx.h`

## 1. 目标与验收

Z1 要把 ADE 里已经验证过的窗口/合成行为拆到 ZAF 的分层里，同时**不改桌面观感、
不改窗口协议、不改内核**。验收面：

1. 现有桌面行为逐项等价：壁纸、菜单栏、Dock、窗口层叠/焦点/拖动/关闭、软件
   光标、两个客户端窗口（Welcome、Calculator）都可交互。
2. 损伤只提交变化区域：光标移动只提交光标包围盒（约 19×20 的两块），拖动窗口
   只提交新旧窗口包围盒的并集；`ZAF_STATS=1` 时串口可打印每帧损伤数与像素数。
3. 内核与引导层二进制不变：`git diff --stat src/kernel src/boot` 为空，
   `nm -u out/kernel.elf` 仍无未定义符号。
4. 为 Z2（控件系统）与 Z3（动画/主题）留下稳定接口：控件只依赖 `zaf_painter`
   与主题令牌，不依赖 ADE 内部头文件。

## 2. 现状（Z0 交付后的实际代码）

| 位置 | 现在承担的职责 | 行数 |
|---|---|---|
| `src/apps/ADE/main.cpp` | 进程入口、`ade_app` 注册表、渲染 API 转发、启动 ADE | 234 |
| `src/apps/ADE/winmgr/dwm.cpp` | 损伤列表、后缓冲、壁纸/菜单栏/Dock/窗口合成、光标、命中、z 序、焦点、拖动、关闭、输入、主循环 | 627 |
| `src/apps/ADE/display/server.cpp` | `ade` 端口、客户端槽位、命令流重放、事件回送 | 323 |
| `src/apps/ADE/wallpaper.cpp` | 壁纸（rule 33 图片，整数缩放）与 Aqua 渐变回退 | 67 |
| `src/apps/ADE/menubar.cpp` / `dock.cpp` | 桌面外壳绘制与命中 | 234 |
| `src/kernel/gfx/render.c` | ZOS API 1.0 软件渲染：目标面、裁剪、图元、文字、SVG、光标 | 608 |
| `src/user/lib/include/zaf_gfx.h` + `zaf_gfx_soft.c` | Z0 的后端中立光栅接口与软件实现（矩形/圆角/线/多边形/alpha） | 46 + 205 |

问题不在“功能缺失”，而在职责重叠：DWM 一个文件里同时有**合成器**（损伤、后缓
冲、提交、光标）、**窗口策略**（z 序、焦点、命中、拖动）、**窗口装饰绘制**
（圆角、标题栏、红绿灯、阴影）和**桌面外壳调度**（壁纸、菜单栏、Dock）。
Z1 按上位设计把它们拆开。

## 3. 设计决策

### D1：Z1 是 Ring3 静态库，不进内核、不加系统调用

ZAF Z1 的代码全部在 `src/zaf/`，以普通对象文件链接进 `/bin/ade`，和现有
`main.cpp`/`dwm.cpp` 同一个进程。Z1 不引入新的 `.zxe` 动态库、不新增系统调用、
不改 `kernel/` 与 `boot/`。ZAF 作为共享库发布是 Z4 的事，届时再按 ZXE 动态
ABI 版本化，避免现在为了“库形态”承担无法验证的链接风险。

### D2：合成后端暂时就是 ZOS 渲染 API（不引入 Skia）

上位设计把 Skia 定为最终渲染基础，同时写明退路：渲染接口不变、先自研 Raster
后端。Z0 已经按退路交付了 `zaf_gfx`（软件光栅）+ `zaf_gfx_demo`（帧率冒烟）。
Z1 不再扩张 `zaf_gfx` 的职责，而是：

- **合成与桌面绘制走现有 `render.c`**（ZOS API 1.0）。它是 ADE、DWM 与应用今天
  唯一在用的绘制接口，内核与 Ring3 共享同一份实现，文字/阴影/SVG/光标都已验证。
- `zaf_gfx` 保留为**便携光栅**与 Z0 demo 的后端；等 Skia 适配真正立项（单独
  spike，需要用户决策）时，再决定是替换 `render.c` 还是让 ZAF 同时拥有两种后端。
- Z1 的唯一要求是：ZAF 内部把对 `render_*` 的调用收敛到 `zaf/gfx/` 的薄封装
  （`zaf_painter`），未来换后端时上层不需要改。**但 Z1 不实现多后端 vtable**：
  只有一个后端时加虚表是纯开销（规则 20 KISS），封装先做成普通函数集合。
- 现有 ADE 外壳（`wallpaper.cpp`/`menubar.cpp`/`dock.cpp`/`icons.cpp`/`about.cpp`）
  在 Z1 期间仍可直接调 `render_*`（它们已经稳定、且不是 ZAF 代码）；ZAF 新代码与
  装饰模块必须走 `zaf_paint_*`。外壳迁移到 painter 属于 Z4 的机械工作。

代价：Z1 的 ZAF 仍依赖 ZOS 渲染 API，不能立即移植到别的宿主。可接受，因为上位
设计已把“可移植”列在接口隔离而非 Z1 交付物上。

### D3：合成器管图层/损伤/提交/光标；DWM 管窗口策略

- `zaf/compositor`：屏幕/后缓冲、有界损伤区域、按 `order` 合成图层、damage-only
  提交、软件光标覆盖。它**不认识**“窗口”“标题栏”“Dock”，只知道“带包围盒和
  绘制回调的图层”。
- `zaf/wm`：窗口表（最多 8 个）、焦点、z 序、命中测试、拖动状态机。它**不绘制**
  任何像素，只回答“哪个窗口在上、该拖到哪、点到谁”。
- `dwm.cpp`：保留 DWM 产品名，变成薄适配层——把 ADE 的 `ade_app` 和 ZAF 的
  window/layer 对上，转接输入与定时器，窗口装饰绘制委托给 `ade_decoration_*`。
- 桌面外壳（壁纸、菜单栏、Dock）仍是 ADE 的 `shell` 职责，以图层形式注册给合成
  器，合成器只负责先后顺序。

这与 AGENTS.md“DWM 独占窗口装饰、z 序、拖动/关闭、焦点、损伤合成与输入”的
现有描述有一处需要同步：**损伤合成归 ZAF 合成器，DWM 归策略与输入**。Z1 完成
时更新 AGENTS.md 该条与 `docs/ARCHITECTURE.md`。

### D4：窗口装饰独立成文件，颜色/尺寸来自 Aqua 令牌

`dwm.cpp` 里的圆角 12/11、标题渐变 `0xFDFEFF → 0xC9DBEC`、红绿灯三色、阴影
spread 9、标题高 32 等魔法数搬到 `zaf/theme/zaf_theme_aqua.c`，装饰绘制搬到
`apps/ADE/decoration.cpp`。Z1 的令牌表是**编译期常量**，只收录窗口装饰需要的
字段；完整主题引擎（主题包加载、运行时切换）是 Z3，不在 Z1 里做半成品。

### D5：损伤区域沿用现有有界算法，不换区域树

现有算法（`dwm.cpp:58` 起）：最多 16 个矩形；新矩形与已有矩形相邻 2px 内就合并；
溢出时把第 1 个矩形扩成总包围盒。Z1 原样搬到 `zaf/core/zaf_damage.c`，只加单元
测试与不变量注释。理由：算法简单、行为已被实机验证；区域树/精确布尔运算是 Z3
动画合成才需要的优化点（规则 20）。

### D6：图层上限与窗口上限保持现有规模

`ZAF_MAX_LAYERS = 16`（背景 1 + 菜单栏 1 + Dock 1 + 窗口 8 + 余量 5），
`ZAF_MAX_WINDOWS = 8`（等于现有 `DWM_MAX_WINDOWS`）。不提前扩容。

## 4. 分层与文件职责（规则 23：一个文件只做一件事）

```
src/zaf/
├── README.md                     模块地图、边界与构建接入
├── core/
│   ├── zaf_rect.h                纯数据矩形 + 内联几何（无依赖，可 host 测试）
│   ├── zaf_rect.c                矩形交/并/空/裁剪
│   ├── zaf_damage.h              有界损伤区域类型与不变量
│   └── zaf_damage.c              合并/溢出/遍历
├── gfx/
│   ├── zaf_painter.h             桌面绘制的 ZOS 封装（fill/blend/round/grad/
│   │                             shadow/blit/text/text_width/cursor）
│   └── zaf_painter.c             上述封装的唯一实现（调用 render_*）
├── compositor/
│   ├── zaf_compositor.h          合成器对外接口：初始化、图层、损伤、一帧
│   ├── zaf_compositor.c          图层表 + 一帧的绘制顺序
│   ├── zaf_present.c             后缓冲 → 屏幕的 damage-only 提交
│   └── zaf_cursor.c              软件光标的保存/恢复/提交（复用 render_cursor*）
├── wm/
│   ├── zaf_wm.h                  窗口表、焦点、z 序、命中、拖动
│   └── zaf_wm.c                  上述状态的唯一实现
└── theme/
    ├── zaf_theme.h               Aqua 令牌结构（颜色/度量/动画时长预留）
    └── zaf_theme_aqua.c          Z1 使用的 Aqua 默认值
```

ADE 侧：

```
src/apps/ADE/
├── main.cpp                      入口 + ade_app 注册表 + 渲染转发（保留）
├── decoration.cpp/.h             窗口装饰绘制（新，使用 Aqua 令牌）
├── wallpaper.cpp / menubar.cpp / dock.cpp / icons.cpp / about.cpp（保留）
├── winmgr/dwm.cpp                薄适配层：ZAF WM/合成器 ↔ ADE 外壳与输入
├── winmgr/dwm.h                  对外 DWM API（基本不变）
└── display/server.cpp            窗口协议与命令流（不变）
```

`src/zaf/README.md` 记录每个文件的“只做一件事”声明，后续任务新增文件前先看
地图，避免再出现“一个文件同时管合成和窗口策略”。

## 5. 接口

以下头文件是 Z1 的契约。实现放到计划里的 Task 1–4，签名以本节为准。

### 5.1 `zaf/core/zaf_rect.h`

纯数据、无系统头依赖，便于 host 单元测试与未来移植：

```c
#ifndef ZAF_RECT_H
#define ZAF_RECT_H

struct zaf_rect { int x0, y0, x1, y1; };

static inline struct zaf_rect zaf_rect_make(int x, int y, int w, int h)
{
    struct zaf_rect r = { x, y, x + w, y + h };
    return r;
}
static inline int zaf_rect_empty(struct zaf_rect r)
{
    return r.x0 >= r.x1 || r.y0 >= r.y1;
}
static inline int zaf_rect_intersects(struct zaf_rect a, struct zaf_rect b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}
struct zaf_rect zaf_rect_intersect(struct zaf_rect a, struct zaf_rect b);
struct zaf_rect zaf_rect_union(struct zaf_rect a, struct zaf_rect b);
int zaf_rect_width(struct zaf_rect r);   /* x1 - x0，空矩形为 0 */
int zaf_rect_height(struct zaf_rect r);
int zaf_rect_area(struct zaf_rect r);

#endif
```

### 5.2 `zaf/core/zaf_damage.h`

```c
#ifndef ZAF_DAMAGE_H
#define ZAF_DAMAGE_H

#include "zaf_rect.h"

#define ZAF_DAMAGE_MAX 16

struct zaf_damage {
    struct zaf_rect items[ZAF_DAMAGE_MAX];
    int count;
};

void zaf_damage_reset(struct zaf_damage *damage);
/* 允许 2px 以内的相邻矩形合并；超过 ZAF_DAMAGE_MAX 时并成总包围盒。 */
void zaf_damage_add(struct zaf_damage *damage, struct zaf_rect rect,
                    struct zaf_rect bounds);
void zaf_damage_add_full(struct zaf_damage *damage, struct zaf_rect bounds);
int zaf_damage_empty(const struct zaf_damage *damage);
int zaf_damage_count(const struct zaf_damage *damage);
struct zaf_rect zaf_damage_item(const struct zaf_damage *damage, int index);

#endif
```

不变量（写进 `.c` 顶部注释）：所有 item 都已与 `bounds` 相交且非空；
`count <= ZAF_DAMAGE_MAX`；合并后任意两个 item 不相邻（2px 容差内）。

### 5.3 `zaf/gfx/zaf_painter.h`

只封装 ADE/DWM 现在真正用到的 `render_*`，不是完整重画 API：

```c
#ifndef ZAF_PAINTER_H
#define ZAF_PAINTER_H

#include <render.h>
#include "core/zaf_rect.h"

void zaf_painter_target(struct render_surface *surface);
struct render_surface *zaf_painter_surface(void);
void zaf_painter_clip(struct zaf_rect rect);
struct zaf_rect zaf_painter_clip_rect(void);

void zaf_paint_fill(struct zaf_rect r, u32 rgb);
void zaf_paint_blend(struct zaf_rect r, u32 rgb, u32 alpha);
void zaf_paint_round(struct zaf_rect r, int radius, u32 rgb, u32 alpha);
void zaf_paint_vgrad(struct zaf_rect r, u32 top, u32 bottom);
void zaf_paint_hline(int x, int y, int w, u32 rgb);
void zaf_paint_shadow(struct zaf_rect r, int spread);
void zaf_paint_soft_circle(int cx, int cy, int radius, u32 rgb, u32 max_alpha);
void zaf_paint_blit(struct render_surface *source, struct zaf_rect r);
int  zaf_paint_svg(const char *svg, int x, int y, int size);
int  zaf_paint_text(const char *text, int x, int y, u32 rgb, int scale);
int  zaf_paint_text_width(const char *text, int scale);
int  zaf_paint_line_height(void);

void zaf_paint_cursor(int x, int y);
void zaf_paint_cursor_restore(void);
struct zaf_rect zaf_paint_cursor_bounds(int x, int y);

#endif
```

`zaf_painter.c` 是唯一允许直接调 `render_*` 的 ZAF 文件；DWM 与装饰只通过
`zaf_paint_*` 画（ADE 外壳的迁移见 D2）。`zaf_gfx.h` 保持原样，不被 Z1 引用。

### 5.4 `zaf/compositor/zaf_compositor.h`

```c
#ifndef ZAF_COMPOSITOR_H
#define ZAF_COMPOSITOR_H

#include <render.h>
#include "core/zaf_rect.h"
#include "core/zaf_damage.h"

#define ZAF_MAX_LAYERS 16
#define ZAF_NO_LAYER (-1)

/* 回调收到的是已裁剪到“损伤 ∩ 图层包围盒”的矩形，坐标一律为屏幕绝对坐标。 */
typedef void (*zaf_layer_paint_fn)(void *context, struct zaf_rect damage);

struct zaf_compositor_ops {
    zaf_layer_paint_fn background;   /* 壁纸，永远最先画，可为 NULL */
    void *background_context;
};

int  zaf_compositor_init(const struct zaf_compositor_ops *ops);
void zaf_compositor_shutdown(void);
struct render_surface *zaf_compositor_back(void);

/* 返回槽位；order 越小越靠下。图层包围盒应包含阴影/装饰的额外像素。 */
int  zaf_compositor_layer_add(void *context, zaf_layer_paint_fn paint,
                              struct zaf_rect bounds, int order);
void zaf_compositor_layer_remove(int layer);
void zaf_compositor_layer_update(int layer, struct zaf_rect bounds, int order);
void zaf_compositor_layer_visible(int layer, int visible);
void zaf_compositor_layer_damage(int layer);

void zaf_compositor_damage(struct zaf_rect rect);
void zaf_compositor_damage_full(void);
int  zaf_compositor_damaged(void);

/* 合成一帧并按损伤提交；cursor_visible 为 0 时恢复旧光标后不再画。
 * 返回本帧提交的损伤矩形数（无损伤且光标未动时为 0）。 */
int  zaf_compositor_frame(int cursor_x, int cursor_y, int cursor_visible);

/* 统计（ZAF_STATS=1 时由 DWM 周期性打印）。 */
unsigned long zaf_compositor_frame_count(void);
unsigned long zaf_compositor_damage_rect_count(void);
unsigned long zaf_compositor_present_pixels(void);

#endif
```

一帧的顺序（与现有 DWM 行为逐项对应）：

1. 若无损伤且光标位置/可见性未变，直接返回 0。
2. 切到后缓冲，`zaf_paint_cursor_restore()` 把上一帧光标像素还原。
3. 快照本帧损伤矩形；对每个损伤矩形：先画背景，再按 `order` 升序画可见图层，
   每层绘制前把裁剪设为“损伤 ∩ 包围盒”，绘制前再与图层包围盒相交。
4. 光标可见则画到后缓冲。
5. 把每个损伤矩形从后缓冲提交到屏幕（`render_blit`），再提交光标包围盒
   （旧位置 + 新位置）；光标像素同样从后缓冲拷到屏幕。

### 5.5 `zaf/wm/zaf_wm.h`

```c
#ifndef ZAF_WM_H
#define ZAF_WM_H

#include "core/zaf_rect.h"

#define ZAF_MAX_WINDOWS 8
#define ZAF_NO_WINDOW (-1)

struct zaf_wm_metrics {
    int title_height;   /* 装饰标题栏高度 */
    int min_width;
    int min_height;
};

struct zaf_window {
    int used;
    int id;
    struct zaf_rect frame;   /* 含标题栏；不含阴影 */
    int order;
    int drag_dx;
    int drag_dy;
    void *context;           /* DWM 放 ade_app 指针 */
};
typedef struct zaf_window zaf_window;

void zaf_wm_init(const struct zaf_wm_metrics *metrics);

/* 打开窗口；frame 为建议位置/尺寸，越界会被夹到 bounds 内。 */
zaf_window *zaf_wm_open(void *context, struct zaf_rect frame,
                        struct zaf_rect bounds);
void zaf_wm_close(zaf_window *window);
zaf_window *zaf_wm_find(void *context);

zaf_window *zaf_wm_at(int x, int y);        /* 最上层命中，没有返回 NULL */
zaf_window *zaf_wm_focused(void);
void zaf_wm_focus(zaf_window *window);      /* 提升到最上层并设为焦点 */
void zaf_wm_cycles(zaf_window *window);     /* z 序过大时折半（沿用现有逻辑） */

void zaf_wm_drag_begin(zaf_window *window, int x, int y);
void zaf_wm_drag_to(zaf_window *window, int x, int y, struct zaf_rect bounds);
void zaf_wm_drag_end(zaf_window *window);
int  zaf_wm_count(void);
zaf_window *zaf_wm_at_index(int index);

/* 内容区：frame 去掉标题栏与左右 2px 边框。 */
struct zaf_rect zaf_wm_content(const zaf_window *window);

#endif
```

### 5.6 `zaf/theme/zaf_theme.h` 与 `zaf_theme_aqua.c`

```c
#ifndef ZAF_THEME_H
#define ZAF_THEME_H

struct zaf_theme {
    unsigned int window_border;
    unsigned int window_face;
    unsigned int title_top;
    unsigned int title_bottom;
    unsigned int title_shine;
    unsigned int title_separator;
    unsigned int title_text;
    unsigned int close_light[3];
    unsigned int close_border[3];
    int window_radius;
    int window_inner_radius;
    int title_height;
    int title_text_inset;
    int shadow_spread;
    int close_radius;
    int close_spacing;
    int close_inset;
    int content_inset;
    /* Z3 动画引擎用；Z1 只占位，不读取。 */
    int animation_ms_fast;
    int animation_ms_normal;
};

const struct zaf_theme *zaf_theme_aqua(void);

#endif
```

`zaf_theme_aqua.c` 的值必须与今天 `dwm.cpp` 的常量逐字一致（颜色、半径、间距、
阴影），否则截图对比会出现差异。

### 5.7 ADE `decoration.h`

```c
#ifndef ZHAOXU_ADE_DECORATION_H
#define ZHAOXU_ADE_DECORATION_H

#include "winmgr/dwm.h"

int  ade_decoration_title_height(void);
struct render_rect ade_decoration_content_rect(struct render_rect frame);
struct render_rect ade_decoration_damage_rect(struct render_rect frame);
void ade_decoration_paint(struct render_rect frame, const char *title,
                          int focused, struct zaf_rect damage);
int  ade_decoration_close_hit(struct render_rect frame, int x, int y);

#endif
```

## 6. 数据流（一帧）

```
DWM 主循环
  ├─ console_read_char() / mouse_*()        ← 输入（不变）
  ├─ 命中/拖动/关闭 → zaf_wm_*               ← 策略（Z1 抽出）
  ├─ 外壳鼠标状态变化 → zaf_compositor_damage()（Dock hover 等）
  ├─ display_poll()                         ← 客户端命令流（不变）
  ├─ timer tick → 外壳 tick / 焦点 app tick（不变）
  └─ zaf_compositor_frame(mx, my, ready)
        ├─ 后缓冲：光标还原 → 背景 → 图层（按 order）
        ├─ 后缓冲：画光标
        └─ 屏幕：按损伤 blit 后缓冲 + 光标包围盒
```

## 7. 迁移步骤（详见计划）

| Task | 内容 | 行为变化 |
|---|---|---|
| 1 | ZAF core：rect + damage + host 单元测试 | 无（新增文件） |
| 2 | painter 封装 + 合成器（图层/损伤/提交/光标），先接管 DWM 的合成部分 | 观感应完全一致 |
| 3 | WM 核心（窗口表/焦点/z 序/命中/拖动），DWM 变薄 | 观感应完全一致 |
| 4 | 窗口装饰拆出 + Aqua 令牌，颜色/尺寸不再散落 | 观感应完全一致 |
| 5 | 构建接入、统计日志、回归与截图对比 | 无 |
| 6 | 文档同步（AGENTS.md、docs/ARCHITECTURE.md、ZAF 文档状态） | 无 |

Task 2 的 `background` 回调是回退开关：若合成器出问题，把整屏绘制塞进
`background`、图层一个不加，就退化成“DWM 以前怎么画还怎么画”，不必回滚代码。

## 8. 非目标

- 控件系统（Button/TextEdit/List）、事件分发树、布局：Z2。
- 动画引擎、主题包加载、主题运行时切换：Z3。
- Skia 移植与多后端 vtable：独立 spike，Z0.5，需用户决策。
- 窗口协议扩展（缩放、最小化、位图提交、共享内存命令环）：Z2/Z3 按需。
- 多显示器、DPI 缩放模型、无障碍：远期。
- 任何内核、引导、驱动、文件系统改动。

## 9. 验收矩阵

| # | 检查 | 通过标准 |
|---|---|---|
| 1 | 干净构建 | `make clean && make -j4` 成功；`nm -u out/kernel.elf` 无未定义符号 |
| 2 | 内核零改动 | `git diff --stat src/kernel src/boot` 为空 |
| 3 | ISO 启动 | `make run-iso` 打印 ISO 内核/字体日志、`[ade ] Ring3 display server`，直接进桌面 |
| 4 | 双客户端 | 日志出现 welcome/calc/install spawn；欢迎窗点击回显、`7 + 5 = 12`、关闭后 Dock 回收图标 |
| 5 | 光标损伤 | 鼠标移动时 `ZAF_STATS=1` 每帧损伤 ≤ 4 个矩形、提交像素 ≈ 光标包围盒面积 |
| 6 | 拖动损伤 | 拖动窗口时提交像素 ≈ 新旧窗口包围盒并集，不是整屏 |
| 7 | 视觉回归 | QEMU HMP `screendump` 抓帧与 Z0 基线对比：菜单栏/Dock/窗口装饰/字体无差异 |
| 8 | 用户异常 | 客户端异常只终止进程，ADE 继续运行、Dock 回收图标 |
| 9 | SMP | `make run-iso QEMU_SMP=4` 桌面行为与单核一致（ADE 单线程，不引入并发） |
| 10 | 文档 | AGENTS.md 的 DWM 职责、docs/ARCHITECTURE.md 里程碑、ZAF 文档状态一致 |

## 10. 环境前置条件

当前会话所在 Windows 机器**没有 WSL2，也没有 gcc/make/ruby/qemu**（`wsl.exe`
提示未安装）。因此本设计文档与计划可以先落地，但 Task 1–5 的编译、QEMU、
截图验证必须在装好 WSL2 + Ubuntu 26.04 与 AGENTS.md 所列依赖的机器上执行。
在工具链可用前，只允许提交不依赖编译的改动（文档/脚本/配置）。

## 11. 风险与回退

1. **合成器重写导致视觉回归**：Task 2 保留“整屏 background 单图层”回退路径；
   每步都用 screendump 与基线对比，不通过就停在上一 Task。
2. **render.c 的全局状态（target/clip/cursor）被新调用顺序打乱**：ZAF 只通过
   `zaf_painter` 访问，painter 每帧显式设置 target/clip，不做隐式假设；单元测试
   覆盖纯逻辑部分（rect/damage），渲染部分靠截图。
3. **图层包围盒漏算阴影**：`dwm.cpp` 现在的 `window_damage_rect` 带
   `dwm_ui(14)` 的内边距（阴影 9 + 余量）；装饰模块必须提供同样的扩展矩形，
   否则拖动/关闭会留下阴影残影。
4. **窗口表与图层槽位不一致**：DWM 适配层是唯一同时持有两者映射的地方，窗口
   关闭时先删图层再释放窗口槽位，顺序写进代码注释与测试步骤。
5. **没有工具链时无法验证**：不改内核/引导/驱动，风险限制在 Ring3 ADE；最坏
   情况是桌面起不来，回退到 Z0 的 `dwm.cpp` 单文件实现。
