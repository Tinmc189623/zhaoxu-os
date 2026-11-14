# ZAF Z1 实施计划：合成器与窗口管理

- 规格：[../specs/2026-11-14-zaf-z1-compositor-design.md](../specs/2026-11-14-zaf-z1-compositor-design.md)
- 分支：`feat/ext4-primary`（沿用当前分支；不新建分支、不推送）
- 模型：会话默认模型；只在工具链可用的 WSL2 环境中执行验证步骤
- 前置：Z0 已交付（`zaf_gfx.h` + `zaf_gfx_soft.c` + `zaf_gfx_demo`），Aqua Desktop Environment/Desktop Window Manager 桌面可启动

本计划只做 Ring3 的 `src/zaf/` 与 `src/apps/ADE/`，**不改** `src/kernel/`、
`src/boot/`、`src/drivers/`、`src/fs/`。每个 Task 结束都能构建、能启动桌面；
任一步视觉回归不过就停在上一 Task，不连续堆改动。

> ⚠️ 当前机器没有 WSL2 与编译工具链，本计划的任务代码不能在会话内验证。
> 执行前先完成 Task 0，并确认 `wsl -d Ubuntu-26.04 -- uname -a` 可用。

## 接口自检（Preflight）

| 产出 → 消费 | 内容 | 结论 |
|---|---|---|
| Task 1 rect/damage → Task 2 compositor | `struct zaf_rect`、`struct zaf_damage`、`ZAF_DAMAGE_MAX` | 头文件无系统依赖，可直接被 compositor 引用 |
| Task 2 painter → Task 2/3/4 绘制 | `zaf_paint_*` 签名 | 与 `render.h` 一一对应，无隐式 target 假设 |
| Task 2 compositor → Task 3 wm | 窗口图层增删改、`layer_damage()` | wm 返回窗口矩形，Desktop Window Manager 负责转成图层 bounds |
| Task 3 wm → Task 4 decoration | `zaf_wm_content()`、`zaf_wm_metrics` | 装饰只接收 frame/title，不反向依赖 wm 内部状态 |
| Task 4 theme → Task 4 decoration | 令牌字段 | 与 `dwm.cpp` 现有魔法数逐项对应 |
| Task 2/5 Makefile 改动 | `ADE_C_SRCS`、`-I.`、`ZAF_STATS` | 只增不改既有对象；`ADE_DEPS` 沿用 `-MMD` |

## Task 0：环境与基线（必须先做）

**Files:** 无源码改动；产物写到 `out/`、`logs/`（均不入库）。

- [ ] **Step 1：确认工具链**

```bash
wsl -d Ubuntu-26.04 -- bash -lc 'gcc --version | head -1; ruby --version; \
  qemu-system-x86_64 --version | head -1; xorriso -version 2>&1 | head -1'
```

Expected: 四条版本都打印。缺件按 AGENTS.md “依赖”一节安装。

- [ ] **Step 2：干净构建并记录基线**

```bash
cd src
make clean && make -j4
nm -u out/kernel.elf          # 期望：无输出
make iso
```

- [ ] **Step 3：抓 Z0 基线截图与启动日志**

```bash
cd src
timeout 40 make run-iso 2>&1 | tee ../logs/zaf-z0-baseline.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-baseline.png
```

基线日志必须包含：`/install/KERNEL.ELF from the ISO.`、`Font: ...`、
`[ade ] Ring3 display server`、welcome/calc/install 的 spawn 行。
基线截图必须能看到：壁纸、菜单栏、Dock、Welcome 与 Calculator 两个窗口。

- [ ] **Step 4：检查点**

```bash
git status --porcelain
```

Expected: 只有本计划/规格/`src/zaf/README.md` 这些文档改动（或为空），没有
`src/kernel`、`src/boot` 的改动。

---

## Task 1：ZAF core（矩形 + 有界损伤）与 host 单元测试

**Files:**
- Create: `src/zaf/core/zaf_rect.h`、`src/zaf/core/zaf_rect.c`
- Create: `src/zaf/core/zaf_damage.h`、`src/zaf/core/zaf_damage.c`
- Create: `src/tests/zaf/damage_test.c`
- Modify: 无（本 Task 不接 Makefile，先保证单独可测）

**Interfaces:** 见规格 5.1、5.2。`zaf_rect.h` 不 include 任何系统头；
`zaf_damage.c` 只 include `zaf_damage.h`。

- [ ] **Step 1：写 `zaf_rect.h` / `zaf_rect.c`**

