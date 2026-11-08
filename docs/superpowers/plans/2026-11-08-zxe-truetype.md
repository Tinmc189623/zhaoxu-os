# .zxe 可执行格式 + TrueType 中文渲染 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ring 3 程序改用自研 `.zxe` 格式（loader 保留 ELF 兼容），内核内实现 TrueType(glyf) 光栅化让控制台显示中文，Shell/motd/示例程序中文化，版本 0.4.0。

**Architecture:** 构建期用纯 Ruby `zxe-pack.rb` 把 gcc 链接出的中间 ELF 封装为 `.zxe`；内核 loader 按魔数分发 `zxe_load()`/`elf_load()`。TrueType 走独立 `kernel/font/ttf.c`：TTC/sfnt 表解析 → cmap format4 → loca/glyf 轮廓（简单+复合）→ 26.6 定点扫描线光栅化为 8-bit alpha 掩码 → 字形缓存 → Ada GOP 层 alpha 混合上屏；字体 `WQY.TTC`（5.2 MB，超堆上限）经 `vfs_read_into` 读入 `vmm_alloc_pages` 的常驻映射。

**Tech Stack:** C11（freestanding，无浮点/SSE）、FASM（不动）、Ada（GOP）、C++20（Shell）、Ruby（构建脚本/冒烟）、QEMU+OVMF 冒烟测试。

**Spec:** `docs/superpowers/specs/2026-11-08-zxe-truetype-design.md`

## Global Constraints

- 内核 freestanding：禁止浮点、SSE/MMX/AVX、libc；一切数学用整数（26.6 定点）。
- 新脚本一律 Ruby（AGENTS.md 规则 6）；WSL 内 Ruby = Windows interop `/mnt/c/Ruby40-x64/bin/ruby.exe`（Makefile 变量 `RUBY`）。
- 构建目录必须是 `src/`（WSL `Ubuntu-26.04`）。`build/`、`out/`、`esp/` 是产物不手改。
- loader 必须同时支持 `.zxe` 与 ELF64（按魔数分发，ELF 路径代码不改行为）。
- 地址不变量不动：`USER_LOAD_BASE 0x100000000`、`USER_ADDRESS_LIMIT 0x800000000000`、内核栈顶五处同步项不触碰。
- 内核工作区已有未提交改动（AGENTS.md/README.md/Makefile/shell.cpp 的 ISO+ps 功能、`src/boot/main.asm.headless`）：开工前先把它们作为独立提交快照，避免与本次功能混提。
- 验证 = 干净重建 + QEMU 冒烟（串口）；帧缓冲汉字渲染需 `-display gtk` 人工确认并在报告中注明。

---

### Task 0: 快照用户未提交改动

**Files:**
- 提交现有：`AGENTS.md`、`README.md`、`src/Makefile`、`src/apps/shell.cpp`；新增：`src/boot/main.asm.headless`（先 Read 确认是有效引导层变体再入库）

- [ ] Step 1: `git add AGENTS.md README.md src/Makefile src/apps/shell.cpp src/boot/main.asm.headless && git commit -m "chore: snapshot pending ISO target, ps command and headless boot variant"`
- [ ] Step 2: `git status --short` 应为空（除本计划文件）

---

### Task 1: zxe-pack.rb 转换器

**Files:**
- Create: `src/tools/zxe-pack.rb`
- Test: 命令行直接验证（无单测框架）

**Interfaces:**
- Consumes: gcc/lld 产出的静态 non-PIE ELF64（`user/user.ld`，vaddr ≥ 0x100000000）
- Produces: `out/*.zxe` 二进制；内核 Task 2 的 `struct zxe_header/segment` 必须与此完全一致：

```
u8[4] magic 'ZXE\x01' | u16 version=1 | u16 machine=0x3E | u16 seg_count(1..16)
u16 hdr_size(=align16(32+40*n)) | u32 reserved=0 | u64 entry | u64 file_size(总长)
段表: n * { u64 vaddr(page对齐) | u64 file_off(16对齐) | u64 file_size | u64 mem_size | u64 flags(1R 2W 4X) }
数据紧随段表，按 file_off 摆放。
```

- [ ] Step 1: 写 `src/tools/zxe-pack.rb`（纯 Ruby，无 gem）：

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

