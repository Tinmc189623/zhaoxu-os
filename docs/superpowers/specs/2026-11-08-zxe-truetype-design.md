# 设计：TrueType 中文渲染 + 自研 .zxe 可执行格式（0.4.0）

日期：2026-11-08
状态：已获批准（补充约束：loader 必须保留 ELF 兼容）

## 1. 目标与范围

1. 设计自研可执行文件格式 `.zxe`，替换所有 ring 3 用户程序产物（`init.elf`、`hello.elf` → `init.zxe`、`hello.zxe`）。内核 loader **同时支持 .zxe 与 ELF64**（按文件魔数分发），旧 ELF 程序仍可运行。
2. 在内核内实现 TrueType（glyf 轮廓）字体解析与光栅化，控制台支持 UTF-8 中文（CJK）渲染。字体使用开源文泉驿 Micro Hei（`wqy-microhei.ttc`，TTC/glyf，约 5.2 MB），存放于 FAT32 数据盘 `/mnt/WQY.TTC`。
3. Shell banner/help、`/etc/motd`、示例程序 `hello.zxe` 输出中文，作为验收用例。
4. 不涉及：BOOTX64.EFI（必须保持 UEFI PE/COFF）、kernel.elf 内部构建产物、调度器/内存布局变更、CFF 字体、字体 hinting。

## 2. .zxe 格式 v1

小端，全部 `u64`。文件 = 头 + 段描述符表 + 段数据。

```
头（32 字节）
  0   u8[4]   magic  'Z' 'X' 'E' 0x01
  4   u16     version = 1
  6   u16     machine = 0x003E (x86-64)
  8   u16     seg_count（1..16）
  10  u16     hdr_size = 32 + seg_count * 40（16 对齐后）
  12  u32     reserved = 0
  16  u64     entry（虚拟地址）
  24  u64     file_size（整个文件大小）

段描述符（每个 40 字节，紧随头）
  0   u64  vaddr     （必须 >= USER_LOAD_BASE，且页对齐）
  8   u64  file_off  （16 对齐，>= hdr_size）
  16  u64  file_size
  24  u64  mem_size  （>= file_size，超出部分清零 = .bss）
  32  u64  flags     （bit0 R，bit1 W，bit2 X）
  40  结束
```

校验规则与 `elf_load` 对齐：范围不越过 `USER_ADDRESS_LIMIT`、不重叠检查（可选）、溢出检查、`file_off + file_size <= file_size(总)`。flags 仅存储不强制执行（当前页权限仍按现有 U/S+RW 全开，行为与 ELF 路径一致）。

## 3. 构建链路

- 新增 `src/tools/zxe-pack.rb`（纯 Ruby，无 gem 依赖）：解析 ELF64 的 PT_LOAD 段与 e_entry，按上文格式写出 `.zxe`。ELF 变成中间产物（`build/%.elf`），最终产物只有 `.zxe`。
- Makefile：`$(OUT)/%.zxe: user/%.c`（先链接到 `$(BUILD)/$*.elf`，再 ruby 转换）；`RAMFS/bin/init` 取自 `init.zxe`（路径名保持 `/bin/init`）；`data.img` 打包 `HELLO.ZXE`（zxe 版）**和** `HELLO.ELF`（原 elf，用于 ELF 兼容回归）。
- Ruby 位置：WSL 内无 ruby，使用 Windows Ruby interop（`/mnt/c/Ruby40-x64/bin/ruby.exe`）；Makefile 变量 `RUBY ?= ruby.exe`，找不到时构建报错并提示（不静默安装系统依赖）。
- 字体文件入库：`src/initramfs/data/wqy.ttc` → mcopy 为 `::/WQY.TTC`（8.3 名，避免 LFN 大小写问题）。

## 4. Loader（process.c）

- 保留 `elf_load()` 原样；新增 `zxe_load()`；`process_run()` 按前 4 字节魔数分发：`ZXE\x01` → zxe，`\x7FELF` → elf，其余报错 “not a valid ZXE/ELF64 executable”。
- 页映射、栈构造、退出路径不变。

## 5. TrueType 中文渲染（kernel/font/ttf.c，新）

Freestanding、无浮点、无 SSE（`-mgeneral-regs-only` 下全部整数运算）。

- 解析：`ttcf` 头（选第 0 个字体）/ sfnt 表目录；必需表 `head`（unitsPerEm、indexToLocaFormat）、`hhea`、`maxp`、`cmap`（平台 3/编码 1 的 format 4；缺失时可回退 format 12）、`hmtx`、`loca`、`glyf`。跳过 hinting（不解释 `cvt/fpgm/prep`）。
- 轮廓：简单轮廓（on/off-point 二次贝塞尔，自适应细分）+ 复合轮廓（glyph 组件递归，含 x/y 偏移与基本缩放）。
- 光栅化：26.6 定点；扫描线 + 非零环绕；每像素垂直 4 次采样（1/16 行步进的整数覆盖宽度）合成 0..255 alpha 掩码。输出 per-glyph：宽高、x/y bearing、advance（按 hmtx，unitsPerEm → 16 px 缩放）。
- 缓存：256 槽哈希表（`ttf_init` 成功后 `kzalloc`，约 150 KiB，不占 BSS），键 = codepoint，存 alpha 掩码（上限 20×20 像素）+ 度量；满时全清（简单策略，够用）。
- 接口：`ttf_available()`、`ttf_render(cp, struct glyph_mask *)`（返回 alpha 掩码与宽高/advance）。
- 合成：`src/drivers/gop.ads/adb` 新增 `gop_blit_alpha(fb, pitch, x, y, w, h, mask, fg, bg)`（Ada，与现有 Blit_Glyph 同风格）；C 侧只调用。