按规格 5.1 落地。`zaf_rect.c` 只实现非 inline 的四个函数：

```c
struct zaf_rect zaf_rect_intersect(struct zaf_rect a, struct zaf_rect b);
struct zaf_rect zaf_rect_union(struct zaf_rect a, struct zaf_rect b);
int zaf_rect_width(struct zaf_rect r);
int zaf_rect_height(struct zaf_rect r);
int zaf_rect_area(struct zaf_rect r);
```

`intersect` 对空结果把 `x1 = x0`、`y1 = y0`（与 `render_rect_intersect` 行为一致）；
`union` 对空矩形返回另一个矩形。

- [ ] **Step 2：写 `zaf_damage.h` / `zaf_damage.c`**

从 `src/apps/ADE/winmgr/dwm.cpp:58-95` 的 `damage_add_raw` / `damage_rect` 逐行
搬迁，只把 `struct render_rect` 换成 `struct zaf_rect`、把 `g_screen` 换成
`bounds` 参数、把 `g_damage` 换成显式结构体指针。顶部注释写清不变量（规格 5.2）。

- [ ] **Step 3：写 host 测试 `src/tests/zaf/damage_test.c`**

不依赖内核/用户库，用 `printf` + 返回非 0 表示失败。至少覆盖：

1. 空区域 → `count == 0`、`empty == 1`。
2. 加一个越界矩形 → 被 `bounds` 裁剪，非空。
3. 加两个相距 ≤2px 的矩形 → 合并成一个。
4. 加两个相距 >2px 的矩形 → 保持两个。
5. 连续加 20 个互不相邻的矩形 → `count == ZAF_DAMAGE_MAX`，最后一项是总包围盒。
6. `add_full` → 恰好 1 个等于 bounds 的矩形。
7. 随机 1000 次加入后：所有 item 非空、都在 bounds 内、count ≤ 16。

- [ ] **Step 4：编译并运行测试**

```bash
cd src
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Izaf/core \
    tests/zaf/damage_test.c zaf/core/zaf_rect.c zaf/core/zaf_damage.c \
    -o /tmp/zaf_damage_test
/tmp/zaf_damage_test
```

Expected: 每行 `ok ...`，最后 `PASS`，退出码 0。

- [ ] **Step 5：检查点**

```bash
git status --porcelain src/zaf src/tests
git diff --stat
```

Expected: 只新增 5 个文件；没有内核/引导改动。

**Commit:** `feat(zaf): bounded damage regions and rectangle core with host tests`

---

## Task 2：painter 封装 + 合成器（图层 / 损伤 / 提交 / 光标）

**Files:**
- Create: `src/zaf/gfx/zaf_painter.h`、`src/zaf/gfx/zaf_painter.c`
- Create: `src/zaf/compositor/zaf_compositor.h`、`zaf_compositor.c`
- Create: `src/zaf/compositor/zaf_present.c`、`zaf_cursor.c`
- Modify: `src/Makefile`（`ADE_C_SRCS` 增加 ZAF 源文件；`-I.`；`ZAF_STATS` 变量）
- Modify: `src/apps/ADE/winmgr/dwm.cpp`（合成部分改走 ZAF）
- Modify: `src/apps/ADE/main.cpp`（不需要改调用方；若需要 `zaf_compositor_init`
  的时序则在 `ade_setup` 后一行接入）

**Interfaces:** 见规格 5.3、5.4。

- [ ] **Step 1：写 `zaf_painter`**

按规格 5.3 落地；`.c` 里每个函数是一行 `render_*` 转发。唯一注意点：

- `zaf_painter_target()` 调 `render_set_target()`；
- `zaf_painter_clip()` 把 `zaf_rect` 转成 `struct render_rect{ x0, y0, x1, y1 }`
  后调 `render_set_clip()`；
- `zaf_paint_shadow()` 用 `render_soft_shadow(x, y, w, h, spread)`；
- `zaf_paint_soft_circle()` 用 `render_soft_circle()`，`zaf_paint_svg()` 用
  `render_svg()`（Z1 的装饰暂不需要 SVG，保留给 Z2 控件图标）；
- 光标三个函数直接转发 `render_cursor*`。

- [ ] **Step 2：写合成器**

`zaf_compositor.c` 只做：初始化/关闭、图层表（16 槽）、图层增删改、损伤入口、
`frame()` 的绘制顺序。`zaf_present.c` 只做“后缓冲 → 屏幕”的 damage 提交；
`zaf_cursor.c` 只做光标保存/恢复/提交。三个文件之间的边界写进文件头注释。