- [ ] Step 2: 构建一个 elf 并转换验证：`wsl -d Ubuntu-26.04 -- bash -lc 'cd "/mnt/g/Apps/OS/x86/AMD64/Zhaoxu OS/src" && make out/hello.elf && /mnt/c/Ruby40-x64/bin/ruby.exe tools/zxe-pack.rb out/hello.elf /tmp/hello.zxe'`，再用 ruby `--verify` 内联脚本读回头字段断言 magic/entry/段数一致（把转换输出的 entry 与 `readelf -h` 对比）。
- [ ] Step 3: 提交 `git add src/tools/zxe-pack.rb && git commit -m "feat: add Ruby ELF to .zxe packer"`

---

### Task 2: 内核 .zxe loader（保留 ELF）

**Files:**
- Modify: `src/kernel/proc/process.c`（新增结构体+`zxe_load`，`process_run` 分发）

**Interfaces:**
- Consumes: Task 1 的二进制布局
- Produces: `process_run()` 对两种格式返回相同语义（0 成功装载，-1 拒绝）；错误消息 `"%s is not a valid ZXE/ELF64 executable"`

- [ ] Step 1: 在 `elf64_program_header` 定义后加：

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

- [ ] Step 2: 实现 `static int zxe_load(const u8 *image, u64 image_size, u64 *entry_out)`：校验 magic/`version==1`/`machine==62`/`seg_count 1..16`/`hdr_size>=32+40*seg_count 且 16 对齐且 <= image_size`/`file_size==image_size`；逐段校验 `vaddr>=USER_LOAD_BASE`、页对齐、`vaddr+mem_size` 不回绕且 `< USER_ADDRESS_LIMIT`、`file_off+file_size <= image_size`、`file_size<=mem_size`；`map_user_page` 覆盖 [vaddr&~4095, round_up(vaddr+mem_size))，memcpy file 部分、memset bss 部分（同 elf_load 写法）；entry 范围校验后 `*entry_out = entry`。
- [ ] Step 3: `process_run` 中替换调用：

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

- [ ] Step 4: `make -j4` 通过；`nm -u out/kernel.elf` 无未定义符号。
- [ ] Step 5: 提交 `git commit -am "feat: add .zxe loader alongside ELF64 in process.c"`

---

### Task 3: Makefile 管线 + data.img + 冒烟 harness

**Files:**
- Modify: `src/Makefile`
- Create: `src/tools/smoke.rb`

**Interfaces:**
- Consumes: Task 1/2
- Produces: `out/init.zxe`、`out/hello.zxe`；`data.img` 含 `/HELLO.ZXE`、`/HELLO.ELF`、`/WQY.TTC`；`smoke.rb "命令串" "期望标记"` 退 0/1

- [ ] Step 1: Makefile 用户程序段改为：

```make
RUBY ?= /mnt/c/Ruby40-x64/bin/ruby.exe
$(BUILD)/%.uelf: user/%.c user/userlib.c user/userlib.h user/user.ld
	@mkdir -p $(BUILD)
	$(CC) $(USER_CFLAGS) $(USER_LDFLAGS) -o $@ user/$*.c user/userlib.c
$(OUT)/%.zxe: $(BUILD)/%.uelf tools/zxe-pack.rb
	@mkdir -p $(OUT)
	$(RUBY) tools/zxe-pack.rb $< $@
```

删除旧 `$(OUT)/%.elf` 规则；`clean:` 追加 `$(OUT)/init.zxe $(OUT)/hello.zxe`。
- [ ] Step 2: `$(RAMFS)/bin/init: $(OUT)/init.zxe`；`$(DATA_IMG): $(OUT)/hello.zxe $(BUILD)/hello.uelf initramfs/data/*`，mcopy 增加：

```make
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(OUT)/hello.zxe ::/HELLO.ZXE
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ $(BUILD)/hello.uelf ::/HELLO.ELF
	MTOOLS_SKIP_CHECK=1 mcopy -o -i $@ initramfs/data/wqy.ttc ::/WQY.TTC
```

- [ ] Step 3: `src/tools/smoke.rb`：`system("qemu-system-x86_64 ... -serial stdio -display none -no-reboot", in: 命令串)` 捕获输出，`text.include?(marker)` 断言；参数：命令串（\n 分隔）、marker、可选 timeout（默认 120s）。
- [ ] Step 4: 验证：`make clean && make -j4 && make out/data.img`；跑 smoke（在 src/ 下，qemu 命令行复刻 `run:` 目标）：
  - marker `exited with status 7`，命令 `run /mnt/HELLO.ZXE\nrun /mnt/HELLO.ELF\nshutdown\n` → 两条都退出码 7。
  - 喂 `run /mnt/NOFILE.ZXE\n` 应打印 not a valid。
