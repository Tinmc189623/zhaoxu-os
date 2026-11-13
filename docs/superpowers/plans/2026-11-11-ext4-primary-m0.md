# ext4 主文件系统（M0）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 ext4 完整实现从内核搬到 Ring3 `/bin/ext4fs`，安装器与 VFS 通过 RPC 使用它；安装盘上内核只存在于 ext4 分区，FAT32 只保留 ESP/兼容；修复 legacy BIOS 从 ISO 启动。

**Architecture:** `src/fs/ext4/` 是与 OS 无关的 ext4 核心（读、写、格式化、以后的日志/校验和），通过 `ext4_env` 的 read/write/flush/alloc/log 回调访问块设备与内存。Ring3 服务 `/bin/ext4fs` 用新增的受信 `blk_*` 系统调用做 I/O，内核分配控制页+数据页并通过 `spawn_arg` 传物理地址。内核 VFS 把 `/mnt` 请求转发给服务；安装器把 ext4 分区交给服务，自己只写 MBR/GPT/ESP/stage2。

**Tech Stack:** C（freestanding，内核与 Ring3），FASM（BIOS 引导），Ruby（全部构建/测试脚本，规则 6），QEMU + OVMF/SeaBIOS，e2fsprogs（主机测试）。

**Spec:** `docs/superpowers/specs/2026-11-11-ext4-primary-design.md`

## Global Constraints

- 内核名称 `Yuanshuo Kernel`，系统 `Zhaoxu OS`，Shell `csh`，桌面 `ADE`（Aqua），窗口管理器 `DWM`；不得改写这些名称。
- 构建在 WSL2 `Ubuntu-26.04` 内、工作目录 `src/`；Ruby 脚本从 Windows 侧运行（WSL 无 ruby）。
- 内核二进制不得包含 ext4/驱动/应用代码；`nm out/kernel.elf | grep -E '^.* ext4_'` 必须为空。
- 内核只允许位于 ext4 分区：MBR/stage1/stage2/ESP 不得出现内核 ELF；ESP 仅 `EFI/BOOT/BOOTX64.EFI`。
- 文件系统必须同时支持 MBR 与 GPT；ESP 必须 FAT32。
- 一个文件只做一件事；新增脚本一律 Ruby 且由 AI 生成。
- 不增加内核态 ext4 回退：M0 结束时内核不再编译 `kernel/fs/ext4*.c`。
- 提交步骤是检查点：**只有用户明确授权时才执行 `git commit`**；未授权时改为运行 `git diff --stat` 并停下汇报。
- 每次改动后至少运行一次 `make -j4`；涉及启动的改动再跑对应 QEMU 冒烟。

---

## 文件结构（M0 结束态）

```
src/fs/ext4/ext4_env.h        环境回调与日志
src/fs/ext4/ext4_env.c        env 存取、日志适配
src/fs/ext4/ext4.h            对外 API（挂载/读/写/格式化/list 回调）
src/fs/ext4/ext4_core.h       内部类型与跨文件声明
src/fs/ext4/ext4_mount.c      超级块、组描述符、探测与挂载
src/fs/ext4/ext4_extent.c     extent 树查找与文件数据读取
src/fs/ext4/ext4_dir.c        目录遍历、查找、list/stat
src/fs/ext4/ext4_read.c       read_all / read_into
src/fs/ext4/ext4_alloc.c      位图、块/inode 分配、super/gdt 同步
src/fs/ext4/ext4_write.c      文件写入、目录增删、unlink、truncate
src/fs/ext4/ext4_format.c     format_volume / make_root
src/apps/ext4fs/main.c        服务入口、RPC 循环
src/apps/ext4fs/service.c     RPC 分发与 list 序列化
src/apps/ext4fs/blk.c         blk_* 系统调用封装与 ext4_env 实现
src/apps/ext4fs/arena.c       Ring3 固定 arena 分配器
src/kernel/include/fs_rpc.h   内核与服务共享协议
src/kernel/proc/fsrpc.c       控制页、数据页、调用与超时
src/tests/ext4/host_main.c    主机测试程序
src/tests/ext4/host_io.c      文件映射块设备与 libc 适配
src/tests/ext4/include/types.h 主机用的定长整数
src/tools/ext4-test.rb        主机矩阵测试（Ruby）
src/tools/iso-bios.rb         ISO9660/El Torito 回填（Ruby）
```

---

### Task 1: 抽取与 OS 无关的 ext4 核心

**Files:**
- Create: `src/fs/ext4/ext4_env.h`, `src/fs/ext4/ext4_env.c`, `src/fs/ext4/ext4_core.h`, `src/fs/ext4/ext4.h`
- Create (by splitting): `src/fs/ext4/ext4_mount.c`, `ext4_extent.c`, `ext4_dir.c`, `ext4_read.c`, `ext4_alloc.c`, `ext4_write.c`, `ext4_format.c`
- Leave in place until Task 5: `src/kernel/fs/ext4.c`, `src/kernel/fs/ext4_write.c`, `src/kernel/fs/ext4_internal.h`, `src/kernel/include/ext4.h`（过渡期保留，任务 5 删除）

**Interfaces:**
- Consumes: 现有 `kernel/fs/ext4.c`、`kernel/fs/ext4_write.c` 的全部逻辑。
- Produces:
  - `void ext4_env_set(const struct ext4_env *env);`
  - `struct ext4_env` 见下方代码。
  - `int ext4_mount(void); int ext4_mount_at(int disk, u64 base);`
  - `int ext4_list(const char *path, ext4_list_cb cb, void *ctx);`
  - `int ext4_read_into(const char *path, u8 *dst, u64 cap, u64 *out);`
  - `int ext4_read_at(const char *path, u64 offset, u8 *dst, u64 cap, u64 *out);`
  - `int ext4_stat(const char *path, int *is_dir, u64 *size);`
  - `int ext4_write_file(const char *path, const u8 *data, u64 size);`
  - `int ext4_write_file_contig(const char *path, const u8 *data, u64 size, u32 *first, u32 *blocks);`
  - `int ext4_unlink(const char *path);`
  - `int ext4_format_volume(int disk, u64 base, u32 sectors, const char *label);`
  - `int ext4_make_root(int disk, u64 base, u32 sectors, const char *label, const u8 *elf, u64 elf_size, const u8 *bin, u64 bin_size, const u8 *font, u64 font_size, u32 *bin_block, u32 *bin_blocks);`
  - `int ext4_file_extent(const char *path, u32 *start, u32 *blocks);`
  - `struct ext4_state { u8 bytes[256]; };`
  - `struct ext4_state ext4_state_save(void); void ext4_state_restore(struct ext4_state state);`

- [ ] **Step 1: 写 `src/fs/ext4/ext4_env.h`**

```c
#ifndef ZHAOXU_EXT4_ENV_H
#define ZHAOXU_EXT4_ENV_H

#include <types.h>

struct ext4_env {
    int (*read)(int disk, u64 base, u64 offset, void *buffer, u32 length);
    int (*write)(int disk, u64 base, u64 offset, const void *buffer,
                 u32 length);
    int (*flush)(int disk);
    void *(*alloc)(u32 bytes);
    void (*free)(void *pointer);
    void (*log)(const char *text);
    int (*disk_count)(void);
    u64 (*disk_sectors)(int disk);
};

void ext4_env_set(const struct ext4_env *env);
const struct ext4_env *ext4_env(void);
void *ext4_alloc(u32 bytes);
void ext4_free(void *pointer);
void ext4_logf(const char *format, ...);

#endif
```

- [ ] **Step 2: 写 `src/fs/ext4/ext4_env.c`**

```c
#include "ext4_env.h"
#include <kprintf.h>
#include <string.h>

static struct ext4_env g_env;

void ext4_env_set(const struct ext4_env *env)
{
    g_env = *env;
}

const struct ext4_env *ext4_env(void)
{
    return &g_env;
}

void *ext4_alloc(u32 bytes)
{
    return g_env.alloc ? g_env.alloc(bytes) : 0;
}

void ext4_free(void *pointer)
{
    if (g_env.free)
        g_env.free(pointer);
}

void ext4_logf(const char *format, ...)
{
    if (!g_env.log)
        return;
    char text[160];
    __builtin_va_list args;
    __builtin_va_start(args, format);
    kvsnprintf(text, sizeof(text), format, args);
    __builtin_va_end(args);
    g_env.log(text);
}
```

- [ ] **Step 3: 写 `src/fs/ext4/ext4.h`（对外 API）**

```c
#ifndef ZHAOXU_EXT4_H
#define ZHAOXU_EXT4_H

#include <types.h>

#define EXT4_MAX_FILE_SIZE (16 * 1024 * 1024)

typedef void (*ext4_list_cb)(const char *name, u64 size, int is_dir,
                             void *context);

struct ext4_state {
    u8 bytes[256];
};

int ext4_mount(void);
int ext4_mounted(void);
const char *ext4_label(void);
int ext4_list(const char *path, ext4_list_cb callback, void *context);
int ext4_read_all(const char *path, u8 **buffer, u64 *size);
int ext4_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int ext4_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                 u64 *read_out);
int ext4_stat(const char *path, int *is_dir, u64 *size);
int ext4_write_file(const char *path, const u8 *data, u64 size);
int ext4_write_file_contig(const char *path, const u8 *data, u64 size,
                           u32 *first, u32 *blocks);
int ext4_unlink(const char *path);
int ext4_mount_at(int disk, u64 volume_base);
int ext4_format_volume(int disk, u64 volume_base, u32 sectors,
                       const char *label);
int ext4_make_root(int disk, u64 volume_base, u32 sectors, const char *label,
                   const u8 *kernel_elf, u64 kernel_elf_size,
                   const u8 *kernel_bin, u64 kernel_bin_size,
                   const u8 *font, u64 font_size,
                   u32 *bin_block, u32 *bin_blocks);
int ext4_file_extent(const char *path, u32 *start_block, u32 *blocks);
struct ext4_state ext4_state_save(void);
void ext4_state_restore(struct ext4_state state);

#endif
```

- [ ] **Step 4: 拆分现有源码**

