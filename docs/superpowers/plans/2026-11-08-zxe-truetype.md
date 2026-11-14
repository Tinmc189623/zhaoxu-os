# .zxe Executable Format + TrueType CJK Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Ring 3 programs to the in-house `.zxe` format (the loader keeps ELF compatibility), implement TrueType (glyf) rasterization inside the kernel so the console can display Chinese, localize the shell/motd/sample programs into Chinese, version 0.4.0.

**Architecture:** At build time the pure-Ruby `zxe-pack.rb` wraps the intermediate ELF linked by gcc into a `.zxe`; the kernel loader dispatches to `zxe_load()`/`elf_load()` by magic number. TrueType goes through a separate `kernel/font/ttf.c`: TTC/sfnt table parsing → cmap format 4 → loca/glyf outlines (simple + composite) → 26.6 fixed-point scanline rasterization into an 8-bit alpha mask → glyph cache → alpha blending onto the screen through the Ada GOP layer; the font `WQY.TTC` (5.2 MB, above the heap limit) is read by `vfs_read_into` into a resident mapping allocated by `vmm_alloc_pages`.

**Tech Stack:** C11 (freestanding, no floating point/SSE), FASM (untouched), Ada (GOP), C++20 (shell), Ruby (build scripts/smoke), QEMU + OVMF smoke tests.

**Spec:** `docs/superpowers/specs/2026-11-08-zxe-truetype-design.md`

## Global Constraints

- The kernel is freestanding: no floating point, SSE/MMX/AVX, or libc; all math is integer (26.6 fixed point).
- New scripts are always Ruby (AGENTS.md rule 6); Ruby inside WSL is the Windows interop `/mnt/c/Ruby40-x64/bin/ruby.exe` (Makefile variable `RUBY`).
- The build directory must be `src/` (WSL `Ubuntu-26.04`). `build/`, `out/`, and `esp/` are artifacts and must not be edited by hand.
- The loader must support both `.zxe` and ELF64 (dispatch by magic; the ELF path code keeps its behavior unchanged).
- Address invariants stay put: `USER_LOAD_BASE 0x100000000`, `USER_ADDRESS_LIMIT 0x800000000000`, and the five synchronized kernel-stack-top items are not touched.
- The kernel workspace already has uncommitted changes (the ISO + ps features in AGENTS.md/README.md/Makefile/shell.cpp, `src/boot/main.asm.headless`): snapshot them as a separate commit before starting so they do not get mixed into this feature.
- Verification = clean rebuild + QEMU smoke (serial); framebuffer Chinese rendering needs manual confirmation with `-display gtk` and must be noted in the report.

---

### Task 0: Snapshot the user's uncommitted changes

**Files:**
- Commit existing: `AGENTS.md`, `README.md`, `src/Makefile`, `src/apps/shell.cpp`; new: `src/boot/main.asm.headless` (Read it first to confirm it is a valid boot layer variant before adding it)

- [ ] Step 1: `git add AGENTS.md README.md src/Makefile src/apps/shell.cpp src/boot/main.asm.headless && git commit -m "chore: snapshot pending ISO target, ps command and headless boot variant"`
- [ ] Step 2: `git status --short` should be empty (apart from this plan file)

---

### Task 1: zxe-pack.rb converter

**Files:**
- Create: `src/tools/zxe-pack.rb`
- Test: validate directly on the command line (no unit-test framework)

**Interfaces:**
- Consumes: the static, non-PIE ELF64 produced by gcc/lld (`user/user.ld`, vaddr ≥ 0x100000000)
- Produces: `out/*.zxe` binaries; the `struct zxe_header/segment` used by the kernel's Task 2 must match this exactly:

```
u8[4] magic 'ZXE\x01' | u16 version=1 | u16 machine=0x3E | u16 seg_count(1..16)
u16 hdr_size(=align16(32+40*n)) | u32 reserved=0 | u64 entry | u64 file_size(total length)
segment table: n * { u64 vaddr(page aligned) | u64 file_off(16 aligned) | u64 file_size | u64 mem_size | u64 flags(1R 2W 4X) }
data follows the segment table, laid out per file_off.
```

- [ ] Step 1: Write `src/tools/zxe-pack.rb` (pure Ruby, no gems):