- [ ] Step 5: 提交 `git add -A src/Makefile src/tools && git commit -m "feat: build ring 3 programs as .zxe; keep ELF regression file on data.img"`

---

### Task 4: vfs_read_into（大文件入调用方缓冲区）

**Files:**
- Modify: `src/kernel/fs/fat32.c`、`src/kernel/fs/initramfs.c`、`src/kernel/fs/vfs.c`、`src/kernel/include/vfs.h`、`src/kernel/include/fat32.h`

**Interfaces:**
- Produces: `int fat32_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)`、`int ramfs_read_into(...)`、`int vfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)`（`/mnt/` 前缀路由同 `vfs_read_all`；文件大于 cap 返回 -1；成功 0）
- Consumes: Task 5 字体加载

- [ ] Step 1: `fat32.c`：把 `fat32_read_all` 的簇循环提为 `static int fat32_read_into_entry(const struct fat_entry *entry, u8 *dst, u64 cap, u64 *read_out)`（暂存缓冲仍 kmalloc cluster_bytes），`fat32_read_all = kmalloc + 调用它`；新增导出 `fat32_read_into`。
- [ ] Step 2: `initramfs.c` 新增 `ramfs_read_into`（`node->size<=cap` 后 memcpy）；`vfs.c` 新增 `vfs_read_into` 路由；头文件声明。
- [ ] Step 3: `make -j4` 通过，QEMU 冒烟启动到 shell 不回归。
- [ ] Step 4: 提交

---

### Task 5: ttf.c —— TTC/sfnt/cmap/loca/glyf 解析 + 字体常驻加载

**Files:**
- Create: `src/kernel/font/ttf.c`、`src/kernel/include/ttf.h`
- Modify: `src/kernel/main.c`（挂载 /mnt 后加载字体）、`src/Makefile`（C_SRCS 加 kernel/font/ttf.c）

**Interfaces:**
- Consumes: Task 4 `vfs_read_into`；`src/initramfs/data/wqy.ttc`（已验证：ttcf 2 fonts，font0 表集 `head hhea maxp cmap glyf loca hmtx …`，unitsPerEm=2048，indexToLocFormat=1（32 位 loca），numGlyphs=49531，cmap 3/1 format4 与 3/10 format12，`中`→glyph 64998）
- Produces（ttf.h）:

```c
int ttf_load(const u8 *data, u64 size);           /* 0 ok, -1 bad */
int ttf_available(void);
int ttf_glyph_for(u32 codepoint, u16 *glyph);      /* cmap fmt4 (+fmt12 后备) */
struct ttf_glyph_outline;                          /* 由 rasterizer 填充，见 Task 6 */
/* Task 5 先提供度量/调试接口 */
int ttf_glyph_bbox(u16 glyph, i16 out[4]);
u32 ttf_num_glyphs(void); u32 ttf_upem(void); const char *ttf_cmap_kind(void);
```

- [ ] Step 1: 解析：`ttcf`（tag 校验，取 offsets[0]）或裸 sfnt；表目录存 `struct { u32 off, len; }` 于所需 8 张表；`head`: unitsPerEm、indexToLocFormat；`maxp`: numGlyphs；`hhea`: numberOfHMetrics；`hmtx` 偏移；`loca`：按 locaFmt 32/16 位取 glyph 偏移；`glyf`：轮廓数据。
- [ ] Step 2: `cmap`：遍历 subtable，优先 (3,1) 或 (0,3)/(0,4) 的 **format 4**（segCountX2/searchRange 全部按下标读；endCode/startCode/idDelta/idRangeOffset 四数组）；若仅 **format 12**（(3,10)/(0,4)）则用 groups 二分。查询 = 二分 endCode + 范围校验 + delta/rangeOffset 公式（`rangeOffset==0 ? (cp+delta)&0xFFFF : (raw + delta)&0xFFFF`，raw 读取越界防护）。
- [ ] Step 3: 字体常驻加载函数放 `ttf.c`：`int ttf_autoload(const char *path)` — `vfs_stat` 取大小 → `vmm_alloc_pages((size+4095)/4096)` → `vfs_read_into` → `ttf_load`；失败路径 `vmm_unmap_page` 逐页释放并返回 -1。`main.c` 在 `vfs_mount_data()` 成功后调用 `ttf_autoload("/mnt/WQY.TTC")` 并 kprintf：`[ttf ] loaded upem=%u glyphs=%u cmap=%s` / `[ttf ] no CJK font (%d)\n`。
- [ ] Step 4: 冒烟：`smoke.rb "" "[ttf ] loaded upem=2048"`（QEMU 512 MiB 足够）；再断言启动日志包含 cmap=fmt4。
- [ ] Step 5: 提交 `git commit -am "feat: TrueType TTC table and cmap parsing"`