`frame()` 的实现要点（与 `dwm.cpp:229-261` 对应）：

```c
int zaf_compositor_frame(int cursor_x, int cursor_y, int cursor_visible)
{
    int moved = cursor_visible &&
                (!g_cursor_presented || cursor_x != g_cursor_present_x ||
                 cursor_y != g_cursor_present_y);
    if (zaf_damage_empty(&g_damage) && !moved)
        return 0;

    int had_old = g_cursor_presented;
    int old_x = g_cursor_present_x;
    int old_y = g_cursor_present_y;

    struct zaf_damage snapshot = g_damage;
    zaf_damage_reset(&g_damage);

    zaf_painter_target(g_back);
    zaf_paint_cursor_restore();

    for (int i = 0; i < snapshot.count; i++) {
        struct zaf_rect damage = zaf_damage_item(&snapshot, i);
        paint_background(damage);
        paint_layers_in_order(damage);
    }

    if (cursor_visible)
        zaf_paint_cursor(cursor_x, cursor_y);

    for (int i = 0; i < snapshot.count; i++)
        zaf_present_rect(zaf_damage_item(&snapshot, i));
    if (cursor_visible)
        zaf_present_cursor(cursor_x, cursor_y, had_old, old_x, old_y);
    else
        g_cursor_presented = 0;

    g_frame_count++;
    return snapshot.count;
}
```

`paint_layers_in_order` 每层：`if (!visible || order 排序) continue;` 仅在
`zaf_rect_intersects(damage, bounds)` 时，`zaf_painter_clip(intersect(damage,
bounds))` 后调用 `paint(context, damage_clipped)`。

`zaf_present_rect()` 从 `dwm.cpp:208-216` 搬迁：`zaf_painter_target(screen)`、
`zaf_paint_blit(back, rect)`、再切回 `back`。

- [ ] **Step 3：Makefile 接入**

```make
ADE_INCLUDES := -I. -Iuser -Iuser/lib/include -Ikernel/include -Ikernel \
                -Ikernel/font
ZAF_C_SRCS := zaf/core/zaf_rect.c zaf/core/zaf_damage.c \
              zaf/gfx/zaf_painter.c \
              zaf/compositor/zaf_compositor.c \
              zaf/compositor/zaf_present.c zaf/compositor/zaf_cursor.c
ZAF_HDRS := $(wildcard zaf/*/*.h)
ADE_C_SRCS := $(ZAF_C_SRCS) user/lib/shim.c user/lib/string_extra.c \
              user/userlib.c kernel/gfx/render.c kernel/gfx/svg.c \
              kernel/font/font.c
```

`ZAF_STATS ?= 0` 加到构建参数区，`ADE_CFLAGS` 追加 `-DZAF_STATS=$(ZAF_STATS)`。
先只编译不调用，确认 `make out/aqua.zxe` 通过。

- [ ] **Step 4：Desktop Window Manager 接入合成器（不改窗口策略）**

`dwm.cpp` 内保留 `dwm_window` 表与 `paint_window`，替换掉这些实现：

| 旧符号 | 新实现 |
|---|---|
| `damage_list` / `damage_reset` / `damage_add_raw` / `damage_rect` | `zaf_compositor_damage*` |
| `paint_rect()` | 拆成 `paint_background()`（壁纸）与菜单栏/Dock/窗口三个图层回调 |
| `present_rect()` / `present_cursor()` / `paint()` | `zaf_compositor_frame()` |
| `window_damage_rect()` | 保留，作为窗口图层 bounds |

`dwm_init`：`zaf_compositor_init({.background = paint_wallpaper})`，注册菜单栏图层
（order 10，bounds = 菜单栏矩形）、Dock 图层（order 11，bounds = Dock 矩形）。
外壳回调签名是 `void(void)`，在 `dwm.cpp` 里加两个一行的适配器：

```c
static void paint_menubar_layer(void *context, struct zaf_rect damage)
{
    (void)context; (void)damage;
    g_ops->paint_menubar();   /* 合成器已设好裁剪 */
}
```

Dock 图层同理。`paint_wallpaper(void *context, struct zaf_rect damage)` 里调
`zaf_paint_blit(g_ops->wallpaper, damage)`，与旧 `paint_rect` 的壁纸分支完全一致。