```ruby
#!/usr/bin/env ruby
# zxe-pack.rb -- ELF64 (static, non-PIE) -> Zhaoxu OS .zxe v1
# usage: ruby zxe-pack.rb in.elf out.zxe
abort "usage: #{$0} in.elf out.zxe" unless ARGV.size == 2
data = File.binread(ARGV[0])
abort "not ELF" unless data[0, 4] == "\x7FELF" && data[4] == 2 && data[5] == 1
e_type, e_machine = data[16, 4].unpack("v2")
abort "not EXEC x86-64" unless e_type == 2 && e_machine == 0x3E
e_entry = data[24, 8].unpack1("Q<")
e_phoff, e_phentsize, e_phnum = data[32, 8].unpack1("Q<"), *data[54, 4].unpack("v2")
segs = (0...e_phnum).map { |i|
  p = e_phoff + i * e_phentsize
  type, flags = data[p, 8].unpack("V2")
  off, vaddr, filesz, memsz = data[p + 8, 32].unpack("Q4")
  [type, flags, off, vaddr, filesz, memsz] if type == 1 && memsz > 0
}.compact
abort "no PT_LOAD" if segs.empty? || segs.size > 16
segs.each { |s| abort "vaddr below USER_LOAD_BASE" if s[3] < 0x1_0000_0000 }
n = segs.size
hdr = 32 + 40 * n
hdr = (hdr + 15) & ~15
out = "".b
cursor = hdr
plan = segs.map { |_, flags, off, vaddr, filesz, memsz|
  vo = vaddr & ~0xFFF
  [vo, cursor, filesz, memsz, flags & 7].tap { cursor += (filesz + 15) & ~15 }
}
total = (cursor + 15) & ~15
out << "ZXE\x01".b << [1, 0x3E, n, hdr].pack("v4") << [0, e_entry, total].pack("Q<3")
plan.each { |vo, fo, fs, ms, fl| out << [vo, fo, fs, ms, fl].pack("Q5") }
out << ("\x00" * (hdr - out.bytesize))
plan.each { |vo, fo, fs, _ms, _fl|
  src = segs.find { |s| (s[3] & ~0xFFF) == vo }
  out[fo, fs] = data[src[2], fs]
}
File.binwrite(ARGV[1], out)
puts "zxe: #{n} segs, entry #{"0x%x" % e_entry}, #{out.bytesize} bytes -> #{ARGV[1]}"
```

- [ ] Step 2: Build an elf and verify the conversion: `wsl -d Ubuntu-26.04 -- bash -lc 'cd "/mnt/g/Apps/OS/x86/AMD64/Zhaoxu OS/src" && make out/hello.elf && /mnt/c/Ruby40-x64/bin/ruby.exe tools/zxe-pack.rb out/hello.elf /tmp/hello.zxe'`, then use an inline Ruby `--verify` script to read the header fields back and assert magic/entry/segment count agree (compare the entry printed by the converter against `readelf -h`).
- [ ] Step 3: Commit `git add src/tools/zxe-pack.rb && git commit -m "feat: add Ruby ELF to .zxe packer"`

---

### Task 2: Kernel .zxe loader (keeping ELF)

**Files:**
- Modify: `src/kernel/proc/process.c` (new structs + `zxe_load`, dispatch in `process_run`)

**Interfaces:**
- Consumes: the binary layout from Task 1
- Produces: `process_run()` with the same semantics for both formats (0 = loaded successfully, -1 = rejected); error message `"%s is not a valid ZXE/ELF64 executable"`

- [ ] Step 1: After the `elf64_program_header` definition, add:

```c
struct zxe_header {
    u8 magic[4];
    u16 version;
    u16 machine;
    u16 seg_count;
    u16 hdr_size;
    u32 reserved;
    u64 entry;
    u64 file_size;
} __attribute__((packed));

struct zxe_segment {
    u64 vaddr;
    u64 file_off;
    u64 file_size;
    u64 mem_size;
    u64 flags;
} __attribute__((packed));

#define ZXE_SEG_MAX 16
```