---

### Task 6: 轮廓提取 + 定点光栅化 + 字形缓存 + fonttest

**Files:**
- Modify: `src/kernel/font/ttf.c`、`src/kernel/include/ttf.h`
- Modify: `src/apps/shell.cpp`（新命令 `fonttest`）

**Interfaces:**
- Produces（ttf.h 增）:

```c
struct ttf_glyph {
    u8 alpha[20 * 20];   /* 行主序 8-bit 覆盖率 0..255 */
    u8 w, h, adv;        /* adv 以半角=4/全角=8 单元格宽（*GLYPH_WIDTH/2 换算）*/
    i8 bx, by;           /* 相对单元格左上角的位图偏移 */
};
int ttf_render(u32 codepoint, struct ttf_glyph *out); /* 命中缓存或新栅格化 */
```

- Consumes: Task 5 的表访问原语
- [ ] Step 1: 轮廓：`glyf` 头（numberOfContours i16；简单轮廓 flags/on-curve/x/y 数组；重复计数优化 x=delta16/同值、y 同理）；`point_scale(v) = (v * (TARGET_PX<<6)) / upem`（i64 乘）；baseline：y 轴翻转 `py = (ascent<<6) - y`（ascent 取 hhea/OS2 任一，先用 hhea.ascender）。复合轮廓：`ARG_1AND2|XY_SCALE|BASIC_SCALE` 等 flag，递归深度上限 8，仅支持平移+1.0/2.0 缩放（wqy 复合基本是平移），未知 flag 则跳过该组件。
- [ ] Step 2: 二次贝塞尔展平：off/on 点序列 → 隐式 on 中点；每条 off 段 5 次对半细分（32 段，误差 <0.02px @2048upem/16px）成折线（26.6 i32 x/y）。
- [ ] Step 3: 扫描线（非零环绕）：目标位图 w×h 由全部折线 bbox 取整外扩 1px 并钳到 20；每像素行 4 条子扫描线（y+0.125/0.375/0.625/0.875 像素）；收集与该子行相交的折线段 → (xfix26.6, winding ±1)；排序、按 winding 累计跳变求覆盖区间；对每列像素求 `[col,col+1)` 与区间的交（1/64 px 精度）累加 `cov += span∩pixel_len`；`alpha = cov * 255 / (4*64)`，钳 0..255，折线端点用上一行连续性合并（简单起见允许 1 子行误差）。
- [ ] Step 4: 缓存：`kzalloc` 256 槽开放寻址表（key=codepoint），命中返回拷贝；满则 memset 清空重建。度量：advance 取 `hmtx[min(gid, numberOfHMetrics-1)].advanceWidth`。
- [ ] Step 5: shell `fonttest <UTF-8字符>`：`ttf_render` 后按行打印 `alpha>=128 ? '#' : alpha>=32 ? '+' : '.'`，空格为 '.'（串口可见字形形状 = 自动化验收）。
- [ ] Step 6: 冒烟：`smoke.rb "fonttest 中\nshutdown\n" "@@@"` 改为 marker `fonttest` 输出非空且形状对称（人工比对一次截屏/ASCII art）；`fonttest A` 可渲染。
- [ ] Step 7: 提交 `git commit -am "feat: TrueType glyf rasterizer with glyph cache and fonttest"`

---

### Task 7: GOP alpha 混合 + 控制台 UTF-8 宽字符排版

**Files:**
- Modify: `src/drivers/gop.ads`/`gop.adb`（`Blit_Alpha`/`gop_blit_alpha`）、`src/kernel/include/drivers.h`、`src/kernel/lib/console.c`

**Interfaces:**
- Consumes: Task 6 `ttf_render`
- Produces: `void console_putc(char)`（签名不变，内部 UTF-8 状态机）；CJK 占 2 列、16px 高；`cp<0x80` 行为与现状逐字节一致。

- [ ] Step 1: Ada 增：