把 `src/kernel/fs/ext4.c` 与 `src/kernel/fs/ext4_write.c` **复制**到 `src/fs/ext4/`（旧文件本轮保留，保证内核仍可编译；任务 5 再删除），按下面的函数归属拆分（函数名逐一搬移，不改逻辑）：

| 新文件 | 从旧文件搬入的函数 |
|---|---|
| `ext4_mount.c` | `g_ext4`、`ext4_read_block`、`ext4_write_block`、`ext4_read_group_desc`、`ext4_read_inode_raw`、`ext4_mount_at`、`probe_volume`、`consider`、`ext4_mount`、`ext4_mounted`、`ext4_label`、`ext4_clear_state`、`ext4_read_super`、`ext4_fill_fs`、`ext4_state_save`、`ext4_state_restore` |
| `ext4_extent.c` | `le16/le32/le64`、`extent_lookup`、`ext4_map_extent`、`ext4_read_inode_data` |
| `ext4_dir.c` | `ext4_dir_next`、`ext4_inode_dir_find`、`walk_path`、`ext4_lookup`、`ext4_list`、`ext4_stat`、`ext4_file_extent` |
| `ext4_read.c` | `ext4_read_all`、`ext4_read_into` |
| `ext4_alloc.c` | `le16/le32/align4`（写侧副本）、`flush_bitmap`、`bitmap_invalidate`、`bitmap_load/test/set/clear`、`gd_*`、`ext4_write_group_desc`、`group_blocks_in_fs`、`fmt_has_super`、`ext4_alloc_run`、`ext4_free_run`、`ext4_alloc_inode`、`ext4_free_inode`、`ext4_write_inode_raw`、`inode_*`、`ext4_truncate`、`ext4_update_super`、`ext4_sync` |
| `ext4_write.c` | `read_dir_block`、`write_dir_block`、`ext4_dir_add`、`ext4_dir_remove`、`ext4_create_node`、`split_parent`、`lookup_child`、`ext4_write_file`、`ext4_write_file_contig`、`ext4_unlink` |
| `ext4_format.c` | `put_inode`、`zero_sectors`、`fs_has_super`、`group_inode_blocks`、`ext4_format_volume`、`ext4_make_root` |

拆分后跨文件使用的 `le16/le32/le64`、`align4`、`EXT4_*` 宏统一放进
`ext4_core.h`，改成 `static inline` 的 `ext4_le16/ext4_le32/ext4_le64/
ext4_align4`，各文件只用这些前缀版本，避免多份 `static` 符号语义漂移。
`g_ext4`、`walk_path` 等确实需要跨文件的声明也放 `ext4_core.h`（非
`static`）。

- [ ] **Step 5: 把块设备/内存/日志调用改为 env**

在 `ext4_mount.c`：

```c
int ext4_read_block(u64 block, void *buffer)
{
    const struct ext4_env *env = ext4_env();
    if (!g_ext4.mounted || !env->read)
        return -1;
    return env->read(g_ext4.disk, g_ext4.volume_base,
                     block * g_ext4.block_size, buffer,
                     g_ext4.block_size);
}

int ext4_write_block(u64 block, const void *buffer)
{
    const struct ext4_env *env = ext4_env();
    if (!g_ext4.mounted || !g_ext4.writable || !env->write)
        return -1;
    return env->write(g_ext4.disk, g_ext4.volume_base,
                      block * g_ext4.block_size, buffer,
                      g_ext4.block_size);
}
```

全局替换（仅 `src/fs/ext4/`）：`kmalloc(` → `ext4_alloc(`，`kfree(` → `ext4_free(`，`kprintf(` → `ext4_logf(`。

块 I/O 的映射规则（env 的 `base` 是分区起始扇区，`offset` 是分区内字节偏移）：

- 挂载后：`block_disk_read(disk, lba, count, buf)` →
  `ext4_env()->read(disk, g_ext4.volume_base, (u64)lba * 512, buf, count * 512)`。
  写同理用 `->write`。
- 挂载前/格式化/探测：用调用参数里的 `base`，不要用 `g_ext4`。
  在 `ext4_mount.c` / `ext4_format.c` 各放一个本地 helper：
  ```c
  static int raw_read(int disk, u64 base, u64 offset, void *buffer, u32 length)
  {
      return ext4_env()->read(disk, base, offset, buffer, length);
  }
  static int raw_write(int disk, u64 base, u64 offset, const void *buffer,
                       u32 length)
  {
      return ext4_env()->write(disk, base, offset, buffer, length);
  }
  ```
- `block_disk_count()` → `ext4_env()->disk_count()`；
  `block_disk_sectors(d)` → `ext4_env()->disk_sectors(d)`；
  `block_disk_flush(d)` → `ext4_env()->flush(d)`。

注意：`ext4_read_super(disk, base, sb)` 里原来的
`block_disk_read(disk, base + 2, EXT4_SB_SECTORS, sb)` 要改成
`raw_read(disk, base, 1024, sb, 1024)`（分区内偏移 1024）。

同时把内核专有头替换掉：删除 `#include <block.h>`、`<mem.h>`，
`<kprintf.h>` 只保留在 `ext4_env.c`；其余文件只 include
`"ext4_env.h"`、`"ext4_core.h"`、`<string.h>`、`<types.h>`。

`ext4_state_save/restore`：

```c
struct ext4_state ext4_state_save(void)
{
    struct ext4_state state;
    _Static_assert(sizeof(state.bytes) >= sizeof(g_ext4), "state too small");
    memcpy(state.bytes, &g_ext4, sizeof(g_ext4));
    return state;
}

void ext4_state_restore(struct ext4_state state)
{
    memcpy(&g_ext4, state.bytes, sizeof(g_ext4));
}
```

- [ ] **Step 6: 加 `ext4_write_file_contig`**

在 `ext4_write.c` 里新增（复用现有的 `ext4_create_node`、
`inode_init_extents`、`inode_extent_append`、`ext4_write_block`）：

```c
int ext4_write_file_contig(const char *path, const u8 *data, u64 size,
                           u32 *first, u32 *blocks)
{
    char parent[256];
    char name[256];
    if (split_parent(path, parent, name) != 0)
        return -1;
    u32 parent_ino = 0;
    if (ext4_lookup(parent, &parent_ino) != 0)
        return -1;

    u32 count = (u32)((size + g_ext4.block_size - 1) / g_ext4.block_size);
    u32 start = 0;
    if (ext4_alloc_run(count, &start) != 0)
        return -1;

    u32 ino = 0;
    if (ext4_create_node(parent_ino, name, 0100644, EXT4_FT_REG_FILE,
                         &ino) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    inode_init_extents(inode);
    if (inode_extent_append(inode, 0, start, count) != 0)
        return -1;
    inode_set_size(inode, (u32)size);
    ext4_put32(inode + EXT4_INODE_BLOCKS_OFF, count * 8);

    u8 *block = ext4_alloc(g_ext4.block_size);
    if (!block)
        return -1;
    for (u32 i = 0; i < count; i++) {
        u32 bytes = g_ext4.block_size;
        if ((u64)(i + 1) * g_ext4.block_size > size)
            bytes = (u32)(size - (u64)i * g_ext4.block_size);
        memset(block, 0, g_ext4.block_size);
        memcpy(block, data + (u64)i * g_ext4.block_size, bytes);
        if (ext4_write_block(start + i, block) != 0) {
            ext4_free(block);
            return -1;
        }
    }
    ext4_free(block);
    if (ext4_write_inode_raw(ino, inode) != 0)
        return -1;
    if (first)
        *first = start;
    if (blocks)
        *blocks = count;
    return ext4_sync();
}
```

`split_parent`/`inode_set_size`/`EXT4_INODE_BLOCKS_OFF` 已在核心内部；
如果它们是 `static`，在同一文件内可直接用。

同一个文件里补 `ext4_read_at`（`ext4_read.c` 里也可以）：

```c
int ext4_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                 u64 *read_out)
{
    u32 ino = 0;
    if (ext4_lookup(path, &ino) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if ((ext4_le16(inode) & EXT4_S_IFDIR) != 0)
        return -1;
    u64 read = 0;
    if (ext4_read_inode_data(inode, offset, dst, cap, &read) != 0)
        return -1;
    if (read_out)
        *read_out = read;
    return 0;
}
```

- [ ] **Step 7: 主机编译验证**

```bash
cd src
gcc -std=gnu11 -O2 -Wall -Wextra -Ifs/ext4 -Itests/ext4/include \
    -DEXT4_HOST -c fs/ext4/ext4_mount.c -o /tmp/m.o
```
Expected: 编译通过；若缺 `<types.h>` 用 `tests/ext4/include/types.h` 提供。

- [ ] **Step 8: 检查点**

```bash
git status --porcelain src/fs/ext4 src/kernel/fs src/kernel/include
git diff --stat
```
Expected: 新目录存在，旧 ext4 文件仍保留（任务 5 删除），`make -j4` 仍通过；提交本轮改动。

---

### Task 2: 主机测试夹具与格式化自检

**Files:**
- Create: `src/tests/ext4/include/types.h`, `src/tests/ext4/host_io.c`, `src/tests/ext4/host_main.c`
- Create: `src/tools/ext4-test.rb`

**Interfaces:**
- Consumes: Task 1 的 `ext4_*` API 与 `ext4_env`。
- Produces: `out/ext4-test` 主机二进制；`ruby src/tools/ext4-test.rb` 返回 0/1。

- [ ] **Step 1: 写主机兼容头**

`src/tests/ext4/include/types.h`：

```c
#ifndef ZHAOXU_TEST_TYPES_H
#define ZHAOXU_TEST_TYPES_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;  typedef uint16_t u16; typedef uint32_t u32;
typedef uint64_t u64; typedef int8_t i8; typedef int16_t i16;
typedef int32_t i32; typedef int64_t i64; typedef size_t usize;
typedef unsigned long long uintptr_t;
#endif
```

`src/tests/ext4/include/kprintf.h`：

```c
#ifndef ZHAOXU_TEST_KPRINTF_H
#define ZHAOXU_TEST_KPRINTF_H
#include <stdarg.h>
#include <stdio.h>
static inline int kvsnprintf(char *out, unsigned long capacity,
                             const char *fmt, va_list ap)
{
    return vsnprintf(out, capacity, fmt, ap);
}
#endif
```

