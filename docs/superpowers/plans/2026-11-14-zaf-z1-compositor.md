# ZAF Z1 implementation plan: compositor and window management

- Spec: [../specs/2026-11-14-zaf-z1-compositor-design.md](../specs/2026-11-14-zaf-z1-compositor-design.md)
- Branch: `feat/ext4-primary` (continue on the current branch; no new branch, no push)
- Model: the session default model; run the verification steps only in the WSL2 environment where the toolchain is available
- Prerequisites: Z0 delivered (`zaf_gfx.h` + `zaf_gfx_soft.c` + `zaf_gfx_demo`), the Aqua Desktop Environment/Desktop Window Manager desktop can boot

This plan works only on Ring3 `src/zaf/` and `src/apps/ADE/`, and **does not touch** `src/kernel/`,
`src/boot/`, `src/drivers/`, `src/fs/`. Every Task ends in a state that builds and boots the desktop;
if the visual regression fails at any step, stop at the previous Task instead of piling up changes.

> ⚠️ This machine has no WSL2 and no build toolchain, so the task code in this plan cannot be verified
> inside the session. Finish Task 0 first and confirm `wsl -d Ubuntu-26.04 -- uname -a` works.

## Interface self-check (Preflight)

| Producer → consumer | Content | Conclusion |
|---|---|---|
| Task 1 rect/damage → Task 2 compositor | `struct zaf_rect`, `struct zaf_damage`, `ZAF_DAMAGE_MAX` | Headers have no system dependencies and can be included directly by the compositor |
| Task 2 painter → Task 2/3/4 drawing | `zaf_paint_*` signatures | One-to-one with `render.h`, no implicit target assumptions |
| Task 2 compositor → Task 3 wm | layer add/remove/update, `layer_damage()` | wm returns window rects, Desktop Window Manager turns them into layer bounds |
| Task 3 wm → Task 4 decoration | `zaf_wm_content()`, `zaf_wm_metrics` | Decoration only receives frame/title, never depends on wm internal state in reverse |
| Task 4 theme → Task 4 decoration | token fields | Correspond item by item with the existing magic numbers in `dwm.cpp` |
| Task 2/5 Makefile changes | `ADE_C_SRCS`, `-I.`, `ZAF_STATS` | Only add, never change existing objects; `ADE_DEPS` keeps using `-MMD` |

## Task 0: environment and baseline (do this first)

**Files:** no source changes; artifacts go to `out/` and `logs/` (neither is committed).

- [ ] **Step 1: confirm the toolchain**

```bash
wsl -d Ubuntu-26.04 -- bash -lc 'gcc --version | head -1; ruby --version; \
  qemu-system-x86_64 --version | head -1; xorriso -version 2>&1 | head -1'
```

Expected: all four versions print. Install whatever is missing per the "Dependencies" section of AGENTS.md.

- [ ] **Step 2: clean build and record the baseline**

```bash
cd src
make clean && make -j4
nm -u out/kernel.elf          # expected: no output
make iso
```

- [ ] **Step 3: capture the Z0 baseline screenshot and boot log**

```bash
cd src
timeout 40 make run-iso 2>&1 | tee ../logs/zaf-z0-baseline.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-baseline.png
```

The baseline log must contain: `/install/KERNEL.ELF from the ISO.`, `Font: ...`,
`[ade ] Ring3 display server`, and the spawn lines for welcome/calc/install.
The baseline screenshot must show: the wallpaper, the menu bar, the Dock, and the two Welcome and Calculator windows.

- [ ] **Step 4: checkpoint**

```bash
git status --porcelain
```

Expected: only the documentation changes for this plan/spec/`src/zaf/README.md` (or nothing at all),
and no changes under `src/kernel` or `src/boot`.

---

## Task 1: ZAF core (rectangles + bounded damage) with host unit tests

**Files:**
- Create: `src/zaf/core/zaf_rect.h`, `src/zaf/core/zaf_rect.c`
- Create: `src/zaf/core/zaf_damage.h`, `src/zaf/core/zaf_damage.c`
- Create: `src/tests/zaf/damage_test.c`
- Modify: none (this Task is not wired into the Makefile; the goal is that it can be tested on its own)