- [ ] Step 2: Implement `static int zxe_load(const u8 *image, u64 image_size, u64 *entry_out)`: validate magic/`version==1`/`machine==62`/`seg_count 1..16`/`hdr_size>=32+40*seg_count and 16-aligned and <= image_size`/`file_size==image_size`; per segment validate `vaddr>=USER_LOAD_BASE`, page alignment, that `vaddr+mem_size` does not wrap and is `< USER_ADDRESS_LIMIT`, `file_off+file_size <= image_size`, `file_size<=mem_size`; `map_user_page` over [vaddr&~4095, round_up(vaddr+mem_size)), memcpy the file part, memset the bss part (same style as elf_load); after validating the entry range set `*entry_out = entry`.
- [ ] Step 3: Replace the call in `process_run`:

```c
int status;
if (image_size >= 4 && image[0] == 'Z' && image[1] == 'X' &&
    image[2] == 'E' && image[3] == 1)
    status = zxe_load(image, image_size, &entry);
else
    status = elf_load(image, image_size, &entry);
kfree(image);
if (status != 0) {
    kprintf("[proc] %s is not a valid ZXE/ELF64 executable\n", path);
```

- [ ] Step 4: `make -j4` passes; `nm -u out/kernel.elf` has no undefined symbols.
- [ ] Step 5: Commit `git commit -am "feat: add .zxe loader alongside ELF64 in process.c"`

---

### Task 3: Makefile pipeline + data.img + smoke harness

**Files:**
- Modify: `src/Makefile`
- Create: `src/tools/smoke.rb`

**Interfaces:**
- Consumes: Task 1/2
- Produces: `out/init.zxe`, `out/hello.zxe`; `data.img` containing `/HELLO.ZXE`, `/HELLO.ELF`, `/WQY.TTC`; `smoke.rb "command string" "expected marker"` returns 0/1

- [ ] Step 1: Change the user program section of the Makefile to:

```make
RUBY ?= /mnt/c/Ruby40-x64/bin/ruby.exe
$(BUILD)/%.uelf: user/%.c user/userlib.c user/userlib.h user/user.ld
	@mkdir -p $(BUILD)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/$*.c user/userlib.c
$(OUT)/%.zxe: $(BUILD)/%.uelf tools/zxe-pack.rb
	@mkdir -p $(OUT)
	$(RUBY) tools/zxe-pack.rb $< $@
```

Delete the old `$(OUT)/%.elf` rule; append `$(OUT)/init.zxe $(OUT)/hello.zxe` to `clean:`.
- [ ] Step 2: `$(RAMFS)/bin/init: $(OUT)/init.zxe`; `$(DATA_IMG): $(OUT)/hello.zxe $(BUILD)/hello.uelf initramfs/data/*`, and add to the mcopy calls:

```make
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(OUT)/hello.zxe ::/HELLO.ZXE
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(BUILD)/hello.uelf ::/HELLO.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/wqy.ttc ::/WQY.TTC
```