- [ ] **Step 2: 写 `host_io.c`**

```c
#define _GNU_SOURCE
#include "ext4_env.h"
#include "ext4.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_fd;

static int host_read(int disk, u64 base, u64 offset, void *buffer, u32 length)
{
    (void)disk;
    (void)base;
    return pread(g_fd, buffer, length, (off_t)offset) == (ssize_t)length ? 0 : -1;
}

static int host_write(int disk, u64 base, u64 offset, const void *buffer,
                      u32 length)
{
    (void)disk;
    (void)base;
    return pwrite(g_fd, buffer, length, (off_t)offset) == (ssize_t)length ? 0 : -1;
}

static int host_flush(int disk) { (void)disk; return fsync(g_fd) == 0 ? 0 : -1; }
static void *host_alloc(u32 bytes) { return calloc(1, bytes); }
static void host_free(void *p) { free(p); }
static void host_log(const char *text) { fputs(text, stderr); }
static int host_disks(void) { return 1; }
static u64 host_sectors(int disk) { (void)disk; return 0; }

int host_env_open(const char *image)
{
    g_fd = open(image, O_RDWR);
    if (g_fd < 0)
        return -1;
    struct ext4_env env = {
        .read = host_read, .write = host_write, .flush = host_flush,
        .alloc = host_alloc, .free = host_free, .log = host_log,
        .disk_count = host_disks, .disk_sectors = host_sectors,
    };
    ext4_env_set(&env);
    return 0;
}
```

- [ ] **Step 3: 写 `host_main.c`**

命令：`mount <base_sector>`、`ls <path>`、`cat <path>`、`write <path> <size>`、
`contig <path> <size>`、`rm <path>`、`stat <path>`、`sync`。

```c
#define _GNU_SOURCE
#include "ext4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int host_env_open(const char *image);

static void list_cb(const char *name, u64 size, int is_dir, void *ctx)
{
    (void)ctx;
    printf("%c %llu %s\n", is_dir ? 'd' : 'f',
           (unsigned long long)size, name);
}

int main(int argc, char **argv)
{
    if (argc < 2 || host_env_open(argv[1]) != 0)
        return 2;
    for (int i = 2; i < argc; i++) {
        char *command = argv[i];
```c
        if (!strcmp(command, "format") && i + 1 < argc) {
            u32 sectors = (u32)strtoul(argv[++i], 0, 0);
            if (ext4_format_volume(0, 0, sectors, "ZTEST") != 0) return 12;
            if (ext4_mount_at(0, 0) != 0) return 13;
        } else if (!strcmp(command, "mount") && i + 1 < argc) {
            if (ext4_mount_at(0, strtoull(argv[++i], 0, 0)) != 0) return 3;
        } else if (!strcmp(command, "ls") && i + 1 < argc) {
            if (ext4_list(argv[++i], list_cb, 0) != 0) return 4;
        } else if (!strcmp(command, "cat") && i + 1 < argc) {
            u8 *data = 0; u64 size = 0;
            if (ext4_read_all(argv[++i], &data, &size) != 0) return 5;
            fwrite(data, 1, (size_t)size, stdout);
        } else if (!strcmp(command, "write") && i + 2 < argc) {
            const char *path = argv[++i];
            u64 size = strtoull(argv[++i], 0, 0);
            u8 *data = malloc((size_t)(size ? size : 1));
            for (u64 k = 0; k < size; k++) data[k] = (u8)('a' + k % 26);
            if (ext4_write_file(path, data, size) != 0) return 6;
            free(data);
        } else if (!strcmp(command, "contig") && i + 2 < argc) {
            const char *path = argv[++i];
            u64 size = strtoull(argv[++i], 0, 0);
            u8 *data = malloc((size_t)(size ? size : 1));
            for (u64 k = 0; k < size; k++) data[k] = (u8)(k & 0xFF);
            u32 first = 0, blocks = 0;
            if (ext4_write_file_contig(path, data, size, &first, &blocks) != 0)
                return 7;
            printf("contig %u %u\n", first, blocks);
            free(data);
        } else if (!strcmp(command, "rm") && i + 1 < argc) {
            if (ext4_unlink(argv[++i]) != 0) return 8;
        } else if (!strcmp(command, "stat") && i + 1 < argc) {
            int is_dir = 0; u64 size = 0;
            if (ext4_stat(argv[++i], &is_dir, &size) != 0) return 9;
            printf("%c %llu\n", is_dir ? 'd' : 'f',
                   (unsigned long long)size);
        } else if (!strcmp(command, "sync")) {
            if (ext4_sync() != 0) return 10;
        } else {
            fprintf(stderr, "bad command %s\n", command);
            return 11;
        }
    }
    return 0;
}
```

- [ ] **Step 4: 写 `src/tools/ext4-test.rb`**

脚本在 Windows Ruby 下运行，通过 `wsl.exe` 在 WSL 内编译与执行：

```ruby
#!/usr/bin/env ruby
# ext4-test.rb -- 主机矩阵测试：格式化 -> 读写 -> e2fsck。
require "open3"
DISTRO = ENV["EXT4_TEST_DISTRO"] || "Ubuntu-26.04"
SRC_WIN = File.expand_path("..", __dir__)          # .../src
DRIVE = SRC_WIN[0].downcase
SRC = "/mnt/#{DRIVE}#{SRC_WIN[2..].tr('\\', '/')}"

def wsl(script)
  out, status = Open3.capture2e("wsl", "-d", DISTRO, "--", "bash", "-lc", script)
  [out, status.success?]
end

build = <<~SH
  cd "#{SRC}" || exit 1
  mkdir -p build/ext4-test
  gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Wall -Wextra \
      -I fs/ext4 -I tests/ext4/include -DEXT4_HOST \
      -o build/ext4-test/ext4-test \
      tests/ext4/host_main.c tests/ext4/host_io.c \
      fs/ext4/ext4_env.c fs/ext4/ext4_mount.c fs/ext4/ext4_extent.c \
      fs/ext4/ext4_dir.c fs/ext4/ext4_read.c fs/ext4/ext4_alloc.c \
      fs/ext4/ext4_write.c fs/ext4/ext4_format.c -lrt
SH
out, ok = wsl(build)
abort "build failed:\n#{out}" unless ok

failures = 0

# 阶段 A：我们自己格式化的卷，读写 + e2fsck 必须干净。
image = "/tmp/zos-ext4-own.img"
script = <<~SH
  set -e
  rm -f #{image}
  dd if=/dev/zero of=#{image} bs=1M count=32 status=none
  cd "#{SRC}"
  ./build/ext4-test/ext4-test #{image} format 65536
  ./build/ext4-test/ext4-test #{image} mount 0 write /HELLO.TXT 70000 \
      stat /HELLO.TXT cat /HELLO.TXT sync
  ./build/ext4-test/ext4-test #{image} mount 0 rm /HELLO.TXT sync ls /
  e2fsck -fn #{image}
SH
out, ok = wsl(script)
puts(ok ? "PASS own-format rw" : "FAIL own-format rw")
failures += 1 unless ok
puts out unless ok

# 阶段 B：mkfs.ext4 矩阵只读（写互操作属于 M2）。
cases = []
[1024, 2048, 4096].each do |bs|
  [[], ["-O 64bit"], ["-O metadata_csum"], ["-O ^has_journal"]].each do |opt|
    cases << { block: bs, opts: opt }
  end
end

cases.each do |c|
  image = "/tmp/zos-ext4-#{c[:block]}-#{c[:opts].join.tr(' ^-', '')}.img"
  script = <<~SH
    set -e
    rm -f #{image}
    dd if=/dev/zero of=#{image} bs=1M count=24 status=none
    mke2fs -q -F -t ext4 -b #{c[:block]} #{c[:opts].join(' ')} #{image}
    debugfs -w -R "write /etc/hostname /HELLO.TXT" #{image} >/dev/null 2>&1 || true
    cd "#{SRC}"
    ./build/ext4-test/ext4-test #{image} mount 0 ls / stat /HELLO.TXT
  SH
  out, ok = wsl(script)
  if ok
    puts "PASS read-only block=#{c[:block]} opts=#{c[:opts].inspect}"
  else
    failures += 1
    puts "FAIL read-only block=#{c[:block]} opts=#{c[:opts].inspect}"
    puts out
  end
end
exit(failures.zero? ? 0 : 1)
```

- [ ] **Step 5: 运行并修复格式化问题**

Run: `ruby src/tools/ext4-test.rb`
Expected: 首轮会失败。用 `e2fsck -fn` 输出逐项修正 `ext4_format_volume`：
- 超级块：`s_reserved_gdt_blocks` 与 `s_blocks_count` 对应；`s_state = 1`
  （clean）；`s_max_mnt_count` 非零；`s_lastcheck/s_mtime` 可置 0。
- 组描述符：`bg_used_dirs_count` 组 0 为 2；`bg_free_blocks/inodes`
  与位图一致；组 0 的根目录块与 lost+found 块在位图中置位。
- inode：根目录 `i_links_count` 至少 3，`lost+found` 为 2；
  `i_blocks` 按 512 字节计；`i_generation` 可 0。
- 若启用了 `metadata_csum` 的镜像，M0 **只读**，测试脚本对这类镜像
  只跑 `mount/ls/cat`，不跑写与 `e2fsck` 断言（写互操作在 M2）。

- [ ] **Step 6: 检查点**

Run: `ruby src/tools/ext4-test.rb`
Expected: `PASS` 覆盖 1K/2K/4K 与我们支持的选项；退出码 0。

---

### Task 3: RPC 协议、受信块调用与 `/bin/ext4fs` 骨架

**Files:**
- Create: `src/kernel/include/fs_rpc.h`
- Create: `src/kernel/proc/fsrpc.c`
- Modify: `src/kernel/proc/process_internal.h`, `src/kernel/proc/process.c`, `src/kernel/include/process.h`
- Modify: `src/kernel/proc/syscall.c`, `src/user/userlib.h`
- Create: `src/apps/ext4fs/main.c`, `service.c`, `blk.c`, `arena.c`
- Modify: `src/Makefile`, `src/kernel/main.c`

**Interfaces:**
- Produces:
  - `int fs_service_start(void);`（等待 READY）
  - `int fs_service_ready(void);`
  - `u8 *fs_data(void);`（内核侧数据页）
  - 精确调用（全部返回 0 成功 / -1 失败）：
    ```c
    int fs_call_mount(void);
    int fs_call_stat(const char *path, int *is_dir, u64 *size);
    int fs_call_list(const char *path, u32 cursor, void *out, u32 cap,
                     u32 *written, u32 *next_cursor, u32 *count);
    int fs_call_read(const char *path, u64 offset, void *dst, u32 length,
                     u32 *got);
    int fs_call_write(const char *path, u64 total, u64 offset,
                      const void *src, u32 length, int truncate_first);
    int fs_call_unlink(const char *path);
    int fs_call_sync(void);
    int fs_call_format(int disk, u64 base, u32 sectors, const char *label);
    int fs_call_write_contig(const char *path, u64 total, u64 offset,
                             const void *src, u32 length, u32 *first,
                             u32 *blocks);
    const char *fs_label(void);
    ```
  - 系统调用 38..42：`blk_count/blk_info/blk_read/blk_write/blk_flush`
  - `process_block_capable()`。

- [ ] **Step 1: 写共享协议 `fs_rpc.h`**

```c
#ifndef ZHAOXU_FS_RPC_H
#define ZHAOXU_FS_RPC_H

