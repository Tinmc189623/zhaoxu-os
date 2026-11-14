# Zhaoxu Aqua Framework (ZAF) Architecture and Milestones

Date: 2026-11-13  
Status: Overall design; Z0 implemented (`src/user/lib/include/zaf_gfx.h`), Z1 design in
[2026-11-14-zaf-z1-compositor-design.md](2026-11-14-zaf-z1-compositor-design.md),
Z2–Z4 not started

## 1. Goals and Boundaries

ZAF is an independent GUI framework that presents the classic Aqua look by default while allowing the theme, or the whole visual style, to be replaced later. It uses Skia as the graphics rendering base and builds its own window manager, desktop compositor, widget system, animation engine and theme engine, reducing its dependence on the host operating system's GUI components.

Constraints:

- Runs in Ring3, reaching the framebuffer, input and IPC through ZOS API 1.0; it never enters the kernel (rule 9).
- The default theme is Aqua; themes are driven by data and style tokens rather than hard-coding appearance into widgets.
- Portable to a self-developed kernel: apart from ZOS API and the ZXE shared library ABI it depends on no Linux/macOS/Windows GUI component.
- The repository currently holds only `third_party/stb` and no Skia source at all; adopting Skia is the first external dependency decision in this plan.

Non-goals (first phase): GPU-accelerated composition, multi-monitor hotplug, accessibility trees, full internationalised text layout.

## 2. Layering

```
application / client (.zxe, depends on the ZAF library)
        │  widget tree + events
ZAF widget system  widgets/   buttons, text fields, lists, menus, scroll containers
ZAF animation engine  anim/   timelines, easing, property animation, interruptible transitions
ZAF theme engine  theme/      style tokens, metrics, image assets, theme package loading
ZAF window management  wm/    window stacking, focus, move/resize, minimise
ZAF compositor    compositor/ layers, damage regions, shadows/blur and similar effects
ZAF render backend  gfx/      Skia Surface/Canvas wrapper and glyph provider
        │  Skia (third party)
ZOS API 1.0: framebuffer mapping, mouse/keyboard ports, shared memory, timers
```

Each directory carries exactly one class of responsibility (rule 23). `Aqua Desktop Environment` and `Desktop Window Manager` keep their existing product names: Desktop Window Manager as the window manager implementation, Aqua Desktop Environment as the desktop shell (wallpaper, menu bar, Dock). Both are built on ZAF, not the other way round.

## 3. Skia Integration Strategy

- Vendor `third_party/skia` at a pinned version, compiling only the necessary subset of `core` + `pathops` + `codec` plus the CPU-only Raster backend; Ganesh/Vulkan, X11, CoreText, DirectWrite, font managers and other platform backends stay disabled.
- Fonts come from ZAF's `SkFontMgr`/`SkTypeface` adapters, reusing the repository's existing `stb_truetype` and system font loading paths (MiSans/MiSans Global/Inter, falling back to WQY.TTC), so FreeType is not pulled in as well.
- The platform abstraction exposes only three kinds of callbacks: surface submission (copy into the framebuffer or a shared-memory surface), a monotonic clock, and an event source. Porting to another OS replaces just those three.
- Risk: Skia depends on a subset of the C++ standard library, and adapting its build with exceptions/RTTI disabled is expensive. If a full port exceeds budget, the fallback is to keep ZAF's rendering interface unchanged and ship a self-developed Raster backend first, with Skia as an optional implementation — the interface is isolated inside `gfx/` and does not affect upper layers.
- Already landed (Z0): following that fallback, `zaf_gfx.h` (backend-neutral rasterisation: rectangles/rounded corners/lines/polygons/alpha) and `zaf_gfx_soft.c` (software implementation) were delivered, and `user/zaf_gfx_demo.c` maps the framebuffer in Ring3 and prints FPS. The repository still has no Skia source.
- Z1 decision (2026-11-14): Z1's composition and desktop drawing use the existing ZOS rendering API directly (`kernel/gfx/render.c`, shared by the kernel and Ring3), wrapped uniformly by `zaf/gfx/zaf_painter`; `zaf_gfx` stays a portable rasteriser and does not grow its responsibilities. The Skia port drops to a standalone spike (Z0.5) and is scheduled only after a user decision. Rationale: with a single rendering backend a multi-backend vtable is pure overhead (KISS), and Z1's acceptance surface is behavioural equivalence and separation of duties, not swapping the rendering technology.