`dwm_open`：注册窗口图层（order = `100 + z`），bounds = `window_damage_rect`。
`bring_to_front`：`zaf_compositor_layer_update(layer, bounds, 100 + z)` 并
`layer_damage` 旧/新焦点窗口。`close_window`：先 `layer_remove` 再清窗口槽位。
`dwm_run`：把 `paint()` 调用换成 `zaf_compositor_frame(mx, my, mouse_ready())`。

`dwm_ops` 结构不变（`wallpaper`、`menu_h`、`dock_h`、三个回调、`poll`）。

- [ ] **Step 5：构建与行为回归**

```bash
cd src
make clean && make -j4
timeout 40 make run-iso 2>&1 | tee ../logs/zaf-z1-task2.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-task2.png
```

Expected: 日志出现 `[zaf] compositor <W>x<H> back buffer ...`；截图与
`out/zaf-z1-baseline.png` 肉眼无差异；窗口可拖动/关闭，Dock 回收图标。

- [ ] **Step 6：检查点**

```bash
git diff --stat src/kernel src/boot        # 期望：空
git diff --stat                            # 期望：只有 zaf/、Aqua Desktop Environment、Makefile
```

**Commit:** `feat(zaf): compositor with layers, damage-only present and cursor overlay`

---

## Task 3：WM 核心（窗口表 / 焦点 / z 序 / 命中 / 拖动）

**Files:**
- Create: `src/zaf/wm/zaf_wm.h`、`src/zaf/wm/zaf_wm.c`
- Modify: `src/Makefile`（`ZAF_C_SRCS` 增加 wm）
- Modify: `src/apps/ADE/winmgr/dwm.cpp`（窗口状态改用 `zaf_wm`，变成适配层）

**Interfaces:** 见规格 5.5。

- [ ] **Step 1：写 `zaf_wm`**

从 `dwm.cpp` 搬迁：`g_windows`/`g_focus`/`g_next_z`（对应 `zaf_wm.c` 内部状态）、
`window_at()`（→ `zaf_wm_at`）、`bring_to_front()`（→ `zaf_wm_focus` + 折半规则）、
`dwm_open` 里的位置夹取与槽位分配（→ `zaf_wm_open`）、拖动状态（→
`zaf_wm_drag_*`）。不做任何绘制、不引用 `render.h`。

窗口 id 用 `id = ++g_next_id` 单调递增，便于日志与调试；`context` 不动。

- [ ] **Step 2：Desktop Window Manager 改为适配层**

- 删除 `dwm_window` 结构与 `g_windows`；保留 `int g_window_layer[ZAF_MAX_WINDOWS]`。
- `dwm_open`：`zaf_wm_open(app, frame, bounds)` → 取 index → 注册图层 →
  `app->init()` → `zaf_compositor_layer_damage`。
- `close_window`：`zaf_wm_close` 前先按 index 删图层。
- `bring_to_front`：`zaf_wm_focus` 后按新 `order` 更新图层。
- 拖动：`handle_press` 里 `zaf_wm_drag_begin`；`dwm_run` 拖动分支
  `zaf_wm_drag_to`，夹取用的 bounds 仍在 Desktop Window Manager 里计算（菜单栏/Dock 约束是外壳策略）。
- `dwm_content_rect` → `zaf_wm_content` 的薄封装（返回 `struct render_rect`）。
- 关闭按钮命中仍由 Desktop Window Manager/装饰决定（Task 4 移到 `ade_decoration_close_hit`）。

- [ ] **Step 3：构建与行为回归**

```bash
cd src
make clean && make -j4
timeout 40 make run-iso 2>&1 | tee ../logs/zaf-z1-task3.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-task3.png
```

Expected: 桌面行为同 Task 2；日志无 `[zaf]` 错误；两个客户端窗口仍可交互。

- [ ] **Step 4：检查点**

```bash
git diff --stat src/kernel src/boot        # 期望：空
grep -n "g_windows\|g_next_z\|window_at(" src/apps/ADE/winmgr/dwm.cpp
# 期望：无输出（窗口状态已只在 zaf/wm 里）
```

**Commit:** `feat(zaf): window table, z-order, focus and drag core`

---

## Task 4：窗口装饰独立 + Aqua 令牌

**Files:**
- Create: `src/zaf/theme/zaf_theme.h`、`src/zaf/theme/zaf_theme_aqua.c`
- Create: `src/apps/ADE/decoration.h`、`src/apps/ADE/decoration.cpp`
- Modify: `src/Makefile`（`ZAF_C_SRCS` 增加 theme；`ADE_CXX_SRCS` 增加 decoration）
- Modify: `src/apps/ADE/winmgr/dwm.cpp`（删除装饰绘制，调用 `ade_decoration_*`）