#include <types.h>

#define FS_RPC_MAGIC 0x3146535Au /* "ZSF1" */
#define FS_DATA_SIZE 4096u

enum fs_state {
    FS_STATE_IDLE = 0,
    FS_STATE_REQUEST = 1,
    FS_STATE_DONE = 2,
    FS_STATE_READY = 3
};

enum fs_op {
    FS_OP_NONE = 0,
    FS_OP_MOUNT = 1,
    FS_OP_STAT = 2,
    FS_OP_LIST = 3,
    FS_OP_READ = 4,
    FS_OP_WRITE = 5,
    FS_OP_UNLINK = 6,
    FS_OP_SYNC = 7,
    FS_OP_FORMAT = 8,
    FS_OP_WRITE_CONTIG = 9
};

#define FS_FLAG_TRUNCATE 0x0001u

struct fs_rpc {
    volatile u32 magic;
    volatile u32 state;
    u32 op;
    i32 status;
    u32 flags;
    u32 length;
    u64 offset;
    u64 value0;
    u64 value1;
    u64 data_phys;
    char path[256];
    char label[17];
};

#endif
```

- [ ] **Step 2: 写 `fsrpc.c`**

```c
#include <fs_rpc.h>
#include <mem.h>
#include <process.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <types.h>

#define FS_WAIT_MS 5000

static struct fs_rpc *g_rpc;
static u8 *g_data;
static int g_ready;
static volatile int g_busy;

u8 *fs_data(void) { return g_data; }

int fs_service_ready(void) { return g_ready; }

static int wait_done(void)
{
    for (int waited = 0; waited < FS_WAIT_MS; waited++) {
        if (g_rpc->state == FS_STATE_DONE)
            return 0;
        thread_sleep_ms(1);
    }
    g_rpc->state = FS_STATE_IDLE;
    return -1;
}

static int fs_call_values(u32 op, const char *path, u64 offset, u32 length,
                          u32 flags, u64 value0, u64 value1)
{
    while (__atomic_test_and_set(&g_busy, __ATOMIC_ACQUIRE))
        thread_sleep_ms(1);
    g_rpc->op = op;
    g_rpc->status = -1;
    g_rpc->flags = flags;
    g_rpc->length = length;
    g_rpc->offset = offset;
    g_rpc->value0 = value0;
    g_rpc->value1 = value1;
    g_rpc->path[0] = 0;
    if (path) {
        usize n = 0;
        while (path[n] && n + 1 < sizeof(g_rpc->path)) {
            g_rpc->path[n] = path[n];
            n++;
        }
        g_rpc->path[n] = 0;
    }
    __sync_synchronize();
    g_rpc->state = FS_STATE_REQUEST;
    int status = wait_done() == 0 ? g_rpc->status : -1;
    g_rpc->state = FS_STATE_IDLE;
    __atomic_clear(&g_busy, __ATOMIC_RELEASE);
    return status;
}

static int fs_call(u32 op, const char *path, u64 offset, u32 length,
                   u32 flags)
{
    return fs_call_values(op, path, offset, length, flags, 0, 0);
}
```

int fs_call_mount(void)
{
    return fs_call(FS_OP_MOUNT, 0, 0, 0, 0);
}

int fs_call_stat(const char *path, int *is_dir, u64 *size)
{
    if (fs_call(FS_OP_STAT, path, 0, 0, 0) != 0)
        return -1;
    if (is_dir)
        *is_dir = (int)g_rpc->value0;
    if (size)
        *size = g_rpc->value1;
    return 0;
}

int fs_call_list(const char *path, u32 cursor, void *out, u32 cap,
                 u32 *written, u32 *next_cursor, u32 *count)
{
    if (fs_call(FS_OP_LIST, path, cursor, cap, 0) != 0)
        return -1;
    if (written)
        *written = g_rpc->length;
    if (next_cursor)
        *next_cursor = (u32)g_rpc->value0;
    if (count)
        *count = (u32)g_rpc->value1;
    memcpy(out, g_data, g_rpc->length);
    return 0;
}

int fs_call_read(const char *path, u64 offset, void *dst, u32 length,
                 u32 *got)
{
    if (length > FS_DATA_SIZE)
        length = FS_DATA_SIZE;
    if (fs_call(FS_OP_READ, path, offset, length, 0) != 0)
        return -1;
    if (got)
        *got = g_rpc->length;
    memcpy(dst, g_data, g_rpc->length);
    return 0;
}

int fs_call_write(const char *path, u64 total, u64 offset,
                  const void *src, u32 length, int truncate_first)
{
    memcpy(g_data, src, length);
    return fs_call_values(FS_OP_WRITE, path, offset, length,
                          truncate_first ? FS_FLAG_TRUNCATE : 0, total, 0);
}

int fs_call_unlink(const char *path)
{
    return fs_call(FS_OP_UNLINK, path, 0, 0, 0);
}

int fs_call_sync(void)
{
    return fs_call(FS_OP_SYNC, 0, 0, 0, 0);
}

int fs_call_format(int disk, u64 base, u32 sectors, const char *label)
{
    usize n = 0;
    while (label[n] && n + 1 < sizeof(g_rpc->label)) {
        g_rpc->label[n] = label[n];
        n++;
    }
    g_rpc->label[n] = 0;
    u64 encoded = ((u64)(u32)disk << 48) | (base & 0xFFFFFFFFFFFFULL);
    return fs_call_values(FS_OP_FORMAT, 0, 0, 0, 0, encoded, sectors);
}

int fs_call_write_contig(const char *path, u64 total, u64 offset,
                         const void *src, u32 length, u32 *first,
                         u32 *blocks)
{
    memcpy(g_data, src, length);
    if (fs_call_values(FS_OP_WRITE_CONTIG, path, offset, length, 0, total,
                       0) != 0)
        return -1;
    if (offset + length >= total) {
        if (first)
            *first = (u32)g_rpc->value0;
        if (blocks)
            *blocks = (u32)g_rpc->value1;
    }
    return 0;
}

const char *fs_label(void)
{
    return g_rpc->label;
}
```

`fs_service_start`：

```c
int fs_service_start(void)
{
    u64 control = pmm_alloc_page();
    u64 data = pmm_alloc_page();
    if (!control || !data)
        return -1;
    g_rpc = (struct fs_rpc *)(uintptr_t)control;
    g_data = (u8 *)(uintptr_t)data;
    memset(g_rpc, 0, PAGE_SIZE);
    memset(g_data, 0, PAGE_SIZE);
    g_rpc->magic = FS_RPC_MAGIC;
    g_rpc->state = FS_STATE_IDLE;
    g_rpc->data_phys = data;

    static char argument[32];
    usize len = 0;
    argument[len++] = '0';
    argument[len++] = 'x';
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        argument[len++] = digits[(control >> shift) & 0xF];
    argument[len] = 0;
    char *argv[1] = { argument };
    if (!process_spawn("/bin/ext4fs", 1, argv))
        return -1;

    for (int waited = 0; waited < FS_WAIT_MS; waited++) {
        if (g_rpc->state == FS_STATE_READY) {
            g_ready = 1;
            return 0;
        }
        thread_sleep_ms(1);
    }
    return -1;
}
```

- [ ] **Step 3: 能力与恒等映射**

`process_internal.h` 的 `struct process` 增加 `int block_cap;`；
`spawn_image` 里：

```c
int service = name && (strcmp(name, "/bin/drivers") == 0 ||
                       strcmp(name, "/bin/ext4fs") == 0);
process->space = vmm_space_create(service);
process->block_cap = name && strcmp(name, "/bin/ext4fs") == 0;
```

`process.h` 增加：

```c
int process_block_capable(void);
```

`process.c`：

```c
int process_block_capable(void)
{
    struct process *process = process_current();
    return process && process->block_cap;
}
```

- [ ] **Step 4: 新增块调用系统调用**

`userlib.h` 追加：

```c
#define SYS_BLK_COUNT 38
#define SYS_BLK_INFO  39
#define SYS_BLK_READ  40
#define SYS_BLK_WRITE 41
#define SYS_BLK_FLUSH 42

static inline long sys_blk_count(void)
{
    return syscall3(SYS_BLK_COUNT, 0, 0, 0);
}

static inline long sys_blk_info(int disk, u64 *sectors, u64 *port)
{
    return syscall3(SYS_BLK_INFO, disk, (long)sectors, (long)port);
}

static inline long sys_blk_read(int disk, u64 lba, u32 count, void *buffer)
{
    return syscall4(SYS_BLK_READ, disk, (long)lba, count, (long)buffer);
}

static inline long sys_blk_write(int disk, u64 lba, u32 count,
                                 const void *buffer)
{
    return syscall4(SYS_BLK_WRITE, disk, (long)lba, count, (long)buffer);
}

static inline long sys_blk_flush(int disk)
{
    return syscall3(SYS_BLK_FLUSH, disk, 0, 0);
}
```

