# ZAF Z1: Compositor and Window Management Design

Date: 2026-11-14
Status: Design finalized; implementation awaits the toolchain (this machine has no WSL2, see section 10)
Related:
- Parent design: [2026-11-13-zaf-architecture-design.md](2026-11-13-zaf-architecture-design.md)
- Implementation plan: [2026-11-14-zaf-z1-compositor.md](../plans/2026-11-14-zaf-z1-compositor.md)
- Existing implementation: `src/apps/ADE/winmgr/dwm.cpp`, `src/kernel/gfx/render.c`, `src/user/lib/include/zaf_gfx.h`

## 1. Goals and acceptance

Z1 moves the window/compositing behavior already proven inside Aqua Desktop Environment into ZAF's layering while
**not changing how the desktop looks, not changing the window protocol, and not changing the kernel**. Acceptance
surface:

1. Existing desktop behavior is equivalent item by item: wallpaper, menu bar, Dock, window stacking/focus/dragging/closing, software
   cursor, and both client windows (Welcome, Calculator) are all interactive.
2. Damage commits only the changed regions: cursor movement commits only the cursor bounding box (two blocks of roughly 19×20); dragging a window
   commits only the union of the old and new window bounding boxes; with `ZAF_STATS=1` the serial port prints the per-frame damage count and pixel count.
3. Kernel and boot layer binaries are unchanged: `git diff --stat src/kernel src/boot` is empty and
   `nm -u out/kernel.elf` still reports no undefined symbols.
4. Leaves stable interfaces for Z2 (widget system) and Z3 (animation/theme): widgets depend only on `zaf_painter`
   and theme tokens, not on Aqua Desktop Environment internal headers.

## 2. Current state (the actual code after Z0)

| Location | Responsibility today | Lines |
|---|---|---|
| `src/apps/ADE/main.cpp` | Process entry, `ade_app` registry, render API forwarding, launching Aqua Desktop Environment | 234 |
| `src/apps/ADE/winmgr/dwm.cpp` | Damage list, back buffer, wallpaper/menu bar/Dock/window compositing, cursor, hit testing, z-order, focus, dragging, close, input, main loop | 627 |
| `src/apps/ADE/display/server.cpp` | `ade` port, client slots, command stream replay, event return | 323 |
| `src/apps/ADE/wallpaper.cpp` | Wallpaper (rule 33 image, integer scaling) and the Aqua gradient fallback | 67 |
| `src/apps/ADE/menubar.cpp` / `dock.cpp` | Desktop shell drawing and hit testing | 234 |
| `src/kernel/gfx/render.c` | ZOS API 1.0 software rendering: target surface, clipping, primitives, text, SVG, cursor | 608 |
| `src/user/lib/include/zaf_gfx.h` + `zaf_gfx_soft.c` | Z0's backend-neutral raster interface and software implementation (rect/rounded/line/polygon/alpha) | 46 + 205 |

The problem is not missing features but overlapping responsibility: a single Desktop Window Manager file holds the **compositor**
(damage, back buffer, present, cursor), **window policy** (z-order, focus, hit testing, dragging), **window decoration painting**
(rounded corners, title bar, traffic lights, shadows) and **desktop shell scheduling** (wallpaper, menu bar, Dock) at once.
Z1 splits these apart following the parent design.

## 3. Design decisions

### D1: Z1 is a Ring3 static library; it does not enter the kernel and adds no syscalls

All ZAF Z1 code lives in `src/zaf/` and links into `/bin/aqua` as ordinary object files, in the same process as the existing
`main.cpp`/`dwm.cpp`. Z1 introduces no new `.zxe` dynamic library, adds no syscalls, and does not touch `kernel/` or `boot/`.
Shipping ZAF as a shared library is Z4's job, versioned against the ZXE dynamic ABI at that point, which avoids carrying
unverifiable link risk today just for the sake of "library form".

### D2: The compositing backend is the ZOS rendering API for now (no Skia)

The parent design names Skia as the final rendering foundation while also stating the fallback: keep the rendering interface unchanged,
build a Raster backend first. Z0 already delivered the fallback as `zaf_gfx` (software raster) + `zaf_gfx_demo` (frame-rate smoke).
Z1 does not expand `zaf_gfx`'s responsibilities; instead:

- **Compositing and desktop drawing go through the existing `render.c`** (ZOS API 1.0). It is the only drawing interface
  Aqua Desktop Environment, Desktop Window Manager and applications use today; the kernel and Ring3 share the same implementation,
  and text/shadows/SVG/cursor are all proven.