**Interfaces:** 见规格 5.6、5.7。

- [ ] **Step 1：写主题令牌**

`zaf_theme_aqua.c` 的值逐项取自 `dwm.cpp:110-165` 与 `dwm.cpp:312-320`：

| 令牌 | 值 | 来源 |
|---|---|---|
| `window_border` | `0x4A6072` | `paint_window` 外框 |
| `window_face` | `0xF4F7FA` | 窗口底色 |
| `title_top` / `title_bottom` | `0xFDFEFF` / `0xC9DBEC` | 标题渐变 |
| `title_shine` | `0xFFFFFF`（alpha 60） | 标题高光 |
| `title_separator` | `0x8EA2B6` | 标题下分隔线 |
| `title_text` | `0x1B2C3D` | 标题文字 |
| `close_light[]` | `0xE9685C, 0xF0BE4E, 0x62C34E` | 红绿灯 |
| `close_border[]` | `0xA53A30, 0xA87B24, 0x3D8A2E` | 红绿灯描边 |
| `window_radius` / `window_inner_radius` | `12` / `11` | 圆角 |
| `title_height` | `32` | `dwm_ui(32)` 的未缩放值 |
| `shadow_spread` | `9` | `render_soft_shadow` |
| `close_radius` | `7` | 红绿灯半径 |
| `close_spacing` | `20` | 红绿灯间距 |
| `close_inset` | `20` | 左边距 |
| `content_inset` | `2` | 内容区边框 |

所有像素度量按项目惯例经 `ade_ui()` / `dwm_ui()` 缩放，令牌存未缩放值。

- [ ] **Step 2：写 `decoration.cpp`**

把 `dwm.cpp` 的 `traffic_lights()`、`paint_window()` 的装饰部分、
`close_button_at()`、`window_damage_rect()` 搬进来，签名按规格 5.7：

- `ade_decoration_paint()`：阴影 → 外框 → 内底 → 标题渐变/高光/分隔线 →
  红绿灯（按 focused 调 alpha）→ 标题文字。
- `ade_decoration_content_rect()`：与 `dwm_content_rect` 完全一致。
- `ade_decoration_damage_rect()`：窗口矩形向外扩 `dwm_ui(14)`（阴影 + 余量），
  必须与旧 `window_damage_rect` 相同，否则拖动残影。
- `ade_decoration_close_hit()`：圆半径 = `dwm_ui(7) + dwm_ui(3)`，圆心与旧逻辑一致。

- [ ] **Step 3：Desktop Window Manager 调用装饰**

`paint_window` 只剩：

```c
zaf_painter_clip(damage);
ade_decoration_paint(frame, window->app->name, focused, damage);
struct zaf_rect content = zaf_wm_content(window);
zaf_painter_clip(zaf_rect_intersect(damage, content));
window->app->paint(&view);
```

`dwm_title_height()` 返回 `ade_decoration_title_height()`；
`dwm_window_rect()` 返回 `ade_decoration_damage_rect()`。
`zaf_wm_metrics` 用 `ade_decoration_title_height()` 初始化。

- [ ] **Step 4：构建与视觉回归**

```bash
cd src
make clean && make -j4
ruby tools/screenshot.rb 3.0 out/zaf-z1-task4.png
```

Expected: 截图与基线逐像素一致（装饰的所有数值都是搬移，不是重设）。若
ImageMagick 可用：`compare -metric AE out/zaf-z1-baseline.png
out/zaf-z1-task4.png /dev/null` 期望 0；否则用图像查看器并排检查。

**Commit:** `feat(zaf): split window decoration into an Aqua-themed module`

---

## Task 5：统计日志与完整回归

**Files:**
- Modify: `src/Makefile`（`ZAF_STATS ?= 0`，已由 Task 2 加；此处核对）
- Modify: `src/apps/ADE/winmgr/dwm.cpp`（每秒打印一行统计）

- [ ] **Step 1：统计输出**

`ZAF_STATS=1` 时，`dwm_run` 每秒打印：

```c
kprintf("[zaf] frames=%lu damage=%lu rects=%lu pixels=%lu\n",
        zaf_compositor_frame_count(), ..., ...);
```

默认 `ZAF_STATS=0` 不输出（与现有 `SCHED_STATS` 约定一致）。

- [ ] **Step 2：执行验收矩阵（规格第 9 节）**