- [ ] Step 3: `src/tools/smoke.rb`: `system("qemu-system-x86_64 ... -serial stdio -display none -no-reboot", in: command_string)` captures the output and asserts with `text.include?(marker)`; arguments: command string (\n separated), marker, optional timeout (default 120s).
- [ ] Step 4: Verify: `make clean && make -j4 && make out/data.img`; run smoke (from src/, replicating the `run:` target's qemu command line):
  - marker `exited with status 7`, command `run /mnt/HELLO.ZXE\nrun /mnt/HELLO.ELF\nshutdown\n` → both exit with code 7.
  - feeding `run /mnt/NOFILE.ZXE\n` should print not a valid.
- [ ] Step 5: Commit `git add -A src/Makefile src/tools && git commit -m "feat: build ring 3 programs as .zxe; keep ELF regression file on data.img"`

---

### Task 4: vfs_read_into (large files into the caller's buffer)

**Files:**
- Modify: `src/kernel/fs/fat32.c`, `src/kernel/fs/initramfs.c`, `src/kernel/fs/vfs.c`, `src/kernel/include/vfs.h`, `src/kernel/include/fat32.h`

**Interfaces:**
- Produces: `int fat32_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)`, `int ramfs_read_into(...)`, `int vfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)` (`/mnt/` prefix routing is the same as `vfs_read_all`; returns -1 if the file is larger than cap; 0 on success)
- Consumes: the font loading in Task 5

- [ ] Step 1: `fat32.c`: lift the cluster loop of `fat32_read_all` into `static int fat32_read_into_entry(const struct fat_entry *entry, u8 *dst, u64 cap, u64 *read_out)` (the staging buffer is still a kmalloc of cluster_bytes), make `fat32_read_all = kmalloc + call it`; export the new `fat32_read_into`.
- [ ] Step 2: `initramfs.c` gains `ramfs_read_into` (memcpy after `node->size<=cap`); `vfs.c` gains the `vfs_read_into` routing; declare both in the headers.
- [ ] Step 3: `make -j4` passes; a QEMU smoke boot to the shell shows no regression.
- [ ] Step 4: Commit

---

### Task 5: ttf.c — TTC/sfnt/cmap/loca/glyf parsing + resident font loading

**Files:**
- Create: `src/kernel/font/ttf.c`, `src/kernel/include/ttf.h`
- Modify: `src/kernel/main.c` (load the font after mounting /mnt), `src/Makefile` (add kernel/font/ttf.c to C_SRCS)

**Interfaces:**
- Consumes: Task 4's `vfs_read_into`; `src/initramfs/data/wqy.ttc` (verified: ttcf with 2 fonts, font0 table set `head hhea maxp cmap glyf loca hmtx …`, unitsPerEm=2048, indexToLocFormat=1 (32-bit loca), numGlyphs=49531, cmap 3/1 format4 and 3/10 format12, `中`→glyph 64998)
- Produces (ttf.h):

```c
int ttf_load(const u8 *data, u64 size);           /* 0 ok, -1 bad */
int ttf_available(void);
int ttf_glyph_for(u32 codepoint, u16 *glyph);      /* cmap fmt4 (+fmt12 fallback) */
struct ttf_glyph_outline;                          /* filled in by the rasterizer, see Task 6 */
/* Task 5 first provides the metrics/debug interfaces */
int ttf_glyph_bbox(u16 glyph, i16 out[4]);
u32 ttf_num_glyphs(void); u32 ttf_upem(void); const char *ttf_cmap_kind(void);
```

- [ ] Step 1: Parsing: `ttcf` (validate tag, take offsets[0]) or a bare sfnt; the table directory stores `struct { u32 off, len; }` for the 8 required tables; `head`: unitsPerEm, indexToLocFormat; `maxp`: numGlyphs; `hhea`: numberOfHMetrics; `hmtx` offset; `loca`: glyph offsets as 32/16-bit depending on locaFmt; `glyf`: outline data.
- [ ] Step 2: `cmap`: walk the subtables, preferring **format 4** at (3,1) or (0,3)/(0,4) (segCountX2/searchRange all read by index; the four arrays endCode/startCode/idDelta/idRangeOffset); if only **format 12** ((3,10)/(0,4)) exists, binary-search the groups. Lookup = binary search on endCode + range check + the delta/rangeOffset formula (`rangeOffset==0 ? (cp+delta)&0xFFFF : (raw + delta)&0xFFFF`, with bounds protection on the raw read).
- [ ] Step 3: The resident font loading function lives in `ttf.c`: `int ttf_autoload(const char *path)` — `vfs_stat` for the size → `vmm_alloc_pages((size+4095)/4096)` → `vfs_read_into` → `ttf_load`; on failure release pages one by one with `vmm_unmap_page` and return -1. `main.c` calls `ttf_autoload("/mnt/WQY.TTC")` after `vfs_mount_data()` succeeds and kprintf's: `[ttf ] loaded upem=%u glyphs=%u cmap=%s` / `[ttf ] no CJK font (%d)\n`.
- [ ] Step 4: Smoke: `smoke.rb "" "[ttf ] loaded upem=2048"` (QEMU with 512 MiB is enough); then assert the boot log contains cmap=fmt4.
- [ ] Step 5: Commit `git commit -am "feat: TrueType TTC table and cmap parsing"`

---

### Task 6: Outline extraction + fixed-point rasterization + glyph cache + fonttest

**Files:**
- Modify: `src/kernel/font/ttf.c`, `src/kernel/include/ttf.h`
- Modify: `src/apps/shell.cpp` (new `fonttest` command)

**Interfaces:**
- Produces (added to ttf.h):

```c
struct ttf_glyph {
    u8 alpha[20 * 20];   /* row-major 8-bit coverage 0..255 */
    u8 w, h, adv;        /* adv is the width in cells (half-width=4/full-width=8, converted by *GLYPH_WIDTH/2) */
    i8 bx, by;           /* bitmap offset relative to the cell's top-left corner */
};
int ttf_render(u32 codepoint, struct ttf_glyph *out); /* cache hit or fresh rasterization */
```

- Consumes: Task 5's table access primitives
- [ ] Step 1: Outlines: `glyf` header (numberOfContours i16; simple outlines with flags/on-curve/x/y arrays; the repeat-count optimization for x=delta16/same value, same for y); `point_scale(v) = (v * (TARGET_PX<<6)) / upem` (i64 multiply); baseline: flip the y axis with `py = (ascent<<6) - y` (ascent from either hhea/OS2, starting with hhea.ascender). Composite outlines: `ARG_1AND2|XY_SCALE|BASIC_SCALE` and similar flags, recursion depth limit 8, only translation + 1.0/2.0 scaling supported (wqy composites are basically translations); on an unknown flag skip that component.
- [ ] Step 2: Quadratic Bezier flattening: off/on point sequence → implicit on-curve midpoints; subdivide each off segment in half 5 times (32 segments, error <0.02px @2048upem/16px) into a polyline (26.6 i32 x/y).
- [ ] Step 3: Scanline (non-zero winding): the target bitmap w×h comes from the bbox of all polylines, rounded outward by 1px and clamped to 20; each pixel row has 4 sub-scanlines (y+0.125/0.375/0.625/0.875 pixel); collect the polyline segments crossing that sub-row → (xfix26.6, winding ±1); sort and accumulate winding jumps to find the coverage spans; for each pixel column intersect `[col,col+1)` with the spans (1/64 px precision) and accumulate `cov += span∩pixel_len`; `alpha = cov * 255 / (4*64)`, clamped to 0..255, with polyline endpoints merged using previous-row continuity (a 1-subrow error is allowed for simplicity).
- [ ] Step 4: Cache: a `kzalloc` 256-slot open-addressing table (key=codepoint), returning a copy on hit; when full, memset it empty and rebuild. Metrics: advance comes from `hmtx[min(gid, numberOfHMetrics-1)].advanceWidth`.
- [ ] Step 5: shell `fonttest <UTF-8 character>`: after `ttf_render`, print per row `alpha>=128 ? '#' : alpha>=32 ? '+' : '.'`, with a space as '.' (the glyph shape visible over serial = automated acceptance).
- [ ] Step 6: Smoke: `smoke.rb "fonttest 中\nshutdown\n" "@@@"` change to: marker `fonttest` output is non-empty and the shape is symmetric (compare a screenshot/ASCII art by hand once); `fonttest A` renders.
- [ ] Step 7: Commit `git commit -am "feat: TrueType glyf rasterizer with glyph cache and fonttest"`

---

### Task 7: GOP alpha blending + console UTF-8 wide-character layout

**Files:**
- Modify: `src/drivers/gop.ads`/`gop.adb` (`Blit_Alpha`/`gop_blit_alpha`), `src/kernel/include/drivers.h`, `src/kernel/lib/console.c`

**Interfaces:**
- Consumes: Task 6's `ttf_render`
- Produces: `void console_putc(char)` (signature unchanged, UTF-8 state machine inside); CJK occupies 2 columns at 16px height; `cp<0x80` behavior is byte-for-byte identical to today.

- [ ] Step 1: Add to Ada:

```ada
procedure Blit_Alpha (Fb    : Unsigned_64; Pitch, X, Y, W, H : Unsigned_32;
                      Mask : Address; Fg, Bg : Unsigned_32);
-- Export => "gop_blit_alpha"; per pixel a=mask(row*w+col):
-- 0 -> Bg; 255 -> Fg; otherwise per channel (Bg*(255-a)+Fg*a)/255 (blend the 8-bit
-- components as integers on the fb-packed color: unpack r/g/b first, then blend; pack_color
-- input is 24-bit RGB, and splitting by byte order on the Ada side is acceptable too --
-- simplification: the C side passes fg/bg as expanded 0xRRGGBB values, Blit_Alpha blends the
-- three channels linearly via (fg>>16)&255 etc. and writes u32 exactly as the fb expects --
-- consistent with the existing pack_color convention; Ada already receives a packed u32 and
-- can blend its 8-bit channels directly (valid when the channel stride is 8 bits; both
-- BGRX/RGBX satisfy this).
```

- [ ] Step 2: console.c: globals `static u32 utf8_acc; static u32 utf8_left;`; `console_putc(c)`: serial still goes through `serial_putc((u8)c)` (including the \n→\r\n handling); the framebuffer side assembles the codepoint: determine the length from the lead byte, accumulate continuation bytes `0x80..0xBF` into `utf8_acc`; once the codepoint is complete: `<0x80` goes through the existing `draw_cell`; otherwise if `ttf_render` succeeds → first compute the width `cols = (g.adv+7)/8` (1 or 2), wrapping if there is not enough room at the line end; for a 2-column glyph use `gop_blit_alpha(x=cur*8, y=cur_row*16, w=g.w, h=g.h, mask, fg_pixel, bg_pixel)`, filling the covered area with `gop_fill_rect` bg first; font not loaded → the existing '?' behavior (draw one '?' cell per codepoint).
- [ ] Step 3: In `main.c` it is enough to move font loading after `console_init` (the console state machine has its own no-font branch); confirm the order: `vfs_mount_data` comes after `console_init`, so no change is needed.
- [ ] Step 4: Verification (semi-automatic): `make run QEMU_DISPLAY="-display gtk"` needs a manual screenshot; serial-side regression: `smoke.rb "ls /mnt\ncat /mnt/HELLO.TXT\nshutdown\n" "HELLO"` passes and scrolling does not hang; `cat /mnt/INFO.TXT` and friends show no regression.
- [ ] Step 5: Commit

---

### Task 8: Localize the text + samples + version 0.4.0 + docs

**Files:**
- Modify: `src/kernel/main.c` (Chinese banner + `ZHAOXU_VERSION "0.4.0"`), `src/apps/shell.cpp` (kVersion, banner/help, the `run` description "runs a .zxe/ELF64 program", common errors in Chinese), `src/initramfs/etc/motd`, `src/initramfs/etc/version`, `src/user/hello.c` (Chinese output line), `src/user/init.c` (if needed), `README.md`, `AGENTS.md`

**Interfaces:**
- Consumes: Task 7's Chinese rendering
- Produces: acceptance evidence: the UTF-8 source text over serial + Chinese glyphs in the framebuffer

- [ ] Step 1: Text changes (keep strings short; write UTF-8 directly in the C++ sources). `hello.c` adds `u_puts("[hello] 肇序 OS 用户态中文渲染测试\n");`.
- [ ] Step 2: Sync the version in 5 places to `0.4.0` (README, shell.cpp kVersion, main.c ZHAOXU_VERSION, motd, version).
- [ ] Step 3: AGENTS.md: verification step `run /mnt/HELLO.ZXE` (noting that HELLO.ELF is for compatibility regression), add Ruby to the dependencies (Windows interop path note), add the two sections "executable format .zxe v1" and "TrueType CJK rendering", and a `WQY.TTC` note (GPL font exception); README gains a feature description.
- [ ] Step 4: Full verification: `make clean && make -j4 && nm -u out/kernel.elf`; smoke sequence `run /mnt/HELLO.ZXE`/`run /mnt/HELLO.ELF`/marker `status 7`; `make iso && make run-iso` (no /mnt → `[ttf ] no CJK font`, ASCII renders normally); confirm Chinese in the framebuffer by hand (screenshot noted).
- [ ] Step 5: Commit `git commit -am "feat: CJK TrueType console + .zxe user format (0.4.0)"`

---

## Self-Review conclusions

- Spec coverage: format/pipeline/compatibility/rendering/loading/text/fallback/tests each map to a Task; the ISO degradation case is in Task 8 Step 4.
- Type consistency: the `zxe_header` fields correspond one by one to the Task 1 Ruby packing order (32B header + 40B segment); `ttf_render`/`gop_blit_alpha` naming is uniform across the whole document.
- Accepted risks: rasterizer 1-subrow continuity error, composite outlines limited to translation + basic scaling — both are in the spec's risk section, with fonttest/screenshots as the acceptance fallback.
