# src/zaf — Zhaoxu Aqua Framework

ZAF is the GUI framework beneath the Aqua Desktop Environment and Desktop Window Manager. During Z1 it is Ring3 static code linked into
`/bin/aqua` (not the kernel, not a `.zxe` shared library).

- Architecture and milestones: [docs/superpowers/specs/2026-11-13-zaf-architecture-design.md](../../docs/superpowers/specs/2026-11-13-zaf-architecture-design.md)
- Z1 design: [docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md](../../docs/superpowers/specs/2026-11-14-zaf-z1-compositor-design.md)
- Z1 plan: [docs/superpowers/plans/2026-11-14-zaf-z1-compositor.md](../../docs/superpowers/plans/2026-11-14-zaf-z1-compositor.md)

## Layers

```
apps/ADE           desktop shell (wallpaper/menu bar/Dock/decorations) + display server + Desktop Window Manager adapter
        │
src/zaf/wm         window table, focus, z-order, hit testing, drag state machine (no pixels)
src/zaf/compositor layers, damage, back buffer, damage-only present, software cursor
src/zaf/theme      Aqua tokens (colors/metrics; Z3 expands into a theme engine)
src/zaf/gfx        painter: the only wrapper over the ZOS rendering API
src/zaf/core       rectangles and bounded damage regions (no system dependencies, host-testable)
        │
render.c           ZOS API 1.0 software rendering (the same copy in kernel and Ring3)
```

`zaf_gfx.h` / `zaf_gfx_soft.c` (the Z0 portable rasterizer and fps demo) are not part of this chain:
they are placeholder implementations for Skia/other host backends; Z1 does not reference them or expand their duties.

## Directory and file responsibilities (rule 23)

| File | Does exactly one thing |
|---|---|
| `core/zaf_rect.h/.c` | Rectangle data and intersection/union/area |
| `core/zaf_damage.h/.c` | Bounded damage region (up to 16 rectangles, merges within 2px, overflow becomes the bounding box) |
| `gfx/zaf_painter.h/.c` | Wraps `render_*` as `zaf_paint_*`; the only ZAF file that calls `render_*` directly |
| `compositor/zaf_compositor.h/.c` | Layer table and per-frame draw order |
| `compositor/zaf_present.c` | Damage present from the back buffer to the screen |
| `compositor/zaf_cursor.c` | Software cursor save, restore, and present |
| `wm/zaf_wm.h/.c` | Window table, focus, z-order, hit testing, dragging (no drawing) |
| `theme/zaf_theme.h` | Theme token structures |
| `theme/zaf_theme_aqua.c` | Aqua default tokens (values must match the existing `dwm.cpp` constants) |

## Boundary rules

1. `core/` includes no system headers (host-testable, compiles directly).
2. `wm/` does not include `render.h` and does not touch pixels.
3. New ZAF code (including decorations) draws only through `zaf_paint_*`; `gfx/zaf_painter.c` is the only ZAF file that calls
   `render_*` directly. The existing Aqua Desktop Environment shell (wallpaper/menubar/dock/icons) keeps direct `render_*` calls
   during Z1; migration to the painter happens in Z4.
4. `compositor/` knows nothing of "windows", "title bars", or "Dock"; it knows only layers with bounding boxes and draw callbacks.
5. `src/apps/ADE/` includes nothing outside the private `zaf/*/` headers; ZAF does not include
   `apps/ADE/` in reverse.
6. One file does exactly one thing: add a row to this table before writing code for a new capability.

## Build integration

`ZAF_C_SRCS` in `src/Makefile` adds the ZAF sources to `ADE_C_SRCS`, and `-I.` makes
paths such as `#include "zaf/core/zaf_rect.h"` resolve. `ZAF_STATS=1` enables per-frame damage statistics
(default 0, no output).

## Status

| Stage | Content | Status |
|---|---|---|
| Z0 | `zaf_gfx` backend-neutral rasterizer + software implementation + fps demo | Implemented |
| Z1 | Compositor + window management + Aqua tokens + decoration split | Design complete, implementation pending (plan ready) |
| Z2 | Widget system, event dispatch, layout | Not started |
| Z3 | Animation engine, theme packs | Not started |
| Z4 | Full Aqua Desktop Environment/Desktop Window Manager migration, ZAF as a `.zxe` shared library | Not started |