**Interfaces:** see spec 5.1 and 5.2. `zaf_rect.h` includes no system headers;
`zaf_damage.c` includes only `zaf_damage.h`.

- [ ] **Step 1: write `zaf_rect.h` / `zaf_rect.c`**

Implement per spec 5.1. `zaf_rect.c` implements only the four non-inline functions:

```c
struct zaf_rect zaf_rect_intersect(struct zaf_rect a, struct zaf_rect b);
struct zaf_rect zaf_rect_union(struct zaf_rect a, struct zaf_rect b);
int zaf_rect_width(struct zaf_rect r);
int zaf_rect_height(struct zaf_rect r);
int zaf_rect_area(struct zaf_rect r);
```

For an empty result `intersect` sets `x1 = x0`, `y1 = y0` (matching `render_rect_intersect` behaviour);
`union` returns the other rectangle when one is empty.

- [ ] **Step 2: write `zaf_damage.h` / `zaf_damage.c`**

Move `damage_add_raw` / `damage_rect` line by line from `src/apps/ADE/winmgr/dwm.cpp:58-95`, changing only
`struct render_rect` to `struct zaf_rect`, `g_screen` to the `bounds` parameter, and
`g_damage` to an explicit structure pointer. Put the invariants in the header comment (spec 5.2).

- [ ] **Step 3: write the host test `src/tests/zaf/damage_test.c`**

No dependency on the kernel or the user library; use `printf` and a non-zero return to signal failure. Cover at least:

1. Empty region → `count == 0`, `empty == 1`.
2. Adding one out-of-bounds rectangle → clipped by `bounds`, non-empty.
3. Adding two rectangles ≤2px apart → merged into one.
4. Adding two rectangles >2px apart → kept as two.
5. Adding 20 mutually non-adjacent rectangles in a row → `count == ZAF_DAMAGE_MAX`, the last entry is the total bounding box.
6. `add_full` → exactly 1 rectangle equal to bounds.
7. After 1000 random additions: every item is non-empty, all lie inside bounds, count ≤ 16.

- [ ] **Step 4: compile and run the test**

```bash
cd src
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Izaf/core \
    tests/zaf/damage_test.c zaf/core/zaf_rect.c zaf/core/zaf_damage.c \
    -o /tmp/zaf_damage_test
/tmp/zaf_damage_test
```

Expected: one `ok ...` line each, `PASS` at the end, exit code 0.

- [ ] **Step 5: checkpoint**

```bash
git status --porcelain src/zaf src/tests
git diff --stat
```

Expected: only 5 new files; no kernel/boot changes.

**Commit:** `feat(zaf): bounded damage regions and rectangle core with host tests`

---

## Task 2: painter wrapper + compositor (layers / damage / present / cursor)

**Files:**
- Create: `src/zaf/gfx/zaf_painter.h`, `src/zaf/gfx/zaf_painter.c`
- Create: `src/zaf/compositor/zaf_compositor.h`, `zaf_compositor.c`
- Create: `src/zaf/compositor/zaf_present.c`, `zaf_cursor.c`
- Modify: `src/Makefile` (`ADE_C_SRCS` gains the ZAF sources; `-I.`; the `ZAF_STATS` variable)
- Modify: `src/apps/ADE/winmgr/dwm.cpp` (composition goes through ZAF)
- Modify: `src/apps/ADE/main.cpp` (no caller change needed; if the timing of `zaf_compositor_init`
  requires it, hook it in one line after `ade_setup`)

**Interfaces:** see spec 5.3 and 5.4.

- [ ] **Step 1: write `zaf_painter`**

Implement per spec 5.3; each function in the `.c` is a one-line forward to `render_*`. The only things to watch:

- `zaf_painter_target()` calls `render_set_target()`;
- `zaf_painter_clip()` converts `zaf_rect` to `struct render_rect{ x0, y0, x1, y1 }`
  and then calls `render_set_clip()`;
