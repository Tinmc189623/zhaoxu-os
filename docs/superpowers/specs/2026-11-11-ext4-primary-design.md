# ext4 作为主文件系统 —— 设计

日期：2026-11-11
状态：已批准（用户确认方向）

## 1. 背景与目标

安装后的 Zhaoxu OS 需要把 ext4 作为主文件系统，FAT32 只保留兼容用途，同时
内核不得出现在引导扇区或 ESP（引导盘）里。现有工作区已有一版未提交的实现，
但存在两个硬伤：

- legacy BIOS 从 ISO 启动时用 EDD 读软盘仿真镜像，SeaBIOS 不支持，启动失败。
- `make all` 仍把 `KERNEL.ELF` 复制进 `esp/EFI/BOOT/`。

用户选择的目标：

1. **ext4 完整 Linux 互操作**：能挂载 `mkfs.ext4` 默认参数生成的卷
   （JBD2 日志、metadata_csum、64bit、flex_bg、htree、extents / 间接块），
   读写之后 `e2fsck -fn` 干净。
2. **完整 ext4 驱动放在 Ring3**：新增 `/bin/ext4fs` 服务；内核只保留 VFS
   转发，不在内核里保留 ext4 实现（不采用双实现）。
3. FAT32 仅用于 ESP 与老盘兼容。

非目标：加密、casefold、verity、bigalloc、DAX；配额只做“不破坏”处理，
不实现配额管理。

## 2. 磁盘布局（安装目标盘）

MBR：

```
LBA 0        stage1（512 字节，含分区表）
LBA 1        stage2（MBR；≤32 KiB）
LBA 1+S..2047 空隙（清零）
LBA 2048     ESP（FAT32，64 MiB，卷标 ZHAOXUEFI）
             仅含 EFI/BOOT/BOOTX64.EFI
其后          ext4 根分区（卷标 ZHAOXUROOT）
             含 KERNEL.ELF、KERNEL.BIN、WQY.TTC 与用户数据
```

GPT 布局中 stage2 改放 LBA 34（在 LBA1/2 的头与项之后），其余相同；
末尾另有备份 GPT。

不变量：

- 内核字节只出现在 ext4 分区内。stage2 参数块里只有 `KERNEL.BIN` 的
  LBA/字节数/扇区数，512 字节的引导扇区与 stage2 都不含内核。
- ESP 只有 UEFI 加载器，不再写 `KERNEL.ELF`。
- `KERNEL.BIN` 必须是单段连续 extent（BIOS stage2 只支持一个块表项）。
  `ext4fs` 的 `WRITE_CONTIG` 负责满足，安装器校验后回填 stage2。

## 3. 启动流程

### 3.1 UEFI

- ISO：`BOOTX64.EFI` 用极简 ISO9660 读 `/install/KERNEL.ELF` 与
  `/install/WQY.TTC`（现有逻辑保留）。
- 安装盘：`BOOTX64.EFI` 用固件内的最小只读 ext4 读 `/KERNEL.ELF`
  与 `/WQY.TTC`（现有逻辑保留，补 64bit 组描述符位、忽略 metadata_csum）。
- 只读实现不校验 csum、不回放日志；安装器保证刚写完的卷是干净的。

### 3.2 legacy BIOS

- 安装盘：stage1（MBR）→ stage2 → 按参数块用 EDD 读 ext4 内的
  `KERNEL.BIN`。
- ISO：El Torito 改为 **no-emulation**。`bios.img` 只含 stage1 + stage2，
  固件把整块镜像装到 0x7C00；stage1 检测“已驻留”标志后直接跳 0x8000。
  `KERNEL.BIN` 作为普通 ISO9660 文件放在光盘根，构建后由 Ruby 工具
  （规则 6）解析 El Torito 目录与 ISO9660 目录记录，把它的 LBA 回填进
  ISO 内的 stage2 参数块。脚本必须校验该文件是单 extent，否则构建失败。

## 4. 系统架构

```
Ring3  /bin/ext4fs
         │ blk_read / blk_write / blk_flush（受信系统调用）
         ▼
Kernel  VFS ── 请求页 + 64 KiB 数据窗 ──► ext4fs
         │
         └─ dev_disk_read/write ──► /bin/drivers（AHCI，Ring3）

Bootloader  UEFI 自带只读 ext4；BIOS 用 LBA 块表
Installer   内核做 MBR/GPT/ESP/stage2；ext4 分区交给 ext4fs
```