`syscall.c` 顶部也要同步这 5 个 `#define`（内核与用户各一份，必须一致）。`syscall.c` 实现：

```c
static long syscall_blk_read(struct process *process, u64 disk, u64 lba,
                             u64 count, u64 buffer)
{
    if (!process->block_cap || count == 0 || count > 8)
        return -1;
    u64 bytes = count * 512;
    if (user_range_ok(process, buffer, bytes) != 1)
        return -1;
    u8 *scratch = kmalloc(bytes);
    if (!scratch)
        return -1;
    long result = -1;
    if (dev_disk_read((int)disk, lba, (u32)count, scratch) == 0 &&
        process_user_write(process, buffer, scratch, bytes) == 0)
        result = (long)count;
    kfree(scratch);
    return result;
}

static long syscall_blk_write(struct process *process, u64 disk, u64 lba,
                              u64 count, u64 buffer)
{
    if (!process->block_cap || count == 0 || count > 8)
        return -1;
    u64 bytes = count * 512;
    if (user_range_ok(process, buffer, bytes) != 1)
        return -1;
    return dev_disk_write((int)disk, lba, (u32)count,
                          (const void *)(uintptr_t)buffer) == 0
               ? (long)count : -1;
}
```

（`blk_count/blk_info/blk_flush` 同样检查 `block_cap`，完整实现：）

```c
static long syscall_blk_count(struct process *process)
{
    return process->block_cap ? dev_disk_count() : -1;
}

static long syscall_blk_info(struct process *process, u64 disk,
                             u64 sectors_pointer, u64 port_pointer)
{
    if (!process->block_cap ||
        user_range_ok(process, sectors_pointer, sizeof(u64)) != 1 ||
        user_range_ok(process, port_pointer, sizeof(u64)) != 1)
        return -1;
    u64 sectors = dev_disk_sectors((int)disk);
    u64 port = (u64)(i64)dev_disk_port((int)disk);
    if (process_user_write(process, sectors_pointer, &sectors,
                           sizeof(u64)) != 0 ||
        process_user_write(process, port_pointer, &port, sizeof(u64)) != 0)
        return -1;
    return 0;
}

static long syscall_blk_flush(struct process *process, u64 disk)
{
    if (!process->block_cap)
        return -1;
    return dev_disk_flush((int)disk) == 0 ? 0 : -1;
}
```

`syscall_dispatch` 追加：

```c
case SYS_BLK_COUNT:
    result = syscall_blk_count(process);
    break;
case SYS_BLK_INFO:
    result = syscall_blk_info(process, frame->rdi, frame->rsi, frame->rdx);
    break;
case SYS_BLK_READ:
    result = syscall_blk_read(process, frame->rdi, frame->rsi, frame->rdx,
                              frame->r10);
    break;
case SYS_BLK_WRITE:
    result = syscall_blk_write(process, frame->rdi, frame->rsi, frame->rdx,
                               frame->r10);
    break;
case SYS_BLK_FLUSH:
    result = syscall_blk_flush(process, frame->rdi);
    break;
```

- [ ] **Step 5: 写 Ring3 服务骨架**

`src/apps/ext4fs/arena.c`：固定 2 MiB 静态 arena，首次适配 + 合并空闲块。
服务是长驻进程，`free` 必须真正回收，否则反复读取会把 2 MiB 耗尽：

```c
#include <types.h>

#define ARENA_BYTES (2u * 1024u * 1024u)

struct arena_block {
    u32 size;
    u32 free;
    struct arena_block *next;
};

static u8 g_arena[ARENA_BYTES] __attribute__((aligned(16)));
static struct arena_block *g_head;

static void arena_init(void)
{
    g_head = (struct arena_block *)g_arena;
    g_head->size = ARENA_BYTES - sizeof(struct arena_block);
    g_head->free = 1;
    g_head->next = 0;
}

void *arena_alloc(u32 bytes)
{
    if (!g_head)
        arena_init();
    bytes = (bytes + 15u) & ~15u;
    for (struct arena_block *block = g_head; block; block = block->next) {
        if (!block->free || block->size < bytes)
            continue;
        if (block->size >= bytes + sizeof(struct arena_block) + 16u) {
            struct arena_block *rest =
                (struct arena_block *)((u8 *)(block + 1) + bytes);
            rest->size = block->size - bytes - sizeof(struct arena_block);
            rest->free = 1;
            rest->next = block->next;
            block->next = rest;
            block->size = bytes;
        }
        block->free = 0;
        return block + 1;
    }
    return 0;
}

void arena_free(void *pointer)
{
    if (!pointer)
        return;
    struct arena_block *block = (struct arena_block *)pointer - 1;
    block->free = 1;
    for (struct arena_block *item = g_head; item; item = item->next) {
        if (item->free && item->next && item->next->free) {
            item->size += sizeof(struct arena_block) + item->next->size;
            item->next = item->next->next;
        }
    }
}
```

`blk.c`：`sys_blk_*` 包装 + `ext4_env` 的 read/write/flush/alloc/log/disk 回调。
read 实现：

```c
static int blk_env_read(int disk, u64 base, u64 offset, void *buffer,
                        u32 length)
{
    u64 lba = base + offset / 512;
    u32 sectors = (length + 511) / 512;
    return sys_blk_read(disk, lba, sectors, buffer) == (long)sectors
               ? 0 : -1;
}
```

链接 `kernel/lib/printf.c` 会引用 `console_write`/`console_write_n`
（`kprintf` 内部调用）。在 `blk.c` 里补上 Ring3 版本：

```c
void console_write(const char *text)
{
    sys_write(1, text, u_strlen(text));
}

void console_write_n(const char *text, usize length)
{
    sys_write(1, text, length);
}
```

`ext4_env` 的 `log` 回调用同一个 `console_write` 即可。

`main.c`：

```c
#include <fs_rpc.h>
#include <types.h>
#include <userlib.h>
#include "ext4.h"

void ext4fs_service(struct fs_rpc *rpc, u8 *data);

static u64 parse_hex(const char *text)
{
    u64 value = 0;
    if (!text)
        return 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    while (*text) {
        u64 digit;
        if (*text >= '0' && *text <= '9')
            digit = (u64)(*text - '0');
        else if (*text >= 'a' && *text <= 'f')
            digit = (u64)(*text - 'a' + 10);
        else if (*text >= 'A' && *text <= 'F')
            digit = (u64)(*text - 'A' + 10);
        else
            break;
        value = (value << 4) | digit;
        text++;
    }
    return value;
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);
    if (argc < 1)
        return 1;
    struct fs_rpc *rpc = (struct fs_rpc *)(u64)parse_hex(argv[0]);
    if (rpc->magic != FS_RPC_MAGIC)
        return 2;
    u8 *data = (u8 *)(u64)rpc->data_phys;
    rpc->state = FS_STATE_READY;
    ext4fs_service(rpc, data);
    return 0;
}
```

`service.c` 的循环与分发：

```c
#define FS_WRITE_MAX (8u * 1024u * 1024u)

static u8 *g_write_buf;
static struct ext4_state g_saved;
static int g_install_active;

struct list_item { u8 name_len; u8 is_dir; u16 pad; u64 size; char name[]; };
struct list_ctx {
    u8 *data; u32 cap; u32 used; u32 index; u32 skip; u32 next; u32 count;
};

static void list_cb(const char *name, u64 size, int is_dir, void *context)
{
    struct list_ctx *ctx = context;
    u32 index = ctx->index++;
    if (index < ctx->skip)
        return;
    u32 name_len = (u32)u_strlen(name);
    u32 need = (16u + name_len + 1u + 15u) & ~15u;
    if (ctx->used + need > ctx->cap)
        return;
    struct list_item *item = (struct list_item *)(ctx->data + ctx->used);
    item->name_len = (u8)name_len;
    item->is_dir = (u8)(is_dir != 0);
    item->pad = 0;
    item->size = size;
    memcpy(item->name, name, name_len + 1);
    ctx->used += need;
    ctx->count++;
    ctx->next = index + 1;
}

static int handle(struct fs_rpc *rpc, u8 *data)
{
    switch (rpc->op) {
    case FS_OP_MOUNT: {
        if (ext4_mount() != 0)
            return -1;
        const char *label = ext4_label();
        usize n = 0;
        while (label && label[n] && n + 1 < sizeof(rpc->label)) {
            rpc->label[n] = label[n];
            n++;
        }
        rpc->label[n] = 0;
        return 0;
    }
    case FS_OP_STAT: {
        int is_dir = 0;
        u64 size = 0;
        if (ext4_stat(rpc->path, &is_dir, &size) != 0)
            return -1;
        rpc->value0 = is_dir ? 1 : 0;
        rpc->value1 = size;
        return 0;
    }
    case FS_OP_LIST: {
        struct list_ctx ctx = { data, FS_DATA_SIZE, 0, 0,
                                (u32)rpc->offset, 0, 0 };
        if (ext4_list(rpc->path, list_cb, &ctx) != 0)
            return -1;
        rpc->length = ctx.used;
        rpc->value0 = ctx.next;
        rpc->value1 = ctx.count;
        return 0;
    }
    case FS_OP_READ: {
        u64 got = 0;
        if (!rpc->length)
            return 0;
        if (ext4_read_at(rpc->path, rpc->offset, data, rpc->length,
                         &got) != 0)
            return -1;
        rpc->length = (u32)got;
        return 0;
    }
    case FS_OP_WRITE: {
        u64 end = rpc->offset + rpc->length;
        if (end > FS_WRITE_MAX || rpc->length > FS_DATA_SIZE)
            return -1;
        memcpy(g_write_buf + rpc->offset, data, rpc->length);
        if (end < rpc->value0)
            return 0;
        return ext4_write_file(rpc->path, g_write_buf, rpc->value0);
    }
    case FS_OP_WRITE_CONTIG: {
        u64 end = rpc->offset + rpc->length;
        if (end > FS_WRITE_MAX || rpc->length > FS_DATA_SIZE)
            return -1;
        memcpy(g_write_buf + rpc->offset, data, rpc->length);
        if (end < rpc->value0)
            return 0;
        u32 first = 0;
        u32 blocks = 0;
        if (ext4_write_file_contig(rpc->path, g_write_buf, rpc->value0,
                                   &first, &blocks) != 0)
            return -1;
        rpc->value0 = first;
        rpc->value1 = blocks;
        return 0;
    }
    case FS_OP_UNLINK:
        return ext4_unlink(rpc->path);
    case FS_OP_SYNC:
        if (ext4_sync() != 0)
            return -1;
        if (g_install_active) {
            ext4_state_restore(g_saved);
            g_install_active = 0;
        }
        return 0;
    case FS_OP_FORMAT: {
        u64 encoded = rpc->value0;
        int disk = (int)(encoded >> 48);
        u64 base = encoded & 0xFFFFFFFFFFFFULL;
        g_saved = ext4_state_save();
        g_install_active = 1;
        if (ext4_format_volume(disk, base, (u32)rpc->value1,
                               rpc->label) != 0 ||
            ext4_mount_at(disk, base) != 0) {
            ext4_state_restore(g_saved);
            g_install_active = 0;
            return -1;
        }
        return 0;
    }
    default:
        return -1;
    }
}
```