- `zaf_gfx` stays as the **portable raster** and the backend for the Z0 demo; when Skia adaptation is actually greenlit (a separate
  spike requiring a user decision), that is when to decide whether to replace `render.c` or let ZAF own both backends.
- Z1's only requirement is that ZAF internally funnels its `render_*` calls into a thin wrapper in `zaf/gfx/`
  (`zaf_painter`), so switching backends later needs no changes above it. **But Z1 does not implement a multi-backend vtable**:
  a vtable with only one backend is pure overhead (rule 20, KISS); the wrapper starts as a plain set of functions.
- The existing Aqua Desktop Environment shell (`wallpaper.cpp`/`menubar.cpp`/`dock.cpp`/`icons.cpp`/`about.cpp`)
  may still call `render_*` directly during Z1 (they are already stable and are not ZAF code); new ZAF code and
  the decoration module must go through `zaf_paint_*`. Migrating the shell onto the painter is mechanical work for Z4.

Cost: Z1's ZAF still depends on the ZOS rendering API and cannot be ported to another host immediately. That is acceptable,
because the parent design lists portability as an interface-isolation concern rather than a Z1 deliverable.

### D3: The compositor owns layers/damage/present/cursor; Desktop Window Manager owns window policy

- `zaf/compositor`: screen/back buffer, bounded damage regions, compositing layers by `order`, damage-only
  present, software cursor overlay. It **does not know** about "windows", "title bars" or the "Dock"; it only knows
  "layers with a bounding box and a paint callback".
- `zaf/wm`: window table (at most 8), focus, z-order, hit testing, drag state machine. It paints **no**
  pixels; it only answers "which window is on top, where should this drag go, who was clicked".
- `dwm.cpp`: keeps the Desktop Window Manager product name and becomes a thin adaptation layer — mapping Aqua Desktop Environment's `ade_app`
  to ZAF's window/layer, forwarding input and timers, and delegating window decoration painting to `ade_decoration_*`.
- The desktop shell (wallpaper, menu bar, Dock) remains the Aqua Desktop Environment `shell` responsibility, registered with the compositor
  as layers; the compositor only decides their order.

This needs one sync with AGENTS.md's current wording that "Desktop Window Manager exclusively owns window decoration, z-order, drag/close,
focus, damage compositing and input": **damage compositing belongs to the ZAF compositor, Desktop Window Manager keeps policy and input**. When Z1
completes, update that AGENTS.md entry and `docs/ARCHITECTURE.md`.

### D4: Window decoration becomes its own file; colors/sizes come from Aqua tokens

The magic numbers in `dwm.cpp` — corner radii 12/11, title gradient `0xFDFEFF → 0xC9DBEC`, the three traffic-light colors, shadow
spread 9, title height 32 — move to `zaf/theme/zaf_theme_aqua.c`, and decoration painting moves to
`apps/ADE/decoration.cpp`. Z1's token table is a **compile-time constant** covering only the fields window decoration
needs; the full theme engine (theme package loading, runtime switching) is Z3, and Z1 will not ship a half-finished version of it.

### D5: Damage regions keep the existing bounded algorithm; no region tree

The existing algorithm (from `dwm.cpp:58`): at most 16 rectangles; a new rectangle merges with an existing one when they are within
2px; on overflow the first rectangle grows into the overall bounding box. Z1 moves it over verbatim to `zaf/core/zaf_damage.c`, adding only unit
tests and invariant comments. Reason: the algorithm is simple and its behavior is verified on real hardware; region trees/exact boolean
operations are an optimization Z3's animation compositing needs (rule 20).

### D6: Layer and window limits keep the current scale

`ZAF_MAX_LAYERS = 16` (background 1 + menu bar 1 + Dock 1 + windows 8 + headroom 5),
`ZAF_MAX_WINDOWS = 8` (equal to the existing `DWM_MAX_WINDOWS`). No early expansion.

## 4. Layering and file responsibilities (rule 23: one file does one thing)

```
src/zaf/
├── README.md                     Module map, boundaries and build integration
├── core/
│   ├── zaf_rect.h                Pure data rects + inline geometry (no dependencies, host-testable)
│   ├── zaf_rect.c                Rect intersect/union/empty/clip
│   ├── zaf_damage.h              Bounded damage region type and invariants
│   └── zaf_damage.c              Merge/overflow/iterate
├── gfx/
│   ├── zaf_painter.h             ZOS wrapper for desktop drawing (fill/blend/round/grad/
│   │                             shadow/blit/text/text_width/cursor)
│   └── zaf_painter.c             The single implementation of those wrappers (calls render_*)
├── compositor/
│   ├── zaf_compositor.h          Compositor public interface: init, layers, damage, one frame
│   ├── zaf_compositor.c          Layer table + per-frame drawing order
│   ├── zaf_present.c             Back buffer → screen damage-only present
│   └── zaf_cursor.c              Software cursor save/restore/present (reuses render_cursor*)
├── wm/
│   ├── zaf_wm.h                  Window table, focus, z-order, hit testing, dragging
│   └── zaf_wm.c                  The single implementation of that state
└── theme/
    ├── zaf_theme.h               Aqua token struct (colors/metrics/animation durations reserved)
    └── zaf_theme_aqua.c          The Aqua defaults Z1 uses
```