## 4. Theme Engine

Themes are supplied as a directory or a packaged resource and contain:

- tokens for colours, fonts, spacing, corner radii, shadows, animation durations and the like;
- widget state styles (default/hover/pressed/disabled/focused);
- optional SVG graphic assets (icons follow rule 22).

Aqua is the built-in default theme; the theme engine handles token parsing, inheritance and runtime switching, and widgets read the tokens instead of hard-coding appearance. Switching themes does not rebuild windows, it only triggers a full repaint.

## 5. Milestones

| Phase | Content | Acceptance | Status |
|---|---|---|---|
| Z0 | Dependency decision: evaluate and pin a Skia subset port; finalise the `gfx/` interface | Can draw rectangles/paths/text in Ring3 with a measurable frame rate | Implemented (via the self-developed Raster fallback: `zaf_gfx` + software backend + fps demo) |
| Z1 | Compositor and window management: layers, damage regions, stacking and focus | Two client windows can be dragged, occluded and closed | Design finalised, implementation pending (see the Z1 design/plan) |
| Z2 | Widget system: base widgets + event dispatch + layout | Rewrite the welcome page and calculator with ZAF widgets | Not started |
| Z3 | Animation engine and the Aqua theme package | Window open/close and button feedback are animated; the theme can be switched | Not started |
| Z4 | Migrate Aqua Desktop Environment/Desktop Window Manager to ZAF | Desktop, menu bar, Dock and window decorations are all driven by ZAF | Not started |

> Z1's scope has been narrowed against the actual code to "move the existing Desktop Window Manager's composition/window policy into ZAF, with equivalent behaviour,
> no protocol changes and no kernel changes"; only from Z2 onward do new capabilities get added. See the Z1 design document.

## 6. Relationship to Existing Code

- The existing `apps/ADE/` (display server, Desktop Window Manager, wallpaper/menu bar/Dock) and `user/lib/ade_client` are the migration source for Z1/Z4; the existing protocol stays usable during the migration.
- The existing `kernel/gfx/render.c` and `zos_api.h` form the low-level drawing and syscall boundary; Z1's ZAF calls it through `zaf/gfx/zaf_painter`, and the compositor composites layers into the back buffer and presents by damage, never writing the framebuffer directly.
- Z1 splits Desktop Window Manager's responsibilities into: `zaf/compositor` (layers/damage/present/cursor), `zaf/wm` (window table/focus/z-order/hit testing/dragging), `apps/ADE/decoration.cpp` (window decorations, reading Aqua tokens). Desktop Window Manager remains as a thin adapter layer.
- Publishing the ZAF library as a ZXE shared library, with applications depending on it through `.zxe.deps`, is a **Z4** plan (see the ZXE v1 design document); Z1 only produces static code linked into `/bin/aqua`, avoiding dynamic ABI risk while the toolchain is unavailable.

## 7. Risks

1. Porting Skia is the largest chunk of work; Z0 already delivered a self-developed Raster through the fallback. Whether to adopt Skia at all needs its own evaluation project.
2. C++ runtime limits (no exceptions/RTTI, no STL iostream) may force a trimmed Skia API surface.
3. Text layout (line breaking, kerning, CJK) needs its own design and cannot rely on glyph rasterisation alone.
4. There is a dual-track compatibility period with the existing Aqua Desktop Environment protocol; desktop usability must not break before the migration completes.
5. Rewriting the compositor in Z1 may introduce visual regressions; the rollback path and per-Task screenshot comparison are described in the Z1 plan.

## 8. Change Log

- 2026-11-13: first draft (Skia integration strategy, layering, Z0–Z4 milestones).
- 2026-11-14: marked Z0 implemented (self-developed Raster fallback); added the Z1 design/plan links;
  Z1 scope narrowed to splitting the existing Desktop Window Manager behaviour into separate duties, rendering continues to use ZOS API; Skia dropped to a Z0.5 spike.
