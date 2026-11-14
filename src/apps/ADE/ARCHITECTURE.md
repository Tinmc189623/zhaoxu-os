# Aqua Desktop Environment Desktop Architecture

## Role

`Aqua Desktop Environment` is the Zhaoxu OS desktop environment. It runs in Ring3 and serves as both display server and
Desktop Window Manager. Applications are separate `.zxe` processes and are not bundled into Aqua Desktop Environment.

## Directory

```
src/apps/ADE/
  main.cpp        process entry, ade_app registry, service startup, event loop
  wallpaper.cpp   wallpaper (rule 33 image, integer scaling; Aqua gradient fallback without a data disk)
  menubar.cpp     Aqua menu bar and RTC clock
  dock.cpp        Dock drawing, hover, click launcher
  icons.cpp       application icons (inline SVG, handed to the system SVG rasterizer)
  about.cpp       About window (Aqua Desktop Environment internal application)
  winmgr/         Desktop Window Manager: focus, z-order, drag/close, input routing, window decoration calls
  compositor/     Desktop Window Compositor: back buffer, damage present, software cursor
  display/        named port service, window protocol, input routing
  (planned for Z1) decoration.cpp  window decoration drawing, colors/metrics from ZAF Aqua tokens
```

Desktop Window Compositor (`compositor/`) owns the back buffer and the present path from the back buffer to the framebuffer: Desktop
Window Manager decides what each layer draws, the compositor decides when completed rectangles go to the screen, and it maintains the cursor position independently.
ZAF (`src/zaf/`) provides graphical rasterization and theme tokens; see `src/zaf/README.md` for the module map.

Cursor shapes come from PNG files plus a `cursor.theme` text descriptor under `/usr/share/cursors/Aqua`
(`compositor/cursor.c`, with PNGs decoded by `stb_image`). Desktop Window Manager selects a shape per hit region on each round:
menu bar clickable areas, Dock icons, and window close buttons use `hand`; window content goes to the application's
`ade_app.cursor_at` callback; clients register cursor regions in content coordinates with `ADE_OP_CURSOR` (`ade_cursor()`),
and the last registration wins. Without a theme it falls back to the built-in arrow.

## Boundaries

- Aqua Desktop Environment contains no application implementations; it only exposes the client API and port protocol.
- Desktop Window Manager does not write the framebuffer directly: pixels go through the ZAF painter (currently forwarded to the system `render_*` API)
  and are ultimately presented by Desktop Window Compositor.
- Applications submit drawing commands through the port command stream; Aqua Desktop Environment validates and executes them.
- Input events are routed by Aqua Desktop Environment to the focused window.
- Fonts are loaded by the system `libzos` from `/mnt` (MiSans / MiSans Global / Inter);
  neither Aqua Desktop Environment nor applications ship fonts.

## Window protocol (summary)

A client connects to the named port `ade`; after the handshake:

- `CREATE_WINDOW`: title, size, icon, command ring shared memory.
- `COMMIT`: sequence number + damage rectangles, telling Aqua Desktop Environment to execute the command stream.
- `CONFIGURE / DESTROY`: window state and lifecycle.
- `INPUT`: Aqua Desktop Environment → client key, click, and focus events.
- `CLOSE`: Aqua Desktop Environment → client close request.
- `CURSOR`: client → Aqua Desktop Environment cursor region hint; rectangles use content coordinates,
  and the text field holds the cursor name from the theme. `CLEAR` clears the regions registered for this frame.

The command stream uses fixed-length commands (opcode + arguments) covering fill, rounded corners, gradients, text, SVG,
clipping, and bitmaps; Aqua Desktop Environment executes them on the window surface through the system rendering interface, then composites to the framebuffer.

## Startup

After the kernel finishes device and filesystem initialization it starts `/bin/aqua`; Aqua Desktop Environment reads
`/etc/ade/apps/*.desk` to build the Dock and launcher, and `spawn`s application processes on demand.