```bash
cd src
make clean && make -j4
nm -u out/kernel.elf                                   # 无输出
git diff --stat src/kernel src/boot                    # 空
timeout 40 make run-iso ZAF_STATS=1 2>&1 | tee ../logs/zaf-z1-final.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-final.png
```

日志检查项：

- `Kernel: /install/KERNEL.ELF from the ISO.`
- `Font: /install/WQY.TTC from the ISO.`
- `[ade ] Ring3 display server`
- `spawn /bin/apps/welcome.zxe`、`calc.zxe`、`install.zxe`
- `[zaf]` 帧统计在鼠标未动时 `rects` 应为 0（无损伤帧不提交）
- 点击欢迎窗口回显 `已收到点击事件`
- 计算器 `7 + 5 =` 显示 `12`，关闭后 Dock 图标消失

- [ ] **Step 3：损伤专项检查（人工，QEMU gtk）**

```bash
cd src
make run-iso ZAF_STATS=1 QEMU_DISPLAY="-display gtk"
```

- 只移动鼠标：日志中每帧 `rects ≤ 4`、`pixels` 不超过两个光标包围盒
  （19×20×2 ≈ 760 像素量级）。
- 拖动窗口：`pixels` 接近新旧窗口包围盒并集，而不是整屏（1920×1080）。
- 静止 2 秒：不打印新帧（`rects=0` 时 `frame()` 直接返回）。

- [ ] **Step 4：异常与 SMP 回归**

```bash
timeout 40 make run-iso QEMU_SMP=4 2>&1 | tee ../logs/zaf-z1-smp.log
```

Expected: `4/4 CPU(s) online`；桌面正常；客户端异常只终止进程、Aqua Desktop Environment 继续运行。

- [ ] **Step 5：检查点**

```bash
git status --porcelain
git diff --stat
```

**Commit:** `test(zaf): damage statistics and the Z1 regression matrix`

---

## Task 6：文档同步与收尾

**Files:**
- Modify: `AGENTS.md`（“桌面分层”一条：损伤合成归 ZAF 合成器，Desktop Window Manager 归策略/装饰/输入）
- Modify: `docs/ARCHITECTURE.md`（ZAF 里程碑状态、Ring3 边界补 `src/zaf/`）
- Modify: `docs/superpowers/specs/2026-11-13-zaf-architecture-design.md`（状态与 Z1 链接）
- Modify: `docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md`（状态改“已实现”）
- Modify: `src/apps/ADE/ARCHITECTURE.md`（目录树加 decoration.cpp，说明合成器归 ZAF）
- 可能需要：`README.md` 的“架构”表加一行 ZAF（规则 13：README 只写当前版本，
  Z1 不是发布版本，**不加**，等版本号变更时随发布更新）

- [ ] **Step 1：改文档**（逐条对照实际实现，禁止提前写“已完成”）
- [ ] **Step 2：全量自检**

```bash
cd src
make clean && make -j4
git diff --stat src/kernel src/boot        # 空
```

- [ ] **Step 3：检查点**

```bash
git status --porcelain
git diff --stat
```

**Commit:** `docs(zaf): sync architecture docs with the Z1 compositor`

---

## 提交序列

| # | Commit | 内容 |
|---|---|---|
| 1 | `feat(zaf): bounded damage regions and rectangle core with host tests` | Task 1 |
| 2 | `feat(zaf): compositor with layers, damage-only present and cursor overlay` | Task 2 |
| 3 | `feat(zaf): window table, z-order, focus and drag core` | Task 3 |
| 4 | `feat(zaf): split window decoration into an Aqua-themed module` | Task 4 |
| 5 | `test(zaf): damage statistics and the Z1 regression matrix` | Task 5 |
| 6 | `docs(zaf): sync architecture docs with the Z1 compositor` | Task 6 |

只做本地提交，不推送；三个远端（GitHub main / Gitee master / GitCode main）保持
不变，直到用户确认发布。

## 回退策略

- **Task 2 失败**：`zaf_compositor` 的 `background` 回调是整屏回退开关；把
  `paint_background` 设为旧的 `paint_rect` 全量逻辑、不注册图层，即恢复 Z0 行为。
- **Task 3 失败**：Desktop Window Manager 保留 `dwm_window` 表，只把 `zaf_wm` 当未使用的库；删掉
  `zaf/wm` 的源文件与 Makefile 条目即可。
- **Task 4 失败**：装饰绘制退回 `dwm.cpp`，主题文件保留但不引用。
- 任何一步导致桌面无法启动：`git revert` 对应提交，回到上一 Task 的已验证状态。