On the Aqua Desktop Environment side:

```
src/apps/ADE/
├── main.cpp                      Entry + ade_app registry + render forwarding (kept)
├── decoration.cpp/.h             Window decoration painting (new, uses Aqua tokens)
├── wallpaper.cpp / menubar.cpp / dock.cpp / icons.cpp / about.cpp (kept)
├── winmgr/dwm.cpp                Thin adaptation layer: ZAF WM/compositor ↔ Aqua Desktop Environment shell and input
├── winmgr/dwm.h                  Public Desktop Window Manager API (essentially unchanged)
└── display/server.cpp            Window protocol and command stream (unchanged)
```

`src/zaf/README.md` records each file's "does one thing" declaration; before adding a file in later tasks, read the
map first, so "one file managing both compositing and window policy" does not come back.

## 5. Interfaces

The following headers are Z1's contract. Implementation lands in Tasks 1–4 of the plan; this section is authoritative for signatures.

### 5.1 `zaf/core/zaf_rect.h`

Pure data, no system header dependencies, so host unit tests and future porting stay easy:

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
int zaf_rect_width(struct zaf_rect r);   /* x1 - x0, 0 for an empty rect */
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
/* Adjacent rects within 2px may merge; past ZAF_DAMAGE_MAX, collapse into the overall bounding box. */
void zaf_damage_add(struct zaf_damage *damage, struct zaf_rect rect,
                    struct zaf_rect bounds);
void zaf_damage_add_full(struct zaf_damage *damage, struct zaf_rect bounds);
int zaf_damage_empty(const struct zaf_damage *damage);
int zaf_damage_count(const struct zaf_damage *damage);
struct zaf_rect zaf_damage_item(const struct zaf_damage *damage, int index);

#endif
```

Invariants (written into the comment at the top of the `.c`): every item has been intersected with `bounds` and is non-empty;
`count <= ZAF_DAMAGE_MAX`; after merging, no two items are adjacent (within the 2px tolerance).

### 5.3 `zaf/gfx/zaf_painter.h`

Wraps only the `render_*` calls Aqua Desktop Environment/Desktop Window Manager actually use today; it is not a complete repaint API:

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

`zaf_painter.c` is the only ZAF file allowed to call `render_*` directly; Desktop Window Manager and decoration paint only through
`zaf_paint_*` (Aqua Desktop Environment shell migration is covered in D2). `zaf_gfx.h` stays as it is and is not referenced by Z1.

### 5.4 `zaf/compositor/zaf_compositor.h`

```c
#ifndef ZAF_COMPOSITOR_H
#define ZAF_COMPOSITOR_H

#include <render.h>
#include "core/zaf_rect.h"
#include "core/zaf_damage.h"

#define ZAF_MAX_LAYERS 16
#define ZAF_NO_LAYER (-1)

/* The callback receives a rect already clipped to "damage ∩ layer bounding box"; all coordinates are absolute screen coordinates. */
typedef void (*zaf_layer_paint_fn)(void *context, struct zaf_rect damage);

struct zaf_compositor_ops {
    zaf_layer_paint_fn background;   /* Wallpaper; always painted first; may be NULL */
    void *background_context;
};

int  zaf_compositor_init(const struct zaf_compositor_ops *ops);
void zaf_compositor_shutdown(void);
struct render_surface *zaf_compositor_back(void);

/* Returns a slot; a smaller order is further back. The layer bounds should include the extra pixels of shadows/decoration. */
int  zaf_compositor_layer_add(void *context, zaf_layer_paint_fn paint,
                              struct zaf_rect bounds, int order);
void zaf_compositor_layer_remove(int layer);
void zaf_compositor_layer_update(int layer, struct zaf_rect bounds, int order);
void zaf_compositor_layer_visible(int layer, int visible);
void zaf_compositor_layer_damage(int layer);

void zaf_compositor_damage(struct zaf_rect rect);
void zaf_compositor_damage_full(void);
int  zaf_compositor_damaged(void);