主循环：

```c
void ext4fs_service(struct fs_rpc *rpc, u8 *data)
{
    long id = sys_shm_create(FS_WRITE_MAX);
    if (id <= 0)
        return;
    g_write_buf = (u8 *)sys_shm_map(id);
    if (!g_write_buf)
        return;
    for (;;) {
        if (rpc->state == FS_STATE_REQUEST) {
            rpc->status = handle(rpc, data);
            __sync_synchronize();
            rpc->state = FS_STATE_DONE;
        } else {
            sys_sleep(1);
        }
    }
}
```

写入缓冲放在一个 8 MiB 的共享内存对象里（`SHM_MAX_PAGES` = 2048 页），
避免占用用户程序 4 MiB 的静态映射配额。

WRITE 采用“内核按顺序分块发送，服务在最后一包整文件落盘”的方式；
`FS_WRITE_MAX` 8 MiB 覆盖 `KERNEL.ELF`（约 300 KiB）与 `WQY.TTC`
（约 5 MiB）。普通 `/mnt` 写入同样走这条路径。

- [ ] **Step 6: 构建服务**

Makefile 增加：

```make
EXT4FS_SRCS := apps/ext4fs/main.c apps/ext4fs/service.c \
               apps/ext4fs/blk.c apps/ext4fs/arena.c \
               fs/ext4/ext4_env.c fs/ext4/ext4_mount.c \
               fs/ext4/ext4_extent.c fs/ext4/ext4_dir.c \
               fs/ext4/ext4_read.c fs/ext4/ext4_alloc.c \
               fs/ext4/ext4_write.c fs/ext4/ext4_format.c \
               user/userlib.c kernel/lib/string.c kernel/lib/printf.c
EXT4FS_HDRS := $(wildcard fs/ext4/*.h) kernel/include/fs_rpc.h

$(BUILD)/ext4fs.uelf: $(EXT4FS_SRCS) $(EXT4FS_HDRS) user/user.ld
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -Ifs/ext4 -Iuser -Ikernel/include \
	  $(USER_LDFLAGS) -o $@ $(EXT4FS_SRCS)
```

并把 `out/ext4fs.zxe`、`$(RAMFS)/bin/ext4fs`、`RAMFS_FILES` 补上（照抄 `csh.zxe` 的三段规则）。

- [ ] **Step 7: 启动服务**

`main.c` 在 `dev_start()` 之后：

```c
kprintf("[fs  ] starting Ring3 ext4 service\n");
if (fs_service_start() == 0)
    kprintf("[fs  ] ext4 service ready\n");
else
    kprintf("[fs  ] ext4 service unavailable\n");
```

- [ ] **Step 8: 冒烟**

```bash
cd src && make -j4
make run-iso   # 或 timeout 45 ...
```
Expected: 串口出现 `[proc] pid 2: /bin/ext4fs`、`[fs  ] ext4 service ready`；无盘时 MOUNT 返回 -1，不影响启动。

- [ ] **Step 9: 检查点**

Run: `git diff --stat`
Expected: 协议、服务、syscall、进程能力改动齐全；等待授权后提交。

---

### Task 4: 安装器改用 ext4fs

**Files:**
- Modify: `src/kernel/fs/install.c`
- Modify: `src/kernel/proc/syscall.c`（install 错误路径已存在）
- Modify: `src/apps/csh/install.c`（提示文字）

**Interfaces:**
- Consumes: Task 3 的 `fs_call_format/fs_call_write/fs_call_write_contig/fs_call_sync`。
- Produces: 安装后 ext4 卷含 `KERNEL.ELF`、`KERNEL.BIN`、`WQY.TTC`。

- [ ] **Step 1: 替换 ext4 调用**

在 `install_run` 中删除 `build_kernel_elf` 之外对 `ext4_make_root` 的调用，改为：

```c
if (fs_call_format(target_disk, root_lba, root_sectors, "ZHAOXUROOT") != 0) {
    set_error("ext4 service could not format the root volume");
    kfree(loader);
    return -1;
}
for (u64 offset = 0; offset < kernel_bytes;) {
    u32 chunk = (u32)(kernel_bytes - offset);
    if (chunk > FS_DATA_SIZE)
        chunk = FS_DATA_SIZE;
    if (fs_call_write_contig("/KERNEL.BIN", kernel_bytes, offset,
                             kernel + offset, chunk, &bin_block,
                             &bin_blocks) != 0) {
        set_error("ext4 service could not write KERNEL.BIN");
        kfree(loader);
        return -1;
    }
    offset += chunk;
}
for (u64 offset = 0; offset < kernel_file_size;) {
    u32 chunk = (u32)(kernel_file_size - offset);
    if (chunk > FS_DATA_SIZE)
        chunk = FS_DATA_SIZE;
    if (fs_call_write("/KERNEL.ELF", kernel_file_size, offset,
                      kernel_file + offset, chunk, offset == 0) != 0) {
        set_error("ext4 service could not write KERNEL.ELF");
        kfree(loader);
        return -1;
    }
    offset += chunk;
}
for (u64 offset = 0; font && offset < font_size;) {
    u32 chunk = (u32)(font_size - offset);
    if (chunk > FS_DATA_SIZE)
        chunk = FS_DATA_SIZE;
    if (fs_call_write("/WQY.TTC", font_size, offset, font + offset, chunk,
                      offset == 0) != 0) {
        set_error("ext4 service could not write WQY.TTC");
        kfree(loader);
        return -1;
    }
    offset += chunk;
}
if (fs_call_sync() != 0) {
    set_error("ext4 service could not sync the root volume");
    kfree(loader);
    return -1;
}
```

`bin_lba = root_lba + (u64)bin_block * 8u;` 保持不变（4 KiB 块）。删除
`#include <ext4.h>` 与 `ext4_make_root`。

把原来紧跟在 `ext4_make_root` 之后的 `kfree(font_copy)`、
`kfree(kernel_file)` 移到上面所有 `fs_call_*` 与 `fs_call_sync` 之后，
避免使用已释放的内存。

- [ ] **Step 2: 保持分区/ESP/stage2 逻辑**

`write_mbr`、`write_gpt`、`zero_gap`、`fat32_make_esp`、
`read_stage1_verify` 全部保留；只改 ext4 部分。

`prepare_stage2` 本轮仍只写现有的 24 字节参数块（kernel_lba/bytes/
sectors）；48 字节扩展（cd/font）在 Task 6 修改 stage2 时一起做，
避免向尚未扩展的旧参数块后面多写 24 字节。

- [ ] **Step 3: 更新提示**

`src/apps/csh/install.c` 的两条提示改为
`Installing MBR + ext4 root; this may take a while...` /
`Installing GPT + ext4 root; this may take a while...`。

- [ ] **Step 4: 头less 安装冒烟（BIOS）**

```bash
cd src
make HEADLESS=1 iso -j4
rm -f out/target.img
(sleep 12; echo 'install mbr'; sleep 45) | timeout 80 qemu-system-x86_64 \
  -M q35 -m 512M -smp 2 \
  -drive if=none,id=zcd,format=raw,media=cdrom,readonly=on,file=out/zhaoxu_0.5.0_installer.iso \
  -device ide-cd,drive=zcd,bus=ide.0 \
  -drive if=none,id=zdst,format=raw,file=out/target.img \
  -device ide-hd,drive=zdst,bus=ide.1 \
  -boot order=d -net none -serial stdio -monitor none -display none -no-reboot
```
Expected: `[install] esp ... ext4 root ...`、`Install complete`。

（此任务依赖 Task 7 的 BIOS ISO 修复；如果先做本任务，用 UEFI + 已存在的 WIP 路径验证，最后再跑 BIOS。）

- [ ] **Step 5: 检查点**

Run: `git diff src/kernel/fs/install.c`
Expected: 不再包含 ext4 头与 `ext4_make_root`。

---

### Task 5: VFS 转发、流式读取与内核去 ext4 化

**Files:**
- Modify: `src/kernel/fs/vfs.c`, `src/kernel/include/vfs.h`
- Modify: `src/boot/efi_main.c`（UEFI 从 ext4 读 `/KERNEL.ELF` 与 `/WQY.TTC`；Task 4 的 UEFI 安装启动依赖它）
- Modify: `src/Makefile`（从 `C_SRCS` 删除 `kernel/fs/ext4.c`、`kernel/fs/ext4_write.c`）
- Modify: `src/kernel/proc/syscall.c`（`syscall_install` 错误输出保持）
- Modify: `src/kernel/main.c`（日志文字）

**Interfaces:**
- Consumes: Task 3 的 `fs_call_*`。
- Produces: `/mnt` 由 ext4fs 提供；FAT32 回退保留。

- [ ] **Step 1: 重写 `vfs_file`**

```c
struct vfs_file {
    int used;
    int kind;          /* 0 = buffer, 1 = ext4 stream */
    u8 *data;
    u64 size;
    u64 offset;
    char path[256];
};
```

`vfs_open`：