- `src/fs/ext4/` 放与 OS 无关的核心：`ext4_io.h` 定义
  `read/write/flush/alloc/free/log` 回调。同一份源码用于：
  - `/bin/ext4fs`（Ring3）
  - Linux 主机测试夹具（文件映射块设备）
- 内核不再编译 ext4 源码；`install.c` 不再包含 `ext4.h`。
- 内核给 `/bin/ext4fs` 与 `/bin/drivers` 同等的恒等映射（按进程名判定），
  以便访问内核分配的控制页与数据窗。
- 启动顺序：`dev_start()` → `fs_service_start()` 等 READY →
  `vfs_mount_data()` → Aqua Desktop Environment。无卷时保持现有行为（`/mnt` 回退到
  initramfs 的 `/data`）。
- FAT32 读写实现暂时留在内核，作为老盘兼容回退；ESP 格式化仍在内核。
  后续可独立迁移，不阻塞本设计。

## 5. 内核 ↔ ext4fs 协议

内核分配一张控制页与 16 张数据页（64 KiB），物理地址经 `spawn_arg`
传给服务。双方通过恒等映射访问，state 机：

```
IDLE → REQUEST → DONE → IDLE
```

控制页：

```c
struct fs_rpc {
    volatile u32 magic;      /* 'ZFS1' */
    volatile u32 state;
    u32 op;
    i32 status;
    u32 flags;
    u32 length;              /* 数据窗内有效字节数 */
    u64 offset;              /* 文件内偏移 */
    u64 value0, value1;      /* FORMAT: base/sectors；CONTIG 回填 LBA/块数 */
    char path[256];
    char label[17];
};
```

操作：

| op | 语义 |
|---|---|
| MOUNT | 探测所有磁盘分区，按卷标优先挂 `ZHAOXUROOT`；回填卷信息 |
| STAT | 路径 → is_dir/size |
| LIST | 路径 → 序列化条目（类型+大小+名字），分块返回 |
| READ | 路径+offset+len → 数据窗 |
| WRITE | 路径+offset+len ← 数据窗（截断/追加由 flags 指定） |
| UNLINK | 删除文件 |
| SYNC | flush 元数据与块设备 |
| FORMAT | disk/base/sectors/label → 新建 ext4 卷 |
| WRITE_CONTIG | 写单个连续文件，回填起始 LBA 与块数 |

内核每次只允许一个未完成请求（VFS 锁），超时 5 s 后把卷标记为 IO 错误。
控制页里的路径与数据窗分离，避免把 300 字节路径塞进 64 字节 IPC 消息。

## 6. 受信块 I/O 系统调用

新增系统调用（编号接在 37 之后）：

```
38 = blk_count()                       -> 磁盘数
39 = blk_info(disk, *sectors, *port)   -> 0/-1
40 = blk_read(disk, lba, count, buf)   -> 扇区数/-1
41 = blk_write(disk, lba, count, buf)  -> 扇区数/-1
42 = blk_flush(disk)                   -> 0/-1
```

- 仅名字为 `/bin/ext4fs` 的进程拥有 `CAP_BLOCK`；其他进程调用返回 -1。
- 内核校验用户缓冲（`user_range_ok`），内部复用 `dev_disk_read/write`，
  受 `g_dev_busy` 串行化，避免两个 Ring3 进程并发写驱动共享页。
- 单次最多 8 个扇区（4 KiB），与现有 dev RPC 上限一致。

## 7. ext4 功能矩阵

### 7.1 挂载与特性

- 支持：`extents`、`filetype`、`64bit`、`flex_bg`、`meta_bg`、
  `sparse_super`/`sparse_super2`、`metadata_csum`、`uninit_bg`、
  `extra_isize`、`large_file`、`huge_file`、`dir_index`、`journal`、
  `orphan_file`、`inline_data`（只读）。
- 拒绝（置只读并报错）：`bigalloc`、`encrypt`、`casefold`、`verity`。
- 日志：挂载时若 `s_errors`/日志需要恢复则回放 JBD2（v2/v3 校验和）；
  写路径按 metadata 日志提交事务。
- 孤儿：挂载时清理 `s_last_orphan` 与 `orphan_file`。

### 7.2 读

- 文件数据：内联 extent、extent 树（含 uninitialized 与空洞）、
  间接块映射（ext2/3）、inline_data。