/* Composite one frame and present by damage; when cursor_visible is 0, restore the old cursor and stop drawing it.
 * Returns the number of damage rects presented this frame (0 when there is no damage and the cursor did not move). */
int  zaf_compositor_frame(int cursor_x, int cursor_y, int cursor_visible);

/* Statistics (printed periodically by Desktop Window Manager when ZAF_STATS=1). */
unsigned long zaf_compositor_frame_count(void);
unsigned long zaf_compositor_damage_rect_count(void);
unsigned long zaf_compositor_present_pixels(void);

#endif
```

The order within one frame (matching current Desktop Window Manager behavior item by item):

1. If there is no damage and the cursor position/visibility is unchanged, return 0 directly.
2. Switch to the back buffer; `zaf_paint_cursor_restore()` restores the previous frame's cursor pixels.
3. Snapshot this frame's damage rects; for each damage rect: paint the background first, then visible layers in ascending `order`,
   setting the clip to "damage ∩ bounds" before each layer and intersecting again with the layer bounds before painting.
4. If the cursor is visible, paint it into the back buffer.
5. Present each damage rect from the back buffer to the screen (`render_blit`), then present the cursor bounding box
   (old position + new position); cursor pixels are likewise copied from the back buffer to the screen.

### 5.5 `zaf/wm/zaf_wm.h`

```c
#ifndef ZAF_WM_H
#define ZAF_WM_H

#include "core/zaf_rect.h"

#define ZAF_MAX_WINDOWS 8
#define ZAF_NO_WINDOW (-1)

struct zaf_wm_metrics {
    int title_height;   /* Decoration title bar height */
    int min_width;
    int min_height;
};

struct zaf_window {
    int used;
    int id;
    struct zaf_rect frame;   /* Includes the title bar; excludes the shadow */
    int order;
    int drag_dx;
    int drag_dy;
    void *context;           /* Desktop Window Manager stores the ade_app pointer here */
};
typedef struct zaf_window zaf_window;

void zaf_wm_init(const struct zaf_wm_metrics *metrics);

/* Open a window; frame is the suggested position/size and is clamped into bounds when out of range. */
zaf_window *zaf_wm_open(void *context, struct zaf_rect frame,
                        struct zaf_rect bounds);
void zaf_wm_close(zaf_window *window);
zaf_window *zaf_wm_find(void *context);

zaf_window *zaf_wm_at(int x, int y);        /* Topmost hit, NULL if none */
zaf_window *zaf_wm_focused(void);
void zaf_wm_focus(zaf_window *window);      /* Raise to the top and set focus */
void zaf_wm_cycles(zaf_window *window);     /* Halve the z-order when it grows too large (current logic kept) */

void zaf_wm_drag_begin(zaf_window *window, int x, int y);
void zaf_wm_drag_to(zaf_window *window, int x, int y, struct zaf_rect bounds);
void zaf_wm_drag_end(zaf_window *window);
int  zaf_wm_count(void);
zaf_window *zaf_wm_at_index(int index);

/* Content area: frame minus the title bar and the 2px border on each side. */
struct zaf_rect zaf_wm_content(const zaf_window *window);

#endif
```

### 5.6 `zaf/theme/zaf_theme.h` and `zaf_theme_aqua.c`

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
    /* For the Z3 animation engine; Z1 only reserves the slots and never reads them. */
    int animation_ms_fast;
    int animation_ms_normal;
};

const struct zaf_theme *zaf_theme_aqua(void);

#endif
```

The values in `zaf_theme_aqua.c` must match today's `dwm.cpp` constants verbatim (colors, radii, spacing,
shadows); otherwise screenshot comparisons will show differences.

### 5.7 Aqua Desktop Environment `decoration.h`

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

## 6. Data flow (one frame)

```
Desktop Window Manager main loop
  ├─ console_read_char() / mouse_*()        ← input (unchanged)
  ├─ hit/drag/close → zaf_wm_*              ← policy (extracted by Z1)
  ├─ shell mouse state changes → zaf_compositor_damage() (Dock hover etc.)
  ├─ display_poll()                         ← client command stream (unchanged)
  ├─ timer tick → shell tick / focused app tick (unchanged)
  └─ zaf_compositor_frame(mx, my, ready)
        ├─ back buffer: cursor restore → background → layers (by order)
        ├─ back buffer: paint cursor
        └─ screen: blit damaged back buffer regions + cursor bounding box
```

## 7. Migration steps (see the plan for details)