## 6. 字体加载（不受 4 MiB 堆限制）

字体 5.2 MB > `HEAP_REGION_SIZE`(4 MiB)，不能走 `vfs_read_all`/kmalloc：

- 新增 `fat32_read_into(path, dst, cap)` / `ramfs_read_into` 与 `vfs_read_into()`（沿用现有簇读取循环，只是目标缓冲区由调用方给；簇暂存缓冲仍 kmalloc ≤32 KiB）。
- 新增 `kernel/font/fontdata.c`（或直接并入 ttf.c）：`vfs_stat` 得到大小 → `vmm_alloc_pages(ceil(size/4K))` 得内核 VA（AHCI 经 `vmm_get_physical` 正常）→ `vfs_read_into` 读入 → 交给 `ttf_init(ptr, size)`；映射常驻不释放。
- 时机：`main.c` 在 `vfs_mount_data()` 之后、`process_init()` 之前调用 `console_load_cjk_font("/mnt/WQY.TTC")`；失败仅打印 `[ttf ] no CJK font`，不影响启动。

## 7. 控制台 UTF-8 排版（console.c 改造）

- 保持 ASCII 路径完全不变（8×8 bitmap，8×16 单元格）；串口仍逐字节透传原始 UTF-8（宿主终端自行显示）。
- `console_putc` 改为 UTF-8 状态机（1~4 字节），帧缓冲侧按码点渲染：
  - `cp < 0x80`：走现有 `draw_cell`；
  - CJK/宽字符（ttf 判定，advance≥阈值或显式宽表：U+2E80..U+9FFF、U+FF00..U+FF60、U+3000..U+303F 等 + 全角标点）：16×16 渲染，占 2 列；不足两列时先换行；未加载字体时画 8×8 的 `?`（沿用现行为）；
  - 组合/其他 BMP 外字符：暂按方框处理（不阻塞验收）。
- `console_write_n`（sys_write 路径）自动获得同样能力。

## 8. 文案中文化

- `shell.cpp`：banner、help、命令描述（`run` 描述改为“执行 .zxe/ELF64 程序”）、常见错误提示 → 中文字符串字面量（UTF-8；C++ 侧无 `-fexec-charset` 需求，源码即 UTF-8）。
- `kernel/main.c`：启动 banner 两行中文 + `ZHAOXU_VERSION "0.4.0"`；`[init]`、`[gop]` 等日志保留英文前缀（可 grep），关键行加中文说明。
- `initramfs/etc/motd`：中文；`version`：0.4.0。
- `user/hello.c`：增加中文输出行（验收：串口看到 UTF-8 字节、帧缓冲看到汉字）。
- 版本按 AGENTS.md 同步 5 处（README、shell.cpp kVersion、main.c、motd、version）。

## 9. 错误处理与回退

- zxe 头校验失败 → 明确错误消息回到 shell，进程不进入 ring 3。
- 无 `/mnt`、字体文件缺失、ttcf/cmap 不识别 → `[ttf ]` 警告 + CJK 显示 `?`，ASCII 不受影响；HEADLESS（无帧缓冲）路径完全不变。
- 字体内存分配失败（vmm_alloc_pages 返回 0）→ 同样回退。
- 用户指针/范围校验沿用 `user_range_ok`。

## 10. 测试与验收（无自动化测试，QEMU 冒烟）

1. `make clean && make -j4`；`nm -u out/kernel.elf` 无未定义符号；`file out/hello.zxe` 不存在依赖，`ruby -c src/tools/zxe-pack.rb` 语法通过。
2. `make run`：banner 中文行（串口 UTF-8 透传可见）；`[ttf ] WQY.TTC loaded, N glyphs cached`（首字形渲染后统计）；`/bin/init` 自动运行返回 shell。
3. `run /mnt/HELLO.ZXE` → 退出码 7；`run /mnt/HELLO.ELF` → 退出码 7（ELF 兼容回归）；`cat /mnt/HELLO.TXT`、`ls /mnt` 正常。
4. 帧缓冲验证：`make run QEMU_DISPLAY="-display gtk"` 人工确认汉字显示、混排对齐、换行、滚屏不错位（无法自动化，报告中注明）。
5. `make iso && make run-iso`：无 /mnt 时中文降级为 `?`，其余功能不回归。
6. 用户异常测试不变（退出码 -14 等）。

## 11. 文档与规则同步（AGENTS.md）

- 验证步骤中 `HELLO.ELF` 条目改为 `HELLO.ZXE`（并保留 ELF 兼容说明）；依赖清单加 Ruby（Windows interop）；新增 .zxe 格式一节；说明字体来自 `src/initramfs/data/wqy.ttc`、GPL 字体例外、`RAMFS_FILES` 不变而 `data.img` 增加 `WQY.TTC`。

## 12. 风险

- 光栅化器是最大工作量（约 600~900 行 C + Ada blit），位图边界（bearing 为负、超出 18×16 槽的超宽字形）需要钳制。
- 512 MiB 下字体常驻 +5 MiB 物理内存：可接受。
- WSL 无 root：ruby 走 Windows interop、字体已入库，不再依赖 apt；如 `ruby.exe` interop 被禁用，构建会显式报错。