- `zaf_paint_shadow()` uses `render_soft_shadow(x, y, w, h, spread)`;
- `zaf_paint_soft_circle()` uses `render_soft_circle()`, `zaf_paint_svg()` uses
  `render_svg()` (Z1 decoration does not need SVG for now; keep it for Z2 control icons);
- the three cursor functions forward straight to `render_cursor*`.

- [ ] **Step 2: write the compositor**

`zaf_compositor.c` does only: init/shutdown, the layer table (16 slots), layer add/remove/update, the damage entry point, and the drawing order in `frame()`.
`zaf_present.c` does only the "back buffer → screen" damage present; `zaf_cursor.c` does only cursor save/restore/present.
Write the boundaries between the three files into their file header comments.

Key points of the `frame()` implementation (corresponding to `dwm.cpp:229-261`):

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

`paint_layers_in_order` per layer: `if (!visible || order sort) continue;` and only when
`zaf_rect_intersects(damage, bounds)` holds, call `paint(context, damage_clipped)` after
`zaf_painter_clip(intersect(damage, bounds))`.

`zaf_present_rect()` moves from `dwm.cpp:208-216`: `zaf_painter_target(screen)`,
`zaf_paint_blit(back, rect)`, then switch back to `back`.

- [ ] **Step 3: Makefile wiring**

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

Add `ZAF_STATS ?= 0` to the build parameter section and append `-DZAF_STATS=$(ZAF_STATS)` to
`ADE_CFLAGS`. First compile without calling anything and confirm `make out/aqua.zxe` passes.

- [ ] **Step 4: wire Desktop Window Manager to the compositor (no window policy changes)**

Keep the `dwm_window` table and `paint_window` inside `dwm.cpp` and replace these implementations:

| Old symbol | New implementation |
|---|---|
| `damage_list` / `damage_reset` / `damage_add_raw` / `damage_rect` | `zaf_compositor_damage*` |
| `paint_rect()` | split into `paint_background()` (wallpaper) and three layer callbacks for the menu bar/Dock/windows |
| `present_rect()` / `present_cursor()` / `paint()` | `zaf_compositor_frame()` |
| `window_damage_rect()` | kept, as the window layer bounds |

`dwm_init`: `zaf_compositor_init({.background = paint_wallpaper})`, register the menu bar layer
(order 10, bounds = menu bar rect) and the Dock layer (order 11, bounds = Dock rect).
The shell callback signature is `void(void)`, so add two one-line adapters in `dwm.cpp`:

```c
static void paint_menubar_layer(void *context, struct zaf_rect damage)
{
    (void)context; (void)damage;
    g_ops->paint_menubar();   /* the compositor has already set the clip */
}
```

The Dock layer works the same way. `paint_wallpaper(void *context, struct zaf_rect damage)` calls
`zaf_paint_blit(g_ops->wallpaper, damage)`, exactly matching the wallpaper branch of the old `paint_rect`.

`dwm_open`: register the window layer (order = `100 + z`), bounds = `window_damage_rect`.
`bring_to_front`: `zaf_compositor_layer_update(layer, bounds, 100 + z)` and
`layer_damage` on the old/new focused windows. `close_window`: `layer_remove` first, then clear the window slot.
`dwm_run`: replace the `paint()` call with `zaf_compositor_frame(mx, my, mouse_ready())`.

The `dwm_ops` structure stays unchanged (`wallpaper`, `menu_h`, `dock_h`, the three callbacks, `poll`).

- [ ] **Step 5: build and behaviour regression**

```bash
cd src
make clean && make -j4
timeout 40 make run-iso 2>&1 | tee ../logs/zaf-z1-task2.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-task2.png
```

Expected: the log shows `[zaf] compositor <W>x<H> back buffer ...`; the screenshot is
visually identical to `out/zaf-z1-baseline.png`; windows can be dragged/closed and the Dock reclaims the icon.

- [ ] **Step 6: checkpoint**

```bash
git diff --stat src/kernel src/boot        # expected: empty
git diff --stat                            # expected: only zaf/, Aqua Desktop Environment, Makefile
```