```c
if (g_data_fs == 2) {
    int is_dir = 0;
    u64 size = 0;
    if (!path_is_mnt(path) || !path[4])
        return -1;
    if (fs_call_stat(path + 4, &is_dir, &size) != 0 || is_dir)
        return -1;
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (g_files[i].used)
            continue;
        g_files[i].used = 1;
        g_files[i].kind = 1;
        g_files[i].data = 0;
        g_files[i].size = size;
        g_files[i].offset = 0;
        usize n;
        for (n = 0; path[n] && n + 1 < sizeof(g_files[i].path); n++)
            g_files[i].path[n] = path[n];
        g_files[i].path[n] = 0;
        return i + VFS_FD_BASE;
    }
    return -1;
}
```

`vfs_read`：`kind == 1` 时按 `FS_DATA_SIZE` 分块：

```c
long total = 0;
while (count) {
    u32 chunk = count > FS_DATA_SIZE ? FS_DATA_SIZE : (u32)count;
    u32 got = 0;
    if (fs_call_read(file->path, file->offset, fs_data(), chunk, &got) != 0)
        return -1;
    if (!got)
        break;
    memcpy(buffer, fs_data(), got);
    file->offset += got;
    buffer += got;
    total += got;
    count -= got;
    if (got < chunk)
        break;
}
return total;
```

- [ ] **Step 2: list/stat/read_all/write/unlink**

`vfs_mount_data`：

```c
int vfs_mount_data(void)
{
    if (fs_service_ready() && fs_call_mount() == 0) {
        g_data_fs = 2;
        return 0;
    }
    if (fat32_mount() == 0) {
        g_data_fs = 1;
        return 0;
    }
    return -1;
}
```

- `vfs_list`：`FS_OP_LIST`，`rpc->offset` 作为条目游标；内核解析：

```c
struct list_item { u8 name_len; u8 is_dir; u16 pad; u64 size; char name[]; };

u32 cursor = 0;
for (;;) {
    u32 written = 0, next = 0, count = 0;
    if (fs_call_list(path + 4, cursor, fs_data(), FS_DATA_SIZE,
                     &written, &next, &count) != 0)
        return -1;
    u8 *p = fs_data();
    for (u32 i = 0; i < count; i++) {
        struct list_item *item = (struct list_item *)p;
        callback(item->name, item->size, item->is_dir != 0, context);
        p += (16u + item->name_len + 1u + 15u) & ~15u;
    }
    if (!count || next <= cursor)
        break;
    cursor = next;
}
return 0;
```

结构体与 `<fs_rpc.h>` 一起放在 `vfs.c` 顶部（或新建
`kernel/fs/fs_list.h`）。
- `vfs_stat`：`fs_call_stat`。
- `vfs_read_all`：先 stat，再循环 `fs_call_read` 拼装（上限
  `#define VFS_READ_ALL_MAX (16u * 1024u * 1024u)`，超限返回 -1；
  该宏放在 `vfs.c` 顶部，不再依赖 `ext4.h`）。
- `vfs_write_file`/`vfs_unlink`：`fs_call_write`（分 4 KiB 块）/
  `fs_call_unlink`。
- `vfs_data_fs()` 返回 `"ext4"`；`vfs_data_label()` 返回服务回填的
  `rpc->label`。

- [ ] **Step 3: 移除内核 ext4 与 ESP 内核拷贝**

Makefile `C_SRCS` 删除两行；删除 `src/kernel/fs/ext4.c`、
`ext4_write.c`、`ext4_internal.h`、`src/kernel/include/ext4.h`（任务 1 保留的过渡副本）。
`vfs.c` 不再 include `<ext4.h>`。

同时修改 Makefile：

```diff
-all: $(OUT)/BOOTX64.EFI $(ESP)/BOOTX64.EFI $(ESP)/KERNEL.ELF
+all: $(OUT)/BOOTX64.EFI $(ESP)/BOOTX64.EFI
@@
-$(ESP)/KERNEL.ELF: $(OUT)/kernel.elf
-	@mkdir -p $(ESP)
-	cp $< $@
-
@@
-	       $(OUT)/target.img $(ESP)/BOOTX64.EFI $(ESP)/KERNEL.ELF
+	       $(OUT)/target.img $(ESP)/BOOTX64.EFI
```

`esp/EFI/BOOT/KERNEL.ELF` 若已存在，由 `make clean` 删除；不要手工保留。

- [ ] **Step 4: 编译与符号检查**

```bash
cd src && make clean && make -j4
nm out/kernel.elf | grep -E ' ext4_' ; echo "exit=$?"
nm -u out/kernel.elf
```
Expected: `grep` 无输出（exit=1）；`nm -u` 无输出。

- [ ] **Step 5: 安装后读写冒烟**

用 Task 4 生成的目标盘启动：

```bash
timeout 60 qemu-system-x86_64 -M q35 -m 512M -smp 2 \
  -drive if=none,id=zdst,format=raw,file=out/target.img \
  -device ide-hd,drive=zdst,bus=ide.0 \
  -boot order=c -net none -serial stdio -monitor none -display none \
  -no-reboot
```
Expected: `[ext4] mounted "ZHAOXUROOT"`（服务日志）、`[vfs ] /mnt mounted (ext4 ...)`。

- [ ] **Step 6: 检查点**

Run: `git diff --stat`
Expected: VFS 改动与 Makefile 删除项齐全。

---

### Task 6: BIOS ISO 改为 no-emulation，内核不进引导镜像

**Files:**
- Modify: `src/boot/bios_stage1.asm`, `src/boot/bios_stage2.asm`
- Modify: `src/boot/bios_stage2.asm`（EDD/CHS 自适应与 sector size）
- Modify: `src/tools/bios-img.rb`
- Create: `src/tools/iso-bios.rb`
- Modify: `src/Makefile`

**Interfaces:**
- Produces: `bios.img` = stage1 + 512 字节填充 + stage2（无内核）；
  ISO 内 `install/KERNEL.BIN` 为普通文件；构建后回填 stage2 参数块。

- [ ] **Step 1: stage1 支持“已驻留”**

**在 `INT 13h AH=08h` 探测几何之前**判断驻留标志（CD 没有可用的
CHS 几何，先探测会直接 fail）：

```asm
        mov     [boot_drive], dl
        mov     ax, [stage2_lba]
        cmp     ax, 0xFFFF              ; no-emulation: stage2 已由固件装入
        je      .enter

        mov     dl, [boot_drive]
        mov     ah, 0x08
        int     0x13
        jc      fail
        and     cl, 0x3F
        jz      fail
        movzx   ax, cl
        mov     [spt], ax
        inc     dh
        movzx   ax, dh
        mov     [heads], ax

        mov     ax, 0x0800
        mov     es, ax
        xor     bx, bx
        mov     ax, [stage2_lba]
        mov     cx, STAGE2_SECTORS
        call    read_chs
        jc      fail
.enter:
        mov     dl, [boot_drive]
        db      0EAh
        dw      STAGE2_LOAD
        dw      0
```

`stage2_lba` 在安装盘上仍是 1/34；ISO 镜像构建时写 0xFFFF。

- [ ] **Step 2: `bios-img.rb` 改为纯引导镜像**

输出布局：`[0,512) = stage1`，`[512,1024) = 0`，`[1024,1024+stage2) = stage2`；
把 `stage2_lba` 字段补成 `0xFFFF`。删除内核参数与 1.44 MB 填充；
文件大小按 512 对齐。

- [ ] **Step 3: stage2 用设备扇区大小读**

新增 `disk_shift`（9 或 11）。启动时：

```asm
        mov     word [dap_geom], 0x1E       ; 返回长度 30
        mov     si, dap_geom
        mov     ah, 0x48
        mov     dl, [boot_drive]
        int     0x13
        jc      .default_sector
        mov     ax, [dap_geom + 0x18]       ; bytes per sector
        cmp     ax, 2048
        jne     .default_sector
        mov     byte [disk_shift], 11
        jmp     .sector_done
.default_sector:
        mov     byte [disk_shift], 9
.sector_done:
```

`read_lba` 的每次块数上限改为“64 KiB 边界”：

```asm
        ; ax = 剩余扇区数
        mov     cl, byte [disk_shift]
        sub     cl, 2                       ; 0x10000 >> shift，单位扇区
        mov     dx, 1
        shl     dx, cl
        dec     dx                          ; max = (0x10000 >> shift) - 1
        cmp     ax, dx
        jbe     .size_ok
        mov     ax, dx
.size_ok:
```

目标指针推进按字节换算成段：

```asm
        mov     ax, [io_chunk]
        mov     cl, byte [disk_shift]
        sub     cl, 4                       ; sectors * (1<<shift) / 16
        shl     ax, cl
        add     word [io_seg], ax
```

`load_kernel` 不再依赖 `param_kernel_sectors`，改为由字节数算：

```asm
        cmp     dword [param_kernel_bytes + 4], 0
        jne     .error
        mov     eax, dword [param_kernel_bytes]
        cmp     eax, MAX_KERNEL_BYTES           ; 448 KiB
        ja      .error
        mov     cl, byte [disk_shift]
        mov     edx, 1
        shl     edx, cl                         ; 1 << shift
        dec     edx
        add     eax, edx
        shr     eax, cl                         ; 设备扇区数
        mov     cx, ax
```

（`MAX_KERNEL_SECTORS` 改为 `MAX_KERNEL_BYTES = 0x70000`；
`read_lba` 内部按设备扇区推进，调用方只需要给出设备扇区数。）

- [ ] **Step 4: 扩展参数块**

`bios_stage2.asm` 的 `ZBOOTPRM` 块固定为 56 字节（8 + 48）：

```asm
param_magic           dq 0x4D5250544F4F425A   ; 'ZBOOTPRM'
param_kernel_lba      dq 0
param_kernel_bytes    dq 0
param_kernel_sectors  dq 0
param_cd              dq 0   ; 1 = LBA 为 ISO 2 KiB 单位
param_font_lba        dq 0
param_font_bytes      dq 0
```

`read_lba` 入口若 `[param_cd]` 非零，把 LBA 乘以
`2048 >> disk_shift`。安装器写 `param_cd = 0`，ISO 工具写 1。
`tools/bios-img.rb` 不再写参数块，只把 `stage2_lba` 补成 0xFFFF。

- [ ] **Step 5: 写 `iso-bios.rb`**