- 目录：线性目录 + htree（逐叶块线性扫描；不依赖 dx 索引）。
- 元数据：校验 csum（不一致时该对象按损坏处理，不崩溃）。

### 7.3 写

- 分配器：组描述符 32/64、flex_bg 元数据组、位图更新与 csum。
- 文件：extent 树插入/分裂、间接块分配、truncate、空洞。
- 目录：线性插入/删除/分裂；对 htree 目录先做叶块内插入，空间不足时
  分裂并更新 dx 索引。
- 元数据：superblock、GDT、位图、inode、extent 块、目录块、日志的
  csum 全链路更新。
- 日志：JBD2 描述符/提交块、revoke、v3 校验和；先写日志再写原位。
- `fsync`/`SYNC` 映射为日志提交 + `blk_flush`。

### 7.4 明确不做

配额管理、ACL/xattr 写入、项目配额、加密、压缩、快照、`mmp`。

## 8. 安装器流程

1. 扫描目标盘，校验空白或既有 Zhaoxu 布局（现有逻辑）。
2. 计算布局：ESP 64 MiB，其余给 ext4 根。
3. 通过 RPC `FORMAT` 建 ext4（卷标 `ZHAOXUROOT`）。
4. `READ` 本机 `boot_info` 字体、运行中内核镜像：
   - `WRITE` `/KERNEL.ELF`（UEFI 用）
   - `WRITE_CONTIG` `/KERNEL.BIN`（BIOS 用，回填 LBA/块数）
   - `WRITE` `/WQY.TTC`
5. 内核写 MBR/GPT、stage2（回填参数块）、ESP（仅 `BOOTX64.EFI`）、
   空隙清零、`blk_flush`。
6. 验证：ESP 是 FAT32、ext4 魔数、stage1 与参数块一致。

`ext4fs` 需要字体时可直接调用 `boot_font` 系统调用；内核也可以按旧路径
把字体随 `WRITE` 发送，二者等价。

## 9. VFS 行为

- `/` 仍是 initramfs；`/mnt` 由 `ext4fs` 提供。
- `vfs_mount_data()`：先请求 `ext4fs` 的 `MOUNT`；失败时回退内核 FAT32，
  再失败则回退 `/data` 镜像。
- `open/read/close` 改为流式：fd 只保存路径、文件大小与偏移；`read`
  按 64 KiB 数据窗分批向服务 `READ`，直接拷入调用者的用户缓冲，
  不再把整个文件读进内核堆，因此不再受 16 MiB 上限约束
  （`vfs_read_all` 保留给确实需要整块的旧路径，上限维持现状）。
- `write_file`/`unlink` 通过 `WRITE`/`UNLINK`。
- `readdir` 通过 `LIST`，内核把序列化条目还原成现有回调。
- 内核 fs 系统调用在服务未就绪时返回 -1，不再 panic。

## 10. 构建与打包

- `src/fs/ext4/`：核心源码 + `ext4_io.h`；不进内核对象列表。
- 新增 `src/apps/ext4fs/main.c`：服务循环 + 块 I/O 回调；打包进
  initramfs 的 `/bin/ext4fs`，Makefile 的 `RAMFS_FILES` 同步。
- `src/tests/ext4/`：主机夹具；`tools/ext4-test.rb` 驱动（规则 6）。
- 内核 `C_SRCS` 删除 `kernel/fs/ext4.c`、`kernel/fs/ext4_write.c`。
- `make all` 删除 `esp/EFI/BOOT/KERNEL.ELF` 依赖与拷贝；
  `clean` 同步删除。
- ISO：`bios.img` 改为 no-emulation 的 stage1+stage2 镜像；
  `install/KERNEL.BIN` 为普通文件；`tools/iso-bios.rb` 构建后回填 LBA。
- `tools/bios-img.rb` 不再把内核拼进引导镜像，只输出 stage1+stage2。

## 11. 测试策略

### 11.1 主机夹具（先于 QEMU）

- Linux 下编译 ext4 核心，块设备用文件映射。
- `tools/ext4-test.rb` 用矩阵生成镜像：
  - 文件系统：ext2、ext3、ext4
  - 块大小：1 KiB、2 KiB、4 KiB
  - 选项：`-O 64bit`、`-O metadata_csum`、`-O flex_bg`、
    `-O meta_bg`、`-O ^has_journal`、`-O dir_index`
