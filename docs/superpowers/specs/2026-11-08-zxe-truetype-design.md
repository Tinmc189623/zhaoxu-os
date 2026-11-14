# Design: TrueType Chinese rendering + in-house .zxe executable format (0.4.0)

Date: 2026-11-08
Status: Approved (additional constraint: the loader must retain ELF compatibility)

## 1. Goals and scope

1. Design an in-house executable format, `.zxe`, and replace all ring 3 user program artifacts (`init.elf`, `hello.elf` → `init.zxe`, `hello.zxe`). The kernel loader **supports both .zxe and ELF64** (dispatching on the file magic), so existing ELF programs still run.
2. Implement TrueType (glyf outlines) font parsing and rasterization inside the kernel, with console support for UTF-8 Chinese (CJK) rendering. Use the open-source WenQuanYi Micro Hei font (`wqy-microhei.ttc`, TTC/glyf, about 5.2 MB), stored on the FAT32 data disk at `/mnt/WQY.TTC`.
3. Make the shell banner/help, `/etc/motd`, and the sample program `hello.zxe` output Chinese as acceptance cases.
4. Out of scope: BOOTX64.EFI (must remain UEFI PE/COFF), kernel.elf build internals, scheduler/memory layout changes, CFF fonts, font hinting.

## 2. .zxe format v1

Little-endian, all fields `u64`. File = header + segment descriptor table + segment data.

```
Header (32 bytes)
  0   u8[4]   magic  'Z' 'X' 'E' 0x01
  4   u16     version = 1
  6   u16     machine = 0x003E (x86-64)
  8   u16     seg_count (1..16)
  10  u16     hdr_size = 32 + seg_count * 40 (after 16-byte alignment)
  12  u32     reserved = 0
  16  u64     entry (virtual address)
  24  u64     file_size (total file size)

Segment descriptor (40 bytes each, immediately after the header)
  0   u64  vaddr     (must be >= USER_LOAD_BASE and page-aligned)
  8   u64  file_off  (16-byte aligned, >= hdr_size)
  16  u64  file_size
  24  u64  mem_size  (>= file_size; the excess is zeroed = .bss)
  32  u64  flags     (bit0 R, bit1 W, bit2 X)
  40  end
```

Validation rules align with `elf_load`: ranges must not cross `USER_ADDRESS_LIMIT`, overlap checks (optional), overflow checks, and `file_off + file_size <= file_size(total)`. Flags are stored but not enforced (current page permissions remain fully open U/S+RW, matching the ELF path).

## 3. Build pipeline

- Add `src/tools/zxe-pack.rb` (pure Ruby, no gem dependencies): parse the ELF64 PT_LOAD segments and e_entry, then write `.zxe` in the format above. ELF becomes an intermediate artifact (`build/%.elf`); the final artifact is only `.zxe`.
- Makefile: `$(OUT)/%.zxe: user/%.c` (link to `$(BUILD)/$*.elf` first, then convert with Ruby); `RAMFS/bin/init` comes from `init.zxe` (the path name stays `/bin/init`); `data.img` packages `HELLO.ZXE` (the zxe version) **and** `HELLO.ELF` (the original elf, for ELF compatibility regression).
- Ruby location: WSL has no Ruby, so use Windows Ruby interop (`/mnt/c/Ruby40-x64/bin/ruby.exe`); the Makefile variable is `RUBY ?= ruby.exe`, and the build errors with a hint when it cannot be found (no silent installation of system dependencies).
- Font file in the repository: `src/initramfs/data/wqy.ttc` → mcopy to `::/WQY.TTC` (8.3 name, avoiding LFN case issues).

## 4. Loader (process.c)

- Keep `elf_load()` unchanged; add `zxe_load()`; `process_run()` dispatches on the first 4 bytes of magic: `ZXE\x01` → zxe, `\x7FELF` → elf, anything else reports "not a valid ZXE/ELF64 executable".
- Page mapping, stack construction, and exit paths are unchanged.

## 5. TrueType Chinese rendering (kernel/font/ttf.c, new)

Freestanding, no floating point, no SSE (all integer arithmetic under `-mgeneral-regs-only`).

- Parsing: `ttcf` header (select font 0) / sfnt table directory; required tables `head` (unitsPerEm, indexToLocaFormat), `hhea`, `maxp`, `cmap` (platform 3/encoding 1, format 4; fall back to format 12 when missing), `hmtx`, `loca`, `glyf`. Skip hinting (do not interpret `cvt/fpgm/prep`).
- Outlines: simple outlines (on/off-point quadratic Béziers, adaptive subdivision) + composite outlines (recursive glyph components, including x/y offsets and basic scaling).
- Rasterization: 26.6 fixed point; scanline + non-zero winding; 4 vertical samples per pixel (integer coverage width stepped in 1/16-row increments) combined into a 0..255 alpha mask. Output per glyph: width/height, x/y bearing, advance (from hmtx, unitsPerEm → 16 px scaling).
- Cache: 256-slot hash table (`kzalloc` after successful `ttf_init`, about 150 KiB, not in BSS), key = codepoint, storing the alpha mask (capped at 20×20 pixels) + metrics; clear everything when full (simple policy, sufficient).
- Interface: `ttf_available()`, `ttf_render(cp, struct glyph_mask *)` (returns the alpha mask plus width/height/advance).
- Compositing: add `gop_blit_alpha(fb, pitch, x, y, w, h, mask, fg, bg)` to `src/drivers/gop.ads/adb` (Ada, in the same style as the existing Blit_Glyph); the C side only calls it.