**Commit:** `feat(zaf): compositor with layers, damage-only present and cursor overlay`

---

## Task 3: WM core (window table / focus / z-order / hit test / drag)

**Files:**
- Create: `src/zaf/wm/zaf_wm.h`, `src/zaf/wm/zaf_wm.c`
- Modify: `src/Makefile` (`ZAF_C_SRCS` gains wm)
- Modify: `src/apps/ADE/winmgr/dwm.cpp` (window state moves to `zaf_wm`, making it an adapter layer)

**Interfaces:** see spec 5.5.

- [ ] **Step 1: write `zaf_wm`**

Move from `dwm.cpp`: `g_windows`/`g_focus`/`g_next_z` (becoming internal state in `zaf_wm.c`),
`window_at()` (→ `zaf_wm_at`), `bring_to_front()` (→ `zaf_wm_focus` + the halving rule), the position
clamping and slot allocation in `dwm_open` (→ `zaf_wm_open`), and the drag state (→ `zaf_wm_drag_*`).
No drawing at all, no reference to `render.h`.

Window ids use `id = ++g_next_id` and increase monotonically, which helps logging and debugging; `context` stays untouched.

- [ ] **Step 2: turn Desktop Window Manager into an adapter layer**

- Delete the `dwm_window` structure and `g_windows`; keep `int g_window_layer[ZAF_MAX_WINDOWS]`.
- `dwm_open`: `zaf_wm_open(app, frame, bounds)` → take the index → register the layer →
  `app->init()` → `zaf_compositor_layer_damage`.
- `close_window`: remove the layer by index before `zaf_wm_close`.
- `bring_to_front`: update the layer by the new `order` after `zaf_wm_focus`.
- Drag: `zaf_wm_drag_begin` in `handle_press`; `zaf_wm_drag_to` in the drag branch of `dwm_run`,
  with the bounds used for clamping still computed in Desktop Window Manager (menu bar/Dock constraints are shell policy).
- `dwm_content_rect` → thin wrapper around `zaf_wm_content` (returns `struct render_rect`).
- Close-button hit testing still belongs to Desktop Window Manager/decoration (Task 4 moves it to `ade_decoration_close_hit`).

- [ ] **Step 3: build and behaviour regression**

```bash
cd src
make clean && make -j4
timeout 40 make run-iso 2>&1 | tee ../logs/zaf-z1-task3.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-task3.png
```

Expected: desktop behaviour the same as Task 2; no `[zaf]` errors in the log; both client windows are still interactive.

- [ ] **Step 4: checkpoint**

```bash
git diff --stat src/kernel src/boot        # expected: empty
grep -n "g_windows\|g_next_z\|window_at(" src/apps/ADE/winmgr/dwm.cpp
# expected: no output (window state now lives only in zaf/wm)
```

**Commit:** `feat(zaf): window table, z-order, focus and drag core`

---

## Task 4: standalone window decoration + Aqua tokens

**Files:**
- Create: `src/zaf/theme/zaf_theme.h`, `src/zaf/theme/zaf_theme_aqua.c`
- Create: `src/apps/ADE/decoration.h`, `src/apps/ADE/decoration.cpp`
- Modify: `src/Makefile` (`ZAF_C_SRCS` gains theme; `ADE_CXX_SRCS` gains decoration)
- Modify: `src/apps/ADE/winmgr/dwm.cpp` (delete the decoration drawing, call `ade_decoration_*`)

**Interfaces:** see spec 5.6 and 5.7.

- [ ] **Step 1: write the theme tokens**

The values in `zaf_theme_aqua.c` come item by item from `dwm.cpp:110-165` and `dwm.cpp:312-320`:

| Token | Value | Source |
|---|---|---|
| `window_border` | `0x4A6072` | `paint_window` outer frame |
| `window_face` | `0xF4F7FA` | window background colour |
| `title_top` / `title_bottom` | `0xFDFEFF` / `0xC9DBEC` | title gradient |
| `title_shine` | `0xFFFFFF` (alpha 60) | title highlight |
| `title_separator` | `0x8EA2B6` | separator under the title |
| `title_text` | `0x1B2C3D` | title text |
| `close_light[]` | `0xE9685C, 0xF0BE4E, 0x62C34E` | traffic lights |
| `close_border[]` | `0xA53A30, 0xA87B24, 0x3D8A2E` | traffic light outlines |
| `window_radius` / `window_inner_radius` | `12` / `11` | corner radius |
| `title_height` | `32` | the unscaled value of `dwm_ui(32)` |
| `shadow_spread` | `9` | `render_soft_shadow` |
| `close_radius` | `7` | traffic light radius |
| `close_spacing` | `20` | traffic light spacing |
| `close_inset` | `20` | left inset |
| `content_inset` | `2` | content area border |

Every pixel metric is scaled through `ade_ui()` / `dwm_ui()` following project convention; the tokens store unscaled values.

- [ ] **Step 2: write `decoration.cpp`**

Move `traffic_lights()`, the decoration part of `paint_window()`, `close_button_at()` and
`window_damage_rect()` out of `dwm.cpp`, with signatures per spec 5.7:

- `ade_decoration_paint()`: shadow → outer frame → inner fill → title gradient/highlight/separator →
  traffic lights (alpha adjusted by focused) → title text.
- `ade_decoration_content_rect()`: identical to `dwm_content_rect`.
- `ade_decoration_damage_rect()`: expand the window rect outward by `dwm_ui(14)` (shadow + margin);
  it must match the old `window_damage_rect`, otherwise dragging leaves trails.
- `ade_decoration_close_hit()`: circle radius = `dwm_ui(7) + dwm_ui(3)`, centres identical to the old logic.

- [ ] **Step 3: Desktop Window Manager calls the decoration**

`paint_window` is left with only:

```c
zaf_painter_clip(damage);
ade_decoration_paint(frame, window->app->name, focused, damage);
struct zaf_rect content = zaf_wm_content(window);
zaf_painter_clip(zaf_rect_intersect(damage, content));
window->app->paint(&view);
```

`dwm_title_height()` returns `ade_decoration_title_height()`;
`dwm_window_rect()` returns `ade_decoration_damage_rect()`.
`zaf_wm_metrics` is initialised with `ade_decoration_title_height()`.

- [ ] **Step 4: build and visual regression**

```bash
cd src
make clean && make -j4
ruby tools/screenshot.rb 3.0 out/zaf-z1-task4.png
```

Expected: the screenshot is pixel-identical to the baseline (every decoration value is moved, not re-set).
If ImageMagick is available: `compare -metric AE out/zaf-z1-baseline.png
out/zaf-z1-task4.png /dev/null` should report 0; otherwise check them side by side in an image viewer.

**Commit:** `feat(zaf): split window decoration into an Aqua-themed module`

---

## Task 5: statistics logging and full regression

**Files:**
- Modify: `src/Makefile` (`ZAF_STATS ?= 0`, already added by Task 2; verify it here)
- Modify: `src/apps/ADE/winmgr/dwm.cpp` (print one statistics line per second)

- [ ] **Step 1: statistics output**

With `ZAF_STATS=1`, `dwm_run` prints once a second:

```c
kprintf("[zaf] frames=%lu damage=%lu rects=%lu pixels=%lu\n",
        zaf_compositor_frame_count(), ..., ...);
```

The default `ZAF_STATS=0` prints nothing (consistent with the existing `SCHED_STATS` convention).

- [ ] **Step 2: run the acceptance matrix (section 9 of the spec)**

```bash
cd src
make clean && make -j4
nm -u out/kernel.elf                                   # no output
git diff --stat src/kernel src/boot                    # empty
timeout 40 make run-iso ZAF_STATS=1 2>&1 | tee ../logs/zaf-z1-final.log
ruby tools/screenshot.rb 3.0 out/zaf-z1-final.png
```

Log check items:

- `Kernel: /install/KERNEL.ELF from the ISO.`
- `Font: /install/WQY.TTC from the ISO.`
- `[ade ] Ring3 display server`
- `spawn /bin/apps/welcome.zxe`, `calc.zxe`, `install.zxe`
- `[zaf]` frame statistics: while the mouse is not moving `rects` should be 0 (frames without damage are never presented)
- clicking the welcome window echoes `已收到点击事件`
- the calculator shows `12` for `7 + 5 =`, and the Dock icon disappears after closing it

- [ ] **Step 3: damage-specific check (manual, QEMU gtk)**

```bash
cd src
make run-iso ZAF_STATS=1 QEMU_DISPLAY="-display gtk"
```

- Mouse movement only: every frame in the log has `rects ≤ 4` and `pixels` no larger than two cursor
  bounding boxes (19×20×2 ≈ 760 pixels).
- Dragging a window: `pixels` approaches the union of the old and new window bounding boxes rather than the full screen (1920×1080).
- Idle for 2 seconds: no new frames are printed (when `rects=0`, `frame()` returns immediately).

- [ ] **Step 4: exception and SMP regression**

```bash
timeout 40 make run-iso QEMU_SMP=4 2>&1 | tee ../logs/zaf-z1-smp.log
```

Expected: `4/4 CPU(s) online`; the desktop works normally; a client fault terminates only that process and Aqua Desktop Environment keeps running.

- [ ] **Step 5: checkpoint**

```bash
git status --porcelain
git diff --stat
```

**Commit:** `test(zaf): damage statistics and the Z1 regression matrix`

---

## Task 6: documentation sync and wrap-up

**Files:**
- Modify: `AGENTS.md` (the "desktop layering" item: damage composition belongs to the ZAF compositor, Desktop Window Manager owns policy/decoration/input)
- Modify: `docs/ARCHITECTURE.md` (ZAF milestone status; add `src/zaf/` to the Ring3 boundary)
- Modify: `docs/superpowers/specs/2026-11-13-zaf-architecture-design.md` (status and Z1 link)
- Modify: `docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md` (change the status to "implemented")
- Modify: `src/apps/ADE/ARCHITECTURE.md` (add decoration.cpp to the directory tree, note that the compositor belongs to ZAF)
- Possibly needed: a ZAF row in the "Architecture" table of `README.md` (rule 13: README only describes the
  current release, and Z1 is not a release, so **do not add it**; update it with the release when the version changes)

- [ ] **Step 1: update the docs** (check each item against the actual implementation; never write "done" ahead of time)
- [ ] **Step 2: full self-check**

```bash
cd src
make clean && make -j4
git diff --stat src/kernel src/boot        # empty
```

- [ ] **Step 3: checkpoint**

```bash
git status --porcelain
git diff --stat
```

**Commit:** `docs(zaf): sync architecture docs with the Z1 compositor`

---

## Commit sequence

| # | Commit | Content |
|---|---|---|
| 1 | `feat(zaf): bounded damage regions and rectangle core with host tests` | Task 1 |
| 2 | `feat(zaf): compositor with layers, damage-only present and cursor overlay` | Task 2 |
| 3 | `feat(zaf): window table, z-order, focus and drag core` | Task 3 |
| 4 | `feat(zaf): split window decoration into an Aqua-themed module` | Task 4 |
| 5 | `test(zaf): damage statistics and the Z1 regression matrix` | Task 5 |
| 6 | `docs(zaf): sync architecture docs with the Z1 compositor` | Task 6 |

Local commits only, no push; all three remotes (GitHub main / Gitee master / GitCode main) stay
unchanged until the user confirms a release.

## Rollback strategy

- **Task 2 fails**: the `background` callback of `zaf_compositor` is the full-screen rollback switch; set
  `paint_background` to the old full `paint_rect` logic and register no layers, which restores Z0 behaviour.
- **Task 3 fails**: keep the `dwm_window` table in Desktop Window Manager and treat `zaf_wm` as an unused library;
  just delete the `zaf/wm` sources and the Makefile entries.
- **Task 4 fails**: decoration drawing goes back into `dwm.cpp`; keep the theme files but do not reference them.
- If any step leaves the desktop unable to boot: `git revert` the corresponding commit and return to the verified state of the previous Task.