- 每轮执行：读全部文件 → 写新文件/覆盖/删除/建目录 → `sync` →
  `e2fsck -fn`，并断言 `debugfs` 可见内容一致。
- 故障注入：在日志提交的每个阶段丢弃后续写，重新挂载后必须能回放或
  回到干净状态。
- 开启 ASAN/UBSAN。

### 11.2 QEMU 回归

1. `make -j4`；`nm out/kernel.elf | grep ext4_` 必须为空；无未定义符号。
2. ISO：UEFI 与 BIOS 均能进桌面，日志显示内核来自 `/install`。
3. 安装：`make run-install`（GPT）与 `make run-install-bios`（MBR），
   或 headless 8sh `install mbr|gpt`。
4. 安装后仅用目标盘启动：UEFI 与 BIOS 都显示
   `Kernel: /KERNEL.ELF from ext4.` / stage2 参数读取，
   `[ext4fs] mounted "ZHAOXUROOT"`，中文正常。
5. 用 `mtools` 列出 ESP，断言只有 `EFI/BOOT/BOOTX64.EFI`；
   用 `dd` 读取 LBA0/1，断言不含内核 ELF 魔数。
6. 在 8sh 中写/读/删 `/mnt` 文件；重启后内容仍在。
7. 把目标盘换成 `mkfs.ext4` 默认参数预格式化的镜像，验证只读挂载。

## 12. 里程碑与验收

### M0 服务骨架与去内核化

- 抽取 `src/fs/ext4/` 核心，保留现有功能；主机夹具可跑通当前卷。
- 新增 `/bin/ext4fs`、RPC、VFS 转发、块 I/O 系统调用。
- 安装器改走 RPC；删除内核 ext4 源码与 ESP 内核拷贝。
- 修复 BIOS ISO 启动（El Torito no-emulation + LBA 回填）。
- 验收：UEFI/BIOS 安装并重启进桌面；ESP 只有加载器；`nm` 无 ext4。

### M1 只读互操作

- JBD2 回放、64bit、flex_bg/meta_bg、间接块、extent 树、
  csum 校验、htree、inline_data、稀疏文件。
- 验收：主机矩阵全部可读，`e2fsck -fn` 无错。

### M2 写互操作

- 分配器、extent 树增长、目录分裂/htree 插入、间接块写入、
  csum 全链路、JBD2 提交、孤儿处理。
- 验收：主机矩阵读写后 `e2fsck -fn` 干净；断电注入可恢复。

### M3 加固

- 大文件/大目录、ENOSPC 语义、只读降级、错误注入、性能基线。
- 验收：故障注入与压力测试通过。

### M4 文档与发布

- 更新 AGENTS.md（布局、验证、架构）、README、CHANGELOG。
- 全量 QEMU 冒烟清单。

## 13. 风险与缓解

| 风险 | 缓解 |
|---|---|
| JBD2 回放/提交复杂 | M1 先只读回放，M2 再写；主机故障注入 |
| htree 分裂与 csum | 先叶块插入，后分裂；每步 `e2fsck -fn` |
| ISO 回填依赖文件连续 | 构建脚本强制校验单 extent，失败即中止 |
| RPC 往返慢 | 64 KiB 数据窗 + 批量读；后续可多请求并发 |
| 内核移除 ext4 后安装失败 | M0 一次性切换并用 headless 安装回归兜底 |
| 老 FAT32 盘兼容退化 | 保留内核 FAT32 回退，标记为兼容路径 |

## 14. 文件布局

```
src/fs/ext4/           与 OS 无关的 ext4 核心（读、写、日志、csum）
src/apps/ext4fs/       Ring3 服务（main.c、rpc.c、blk.c）
src/kernel/fs/vfs.c    VFS 转发与回退
src/kernel/proc/fsrpc.c 控制页/数据窗、等待与超时
src/kernel/proc/dev.c  现有驱动 RPC（块 I/O 复用）
src/kernel/proc/syscall.c  新增 blk_* 与 CAP_BLOCK 校验
src/kernel/fs/install.c    布局 + ESP + stage2，ext4 交给服务
src/boot/bios_stage1.asm   支持 no-emulation 直跳
src/boot/bios_stage2.asm   参数块读取（EDD/CHS 自适应）
src/tools/iso-bios.rb      ISO9660 + El Torito 回填
src/tests/ext4/            主机夹具
src/tools/ext4-test.rb     主机矩阵测试
```