```ada
procedure Blit_Alpha (Fb    : Unsigned_64; Pitch, X, Y, W, H : Unsigned_32;
                      Mask : Address; Fg, Bg : Unsigned_32);
-- Export => "gop_blit_alpha"；对每像素 a=mask(row*w+col)：
-- 0 -> Bg；255 -> Fg；否则逐通道 (Bg*(255-a)+Fg*a)/255（8-bit 分量按 fb 打包色直接整型混合：先解出 r/g/b 再混合，用 pack_color 输入即 24bit RGB，ada 侧按字节序拆开亦可 —— 简化：C 侧把 fg/bg 传为 0xRRGGBB 展开值，Blit_Alpha 内按 (fg>>16)&255 等三通道线性混合后按 fb 原样写 u32 —— 与现有 pack_color 约定一致，Ada 收到的已是打包好 u32，直接按通道 8bit 混合即可（通道间距 8bit 时成立；BGRX/RGBX 都满足）。
```

- [ ] Step 2: console.c：全局 `static u32 utf8_acc; static u32 utf8_left;`；`console_putc(c)`：串口仍 `serial_putc((u8)c)`（含 \n→\r\n 处理）；帧缓冲侧构码：起始字节判长度、延续字节 `0x80..0xBF` 累入 `utf8_acc`；得到码点后：`<0x80` 走现有 `draw_cell`；否则 `ttf_render` 成功 → 先算宽 `cols = (g.adv+7)/8`（1 或 2），行尾不足则换行；对 2 列字用 `gop_blit_alpha(x=cur*8, y=cur_row*16, w=g.w, h=g.h, mask, fg_pixel, bg_pixel)`，覆盖区内先 `gop_fill_rect` bg；未加载字体 → 现有 '?' 行为（每码点画一个 '?' 格）。
- [ ] Step 3: `main.c` 字体加载移到 `console_init` 之后即可（console 状态机自带无字体分支）；确认顺序：`vfs_mount_data` 在 `console_init` 后，无需改动。
- [ ] Step 4: 验证（半自动）：`make run QEMU_DISPLAY="-display gtk"` 截屏需人工；串口侧回归：`smoke.rb "ls /mnt\ncat /mnt/HELLO.TXT\nshutdown\n" "HELLO"` 通过、滚屏不死机；`cat /mnt/INFO.TXT` 等不回归。
- [ ] Step 5: 提交

---

### Task 8: 文案中文化 + 示例 + 版本 0.4.0 + 文档

**Files:**
- Modify: `src/kernel/main.c`（banner 中文 + `ZHAOXU_VERSION "0.4.0"`）、`src/apps/shell.cpp`（kVersion、banner/help/`run` 描述“执行 .zxe/ELF64 程序”、常用错误中文）、`src/initramfs/etc/motd`、`src/initramfs/etc/version`、`src/user/hello.c`（中文输出行）、`src/user/init.c`（如需）、`README.md`、`AGENTS.md`

**Interfaces:**
- Consumes: Task 7 的中文渲染
- Produces: 验收证据：串口 UTF-8 原文 + 帧缓冲汉字

- [ ] Step 1: 文案改动（字符串保持简短；C++ 源 UTF-8 直接写）。`hello.c` 增 `u_puts("[hello] 肇序 OS 用户态中文渲染测试\n");`。
- [ ] Step 2: 版本 5 处同步为 `0.4.0`（README、shell.cpp kVersion、main.c ZHAOXU_VERSION、motd、version）。
- [ ] Step 3: AGENTS.md：验证步骤 `run /mnt/HELLO.ZXE`（并注明 HELLO.ELF 用于兼容回归）、依赖加 Ruby（Windows interop 路径说明）、新增“可执行格式 .zxe v1”与“TrueType 中文渲染”两节、`WQY.TTC` 说明（GPL 字体例外）；README 增功能说明。
- [ ] Step 4: 全量验证：`make clean && make -j4 && nm -u out/kernel.elf`；smoke 序列 `run /mnt/HELLO.ZXE`/`run /mnt/HELLO.ELF`/marker `status 7`；`make iso && make run-iso`（无 /mnt → `[ttf ] no CJK font`，ASCII 正常）；帧缓冲人工确认中文（截图注明）。
- [ ] Step 5: 提交 `git commit -am "feat: CJK TrueType console + .zxe user format (0.4.0)"`

---

## Self-Review 结论

- Spec 覆盖：格式/管线/兼容/渲染/加载/文案/回退/测试均有对应 Task；ISO 场景降级在 Task 8 Step 4。
- 类型一致性：`zxe_header` 字段与 Task 1 Ruby 打包顺序逐一对应（32B 头 + 40B 段）；`ttf_render`/`gop_blit_alpha` 命名全文统一。
- 风险留白：光栅化 1 子行连续性误差、复合轮廓仅平移+基本缩放 —— 均在 spec 风险节内，验收以 fonttest/截屏兜底。