| Task | Content | Behavior change |
|---|---|---|
| 1 | ZAF core: rect + damage + host unit tests | None (new files) |
| 2 | Painter wrapper + compositor (layers/damage/present/cursor), taking over the compositing half of Desktop Window Manager first | Visuals should be exactly identical |
| 3 | WM core (window table/focus/z-order/hit testing/drag), Desktop Window Manager becomes thin | Visuals should be exactly identical |
| 4 | Window decoration split out + Aqua tokens, colors/sizes no longer scattered | Visuals should be exactly identical |
| 5 | Build integration, statistics logging, regression and screenshot comparison | None |
| 6 | Documentation sync (AGENTS.md, docs/ARCHITECTURE.md, ZAF doc status) | None |

Task 2's `background` callback is the fallback switch: if the compositor misbehaves, stuff the whole-screen drawing into
`background` and add no layers, and it degrades to "Desktop Window Manager drawing exactly as before", with no code rollback.

## 8. Non-goals

- Widget system (Button/TextEdit/List), event dispatch tree, layout: Z2.
- Animation engine, theme package loading, runtime theme switching: Z3.
- Skia port and multi-backend vtable: a separate spike, Z0.5, needs a user decision.
- Window protocol extensions (resize, minimize, bitmap submission, shared-memory command ring): Z2/Z3 as needed.
- Multiple monitors, DPI scaling model, accessibility: longer term.
- Any kernel, boot, driver or filesystem change.

## 9. Acceptance matrix

| # | Check | Pass criteria |
|---|---|---|
| 1 | Clean build | `make clean && make -j4` succeeds; `nm -u out/kernel.elf` shows no undefined symbols |
| 2 | Zero kernel changes | `git diff --stat src/kernel src/boot` is empty |
| 3 | ISO boot | `make run-iso` prints the ISO kernel/font log and `[ade ] Ring3 display server`, then goes straight to the desktop |
| 4 | Two clients | The log shows welcome/calc/install spawn; clicking the welcome window echoes, `7 + 5 = 12`, and the Dock reclaims the icon after close |
| 5 | Cursor damage | While the mouse moves, `ZAF_STATS=1` shows ≤ 4 damage rects per frame and present pixels ≈ cursor bounding box area |
| 6 | Drag damage | While dragging a window, present pixels ≈ the union of the old and new window bounding boxes, not the whole screen |
| 7 | Visual regression | QEMU HMP `screendump` frames compared against the Z0 baseline: no difference in menu bar/Dock/window decoration/fonts |
| 8 | User exception | A client crash kills only that process; Aqua Desktop Environment keeps running and the Dock reclaims the icon |
| 9 | SMP | `make run-iso QEMU_SMP=4` desktop behavior matches single-core (Aqua Desktop Environment is single-threaded, no concurrency introduced) |
| 10 | Documentation | AGENTS.md's Desktop Window Manager responsibilities, docs/ARCHITECTURE.md milestones and the ZAF doc status agree |

## 10. Environment prerequisites

The Windows machine this session runs on **has no WSL2 and no gcc/make/ruby/qemu** (`wsl.exe`
reports it is not installed). This design document and the plan can therefore land first, but compiling, QEMU runs and
screenshot verification for Tasks 1–5 must happen on a machine with WSL2 + Ubuntu 26.04 and the dependencies listed in AGENTS.md.
Until the toolchain is available, only changes that need no compiling (docs/scripts/config) may be committed.

## 11. Risks and rollback

1. **A compositor rewrite causes visual regressions**: Task 2 keeps the "whole-screen background as a single layer" fallback path;
   every step compares screendumps against the baseline, and a failing step stops at the previous task.
2. **render.c's global state (target/clip/cursor) gets scrambled by the new call order**: ZAF only accesses it through
   `zaf_painter`, and the painter sets target/clip explicitly every frame with no implicit assumptions; unit tests
   cover the pure logic (rect/damage), while rendering relies on screenshots.
3. **Layer bounding boxes miss the shadow**: `dwm.cpp`'s current `window_damage_rect` adds a
   `dwm_ui(14)` inset (shadow 9 + headroom); the decoration module must provide the same expanded rect,
   otherwise dragging/closing leaves shadow trails.
4. **Window table and layer slots drift out of sync**: the Desktop Window Manager adaptation layer is the only place holding both mappings;
   when a window closes, delete the layer before freeing the window slot, and write that order into code comments and test steps.
5. **Nothing can be verified without the toolchain**: no kernel/boot/driver changes, so risk is confined to Ring3 Aqua Desktop Environment; worst
   case the desktop fails to start, and the rollback is Z0's single-file `dwm.cpp` implementation.