```ruby
#!/usr/bin/env ruby
# iso-bios.rb -- 在 ISO 内回填 BIOS stage2 的内核/字体参数（规则 6）。
iso = ARGV[0] or abort "usage: iso-bios.rb <iso>"
data = File.binread(iso)
SECTOR = 2048

def le32(s, o) = s[o, 4].unpack1("V")
def le16(s, o) = s[o, 2].unpack1("v")

def entries(data, lba, size)
  raw = data[lba * SECTOR, size]
  out = []
  pos = 0
  while pos < raw.bytesize
    len = raw.getbyte(pos)
    break if len.nil? || len.zero?
    rec = raw[pos, len]
    name_len = rec.getbyte(32)
    name = rec[33, name_len].to_s
    name = "." if name_len == 1 && rec.getbyte(33) == 0
    name = ".." if name_len == 1 && rec.getbyte(33) == 1
    out << { name: name, lba: le32(rec, 2), size: le32(rec, 10),
             final: (rec.getbyte(25) & 0x80).zero? }
    pos += len
  end
  out
end

def find(list, name)
  item = list.find { |e| e[:name].casecmp?(name) }
  abort "iso-bios: #{name} missing" unless item
  abort "iso-bios: #{name} is multi-extent" unless item[:final]
  item
end

pvd = data[16 * SECTOR, SECTOR]
abort "iso-bios: no ISO9660 PVD" unless pvd[1, 5] == "CD001"
root_lba = le32(pvd, 158)
root_size = le32(pvd, 166)
root = entries(data, root_lba, root_size)
install = find(root, "INSTALL")
install_entries = entries(data, install[:lba], install[:size])
kernel = find(install_entries, "KERNEL.BIN")
font = find(install_entries, "WQY.TTC")
boot = find(root, "BIOS.IMG")

stage2_offset = boot[:lba] * SECTOR + 1024
magic = "ZBOOTPRM"
index = data.index(magic, stage2_offset)
abort "iso-bios: ZBOOTPRM missing" unless index && index < stage2_offset + 0x8000
patch = [kernel[:lba], kernel[:size], 0, 1, font[:lba], font[:size]]
               .pack("Q<Q<Q<Q<Q<Q<")
data[index + 8, patch.bytesize] = patch
File.binwrite(iso, data)
puts "iso-bios: kernel LBA #{kernel[:lba]}, font LBA #{font[:lba]}"
```

（`entries` 需要读目录项长度字段 32 位名长；多 extent 用 flags bit 7
拒绝。`root` 的 PVD 偏移 156/166 是根目录记录；上面用 158/166 取
LBA/长度，二者相差 2 字节，实际实现按 ISO9660 结构校验一遍再运行。）

- [ ] **Step 6: Makefile ISO 规则**

```make
$(ISO): $(OUT)/BOOTX64.EFI $(OUT)/kernel.elf $(EFI_IMG) $(BIOS_IMG) \
        initramfs/data/wqy.ttc tools/iso-bios.rb
	@rm -rf $(ISO_ROOT); mkdir -p $(ISO_ROOT)/install
	cp $(EFI_IMG) $(ISO_ROOT)/efi.img
	cp $(BIOS_IMG) $(ISO_ROOT)/bios.img
	$(RUBY) tools/kernel-flat.rb $(OUT)/kernel.elf $(ISO_ROOT)/install/KERNEL.BIN
	cp $(OUT)/kernel.elf $(ISO_ROOT)/install/KERNEL.ELF
	cp initramfs/data/wqy.ttc $(ISO_ROOT)/install/WQY.TTC
	xorriso -as mkisofs -o $@ -V ZHAOXU_OS -J -R \
	  -b bios.img -no-emul-boot -eltorito-alt-boot \
	  -e efi.img -no-emul-boot -isohybrid-gpt-basdat $(ISO_ROOT)
	$(RUBY) tools/iso-bios.rb $@
```

`kernel-flat.rb` 复用 `bios-img.rb` 里的 `flatten_kernel`，输出 PT_LOAD
展开后的原始内核。

- [ ] **Step 7: 启动验证**

```bash
cd src && make HEADLESS=0 iso -j4
timeout 45 make run-bios
```
Expected: `ZhaoxuBootloader stage1`、`stage2`、`Reading kernel image.`、
内核启动日志；不再出现 `stage2: disk read failed`。

- [ ] **Step 8: 检查点**

Run: `git diff --stat src/boot src/tools src/Makefile`
Expected: 引导镜像不含内核；ISO 构建成功。

---

### Task 7: BIOS 安装时把字体带进 ext4

**Files:**
- Modify: `src/boot/bios_stage2.asm`
- Modify: `src/tools/iso-bios.rb`
- Modify: `src/kernel/fs/install.c`（字体来源）

**Interfaces:**
- Consumes: Task 6 的参数块与 CD/HDD 读取。
- Produces: `boot_info.font_phys/font_size` 在 BIOS ISO 启动时有效。

- [ ] **Step 1: 参数块字段**

字体字段已在 Task 6 Step 4 的 `ZBOOTPRM` 块里定义：
`param_font_lba`、`param_font_bytes`。`iso-bios.rb` 已回填。
安装盘（HDD）由安装器把两个字段清零。

- [ ] **Step 2: 分段加载字体到高内存**

不依赖 unreal mode；复用“实模式读低缓冲区 → 保护模式拷高内存”的
现有套路，分批执行（字体 ≤ 8 MiB，每批 ≤ 448 KiB）：

```asm
load_font:
        cmp     qword [param_font_bytes], 0
        je      .done
        cmp     qword [param_font_bytes], FONT_MAX
        ja      .done
        mov     dword [font_done], 0
.loop:
        ; 剩余字节 = param_font_bytes - font_done
        mov     eax, dword [param_font_bytes]
        sub     eax, [font_done]
        jz      .done
        cmp     eax, FONT_CHUNK          ; 448 KiB
        jbe     .size_ok
        mov     eax, FONT_CHUNK
.size_ok:
        ; 计算本次扇区数（按设备扇区大小）
        ; 读 LBA = param_font_lba + font_done / 2048（CD 用 2 KiB 单位）
        call    read_font_chunk          ; 读入 0x10000
        ; 进入保护模式，把 0x10000 的 chunk 拷到 0x04000000+font_done
        call    pm_copy_font
        add     [font_done], eax
        jmp     .loop
.done:
        mov     rax, 0x04000000
        mov     [boot_info + FONT_PHYS], rax
        mov     rax, [param_font_bytes]
        mov     [boot_info + FONT_SIZE], rax
```

`pm_copy_font`/`read_font_chunk` 与现有 `load_kernel` 的模式切换代码
共用 GDT 与页表，必须保存/恢复 `esp` 与段寄存器；每次回到实模式后
重新 `xor ax,ax; mov ds/es/ss,ax`。

- [ ] **Step 3: 内核保留字体页**

`pmm_init` 在建立空闲页表后，把 `boot_info.font_phys` 起
`font_size` 字节标记为已用（按页向上取整），避免分配器覆盖字体。
UEFI 路径同样受益（原先依赖固件内存类型）。

- [ ] **Step 4: 安装器字体优先级**

`install.c` 保持：

```c
const u8 *font = (const u8 *)(uintptr_t)info->font_phys;
if (!font || font_size < 13)
    font_size = 0;
```
（BIOS 现在也会填 `font_phys`；无字体时 ext4 不写 `WQY.TTC`。）

- [ ] **Step 5: 验证**

BIOS 从 ISO 启动，串口应打印字体大小；执行 `install mbr` 后，
目标盘 ext4 根应存在 `/WQY.TTC`（可用主机 `debugfs -R "ls /"` 检查）。

- [ ] **Step 6: 检查点**

Run: `git diff --stat src/boot src/tools src/kernel/fs/install.c src/kernel/mem/pmm.c`

---

### Task 8: 全量回归与文档

**Files:**
- Create: `src/tools/ext4-regress.rb`
- Modify: `AGENTS.md`, `README.md`, `CHANGELOG.md`
- Modify: `src/tools/smoke.rb`（如需新 boot 模式）

**Interfaces:**
- Produces: 一条 Ruby 命令跑完 M0 回归。

- [ ] **Step 1: 回归脚本**

`ext4-regress.rb` 顺序执行并在失败处退出：
1. `make -j4`；`nm out/kernel.elf | grep ext4_` 必须为空。
2. `make iso`；`timeout 45 make run-iso` 匹配 `ext4 service ready`。
3. `timeout 45 make run-bios` 匹配 `Reading kernel image` 且无
   `disk read failed`。
4. 删除 `out/target.img`，BIOS headless 执行 `install mbr`，匹配
   `Install complete`。
5. 挂目标盘启动（BIOS 与 UEFI 各一次），匹配 `mounted "ZHAOXUROOT"`。
6. 用 `dd` 截出 ESP 到 `/tmp/esp.img`，`mdir -i /tmp/esp.img ::/EFI/BOOT`
   断言只有 `BOOTX64.EFI`。
7. `dd if=out/target.img bs=512 count=64 | grep -a 'ELF'` 必须无输出。

- [ ] **Step 2: 文档**

- `AGENTS.md`：更新“架构与不变量”“验证”中关于 ext4/ESP/内核位置的
  段落；写明 `/bin/ext4fs`、`fs_rpc.h`、`blk_*` 系统调用与 BIOS
  no-emulation。
- `README.md`：只写本次版本更新（规则 13），突出 ext4 主文件系统。
- `CHANGELOG.md`：新增条目。

- [ ] **Step 3: 跑回归**

Run: `ruby src/tools/ext4-regress.rb`
Expected: 全绿，退出码 0。

- [ ] **Step 4: 检查点**

Run: `git status --porcelain`；等待用户授权后提交。

---

## 后续计划（不在本计划内）

- **M1 只读互操作**：JBD2 回放、64bit、flex_bg/meta_bg、间接块、
  extent 树、csum 校验、htree、inline_data、稀疏文件。
- **M2 写互操作**：分配器、extent 树增长、目录分裂/htree 插入、
  csum 全链路、JBD2 提交、orphan。
- **M3 加固**：断电注入、大文件大目录、ENOSPC、只读降级。
- **M4 发布**：文档与全量回归。

每个里程碑单独写计划；本计划只交付 M0（服务骨架 + 去内核化 + BIOS ISO）。