## 6. Font loading (not limited by the 4 MiB heap)

The 5.2 MB font is larger than `HEAP_REGION_SIZE` (4 MiB), so it cannot go through `vfs_read_all`/kmalloc:

- Add `fat32_read_into(path, dst, cap)` / `ramfs_read_into` and `vfs_read_into()` (reuse the existing cluster read loop, but the destination buffer is supplied by the caller; the cluster staging buffer remains a kmalloc allocation of ≤32 KiB).
- Add `kernel/font/fontdata.c` (or merge it directly into ttf.c): `vfs_stat` obtains the size → `vmm_alloc_pages(ceil(size/4K))` returns a kernel VA (AHCI works normally through `vmm_get_physical`) → `vfs_read_into` loads the data → hand it to `ttf_init(ptr, size)`; the mapping stays resident and is never released.
- Timing: `main.c` calls `console_load_cjk_font("/mnt/WQY.TTC")` after `vfs_mount_data()` and before `process_init()`; on failure it only prints `[ttf ] no CJK font` and does not affect boot.

## 7. Console UTF-8 layout (console.c changes)

- Keep the ASCII path completely unchanged (8×8 bitmap, 8×16 cells); the serial port still passes raw UTF-8 through byte by byte (the host terminal displays it).
- `console_putc` becomes a UTF-8 state machine (1–4 bytes), and the framebuffer side renders by codepoint:
  - `cp < 0x80`: use the existing `draw_cell`;
  - CJK/wide characters (determined by ttf, advance ≥ threshold or explicit wide table: U+2E80..U+9FFF, U+FF00..U+FF60, U+3000..U+303F, etc. + fullwidth punctuation): 16×16 rendering, occupying 2 columns; wrap first when fewer than two columns remain; when no font is loaded, draw an 8×8 `?` (existing behavior);
  - combining/other non-BMP characters: render a box for now (does not block acceptance).
- `console_write_n` (the sys_write path) gains the same capability automatically.

## 8. Chinese UI strings

- `shell.cpp`: banner, help, command descriptions (`run` description changed to "execute a .zxe/ELF64 program"), common error messages → Chinese string literals (UTF-8; the C++ side needs no `-fexec-charset` because the source is UTF-8).
- `kernel/main.c`: two Chinese lines in the boot banner + `ZHAOXU_VERSION "0.4.0"`; keep English prefixes for logs such as `[init]` and `[gop]` (grep-friendly), and add Chinese notes on key lines.
- `initramfs/etc/motd`: Chinese; `version`: 0.4.0.
- `user/hello.c`: add Chinese output lines (acceptance: UTF-8 bytes visible on the serial port, Chinese characters visible in the framebuffer).
- Sync the version in the 5 places required by AGENTS.md (README, shell.cpp kVersion, main.c, motd, version).

## 9. Error handling and fallback

- zxe header validation failure → a clear error message returns to the shell; the process never enters ring 3.
- No `/mnt`, missing font file, unrecognized ttcf/cmap → `[ttf ]` warning + CJK displays as `?`, ASCII unaffected; the HEADLESS (no framebuffer) path is completely unchanged.
- Font memory allocation failure (`vmm_alloc_pages` returns 0) → the same fallback.
- User pointer/range validation continues to use `user_range_ok`.

## 10. Testing and acceptance (no automated tests, QEMU smoke)

1. `make clean && make -j4`; `nm -u out/kernel.elf` has no undefined symbols; `file out/hello.zxe` reports no dependencies, and `ruby -c src/tools/zxe-pack.rb` passes syntax checking.
2. `make run`: Chinese banner lines (visible through raw UTF-8 serial passthrough); `[ttf ] WQY.TTC loaded, N glyphs cached` (counted after the first glyph render); `/bin/init` runs automatically and returns to the shell.
3. `run /mnt/HELLO.ZXE` → exit code 7; `run /mnt/HELLO.ELF` → exit code 7 (ELF compatibility regression); `cat /mnt/HELLO.TXT` and `ls /mnt` work normally.
4. Framebuffer verification: `make run QEMU_DISPLAY="-display gtk"` to manually confirm Chinese character display, mixed-script alignment, line wrapping, and scrolling without misalignment (cannot be automated; note this in the report).
5. `make iso && make run-iso`: without /mnt, Chinese degrades to `?`, and the remaining functionality does not regress.
6. User exception tests are unchanged (exit code -14, etc.).

## 11. Documentation and rules sync (AGENTS.md)

- In the verification steps, change the `HELLO.ELF` entry to `HELLO.ZXE` (retaining the ELF compatibility note); add Ruby (Windows interop) to the dependency list; add a section on the .zxe format; document that the font comes from `src/initramfs/data/wqy.ttc`, the GPL font exception, and that `RAMFS_FILES` is unchanged while `data.img` gains `WQY.TTC`.

## 12. Risks

- The rasterizer is the largest workload (about 600–900 lines of C + Ada blit); bitmap boundaries (negative bearings, extra-wide glyphs beyond the 18×16 cell) need clamping.
- A resident font adds 5 MiB of physical memory under 512 MiB: acceptable.
- WSL has no root: Ruby uses Windows interop and the font is already in the repository, so apt is no longer required; if `ruby.exe` interop is disabled, the build fails explicitly.
