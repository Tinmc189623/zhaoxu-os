# glibc 2.44 端口计划（x86_64-zhaoxu）

来源：Fedora 45 `glibc-2.44-1.fc45.src.rpm` 内的 `glibc-2.44-7-g9bcb85688e.tar.xz`，由 `tools/srpm-extract.py` 解包，`tools/glibc.py` 解到 `$HOME/glibc-build/source`。binutils 2.47 的 `x86_64-zhaoxu-*` 交叉工具由 `tools/binutils.py` 产出到 `$HOME/binutils-build/install`。

## 目标与边界

1. 先把 **静态** glibc 跑起来（`--disable-shared --enable-static`）：`user/glibc_hello.c` 在 `out/target.img` 上打印、分配内存、读环境变量并退出 0。这是 btrfs-progs 的前置。
2. 再做动态：`PT_INTERP` 加载 `/lib/ld.so.1`，内核 `elf_loader.c` 增加解释器映射与 `AT_BASE`。
3. 端口文件放 `third_party/glibc-port/`（新增、可审查的小文件），构建时由 `tools/glibc.py` 复制进上游树；上游 tar 保持原样，Fedora 补丁不打。
4. 内核改动只走已定义的 0–112 调用；缺什么就补内核调用，不在 glibc 里伪造系统行为。

## 端口树（第一切片）

```
third_party/glibc-port/
  sysdeps/x86_64/zhaoxu/
    Implies            # x86_64, unix/sysv/zhaoxu
    preconfigure       # 记录 libc_cv_*（stack guard、ifunc 关闭）
    sysdep.h
    dl-sysdep.h
    kernel_sigaction.h
    bits/{stat,fcntl,mman,socket,types,errno,...}.h
  sysdeps/unix/sysv/zhaoxu/
    Implies            # gnu, unix/inet
    Makefile
    syscalls.list      # name caller syscall-name args strong weak
    arch-syscall.h     # #define __NR_read 4 …（按 userlib.h 生成）
    bits/syscall.h     # SYS_read 等，包住 arch-syscall.h
    configure          # 架构能力探测的最小回答
    configure.ac
    kernel-features.h  # 关掉 Linux 专有特性
    bits/local_lim.h、bits/ioctls.h、bits/termios.h 等
```

`syscalls.list` 的列格式与 Linux 版一致（`read - read i:ib __read read`），由 `sysdeps/unix/make-syscalls.sh` 生成汇编桩，桩体来自 `sysdeps/unix/syscall-template.S`；`sysdeps/x86_64/zhaoxu/sysdep.h` 只需给出 x86_64 的 `PSEUDO`/`SYSCALL_CANCEL` 宏（`int 0x80`，`rax` 号，`rdi/rsi/rdx/r10/r8/r9` 参数），并让 `__NR_*` 可见（`sysdeps/unix/sysv/linux/x86_64/64/arch-syscall.h` 是可直接参照的格式）。

`Implies` 不引入 `nptl`：内核还没有 `clone`，线程库先编译为“创建失败返回 ENOSYS”的桩，静态程序单线程运行。`pthread_*` 符号仍导出，避免链接期失败。

## 系统调用映射

glibc 端口层用 `sysdeps/unix/sysv/zhaoxu/syscalls.list` 与手写汇编包装（`int 0x80`，`rax` 号，`rdi/rsi/rdx/r10/r8/r9` 参数），与 `kernel/proc/syscall.c` 一一对应：

| glibc 符号 | 编号 | 说明 |
| --- | --- | --- |
| read/write/close | 4/1/5 | 旧约定 `-1`，经 `SYS_LAST_ERROR` 取 errno |
| open/openat | 3/106* | 106 与块设备通道一起补 |
| stat/fstat/lseek | 77/107*/53 | 现在返回 is_dir/size 的简化结构，端口层转成 `struct stat` |
| mmap/munmap/mprotect/madvise | 54/55/86/87 | 匿名走 `anon.c`，memfd 走旧路径 |
| brk | 85 | |
| arch_prctl | 88 | `ARCH_SET_FS`/`ARCH_GET_FS` |
| set_tid_address/gettid/exit_group | 89/90/91 | |
| futex | 92 | WAIT/WAKE/BITSET |
| rt_sigaction/procmask/sigreturn/kill/tgkill | 93–97 | 32 字节 sigaction、Linux 帧布局 |
| getrandom/clock_gettime | 98/99 | |
| prlimit64/getrlimit | 101/102 | 静态限制值 |
| uname/access | 103/104 | |
| last_error | 112 | 旧调用 errno 通道 |

`clone` 在端口层返回 `ENOSYS`；`clone3` 同。`readlink` 已是 `ENOSYS`，glibc 的 `realpath` 走普通路径分支。

## 构建与验证

```
python3 tools/binutils.py          # 交叉 as/ld/ar，必须 x86_64-zhaoxu-ld --version 通过
python3 tools/gcc.py               # 阶段 1：C 编译器 + libgcc（--without-headers）
python3 tools/glibc.py             # 解包 + 安装端口树 + configure + make
```

`tools/gcc.py` 的 GCC 自带 `contrib/download_prerequisites` 负责 GMP/MPFR/MPC/ISL（带各自校验和），GCC 的汇编器/链接器用 `--with-as/--with-ld` 指向 binutils 的输出，因此两个工具链前缀（`$HOME/binutils-build/install`、`$HOME/gcc-build/install`）保持独立、路径写死可复现。

### 已确认的 configure 前置（2026-11-27）

- glibc 只接受 linux/gnu 平台，新端口须 `--enable-hacker-mode`；
- 端口必须放在 `sysdeps/x86_64/zhaoxu`（machine/os 目录），否则报 `Operating system zhaoxu is not supported`；共享的 OS 文件放 `sysdeps/unix/sysv/zhaoxu`，由前者的 `Implies` 引入；
- `abi-tags` 没有兜底项，必须加分给 zhaoxu 的 ABI tag（`tools/glibc.py` 追加 `.*-.*-zhaoxu.* 6 0.8.0`）；
- 宿主机没有 `bison` 时 configure 直接拒绝；发布 tar 自带生成的 parser，用只回答 `--version` 的替身即可；
- 第一次 `make` 只生成 `build/sysd-sorted`，`soversions.mk`/`libc-modules.h` 要等第二次 `make` 才出现，脚本因此重试一次；
- 交叉 gcc 被 configure 判定为 "sufficient to build libc"。

下一步的构建错误会集中在 sysdeps 缺文件（bits 头、sysdep.h 的 PSEUDO/INTERNAL_SYSCALL 宏、stat/readdir 的 ABI 适配），按错误逐个补。

`tools/glibc.py` 的关键参数（逐项写进脚本，便于复现）：

```
--host=x86_64-pc-zhaoxu --build=x86_64-pc-linux-gnu
--prefix=/usr --disable-shared --enable-static --disable-nls
--disable-werror --disable-profile --disable-timezone-tools
--disable-multi-arch --without-selinux --without-gd --disable-crypt
CC="$HOME/gcc-build/install/bin/x86_64-zhaoxu-gcc"
```

首轮允许 `libc_cv_*` 缓存变量（`libc_cv_forced_unwind=no`、`libc_cv_c_cleanup=no`、`libc_cv_ssp=no`、`libc_cv_ssp_strong=no` 等），每一条都记在脚本注释里，不允许“猜着关”。

门槛：

- 门 1：`configure` 通过，`make -j` 编出 `libc.a`；
- 门 2：`user/glibc_hello.c` 以 `-static -no-pie -Ttext-segment=0x10000000000` 链接，`GLIBC_BOOT_TEST=1` 在安装盘上输出 `[glibc] hello ...` 并 `exit 0`；
- 门 3：`printf`/`malloc`/`fopen`/`opendir`/`getenv`/`errno` 全覆盖后再接 btrfs-progs。

## 已知难点

- **TLS**：静态 glibc 启动时 `TLS_INIT_TP` 调 `arch_prctl(ARCH_SET_FS)`，内核已支持；`sysdeps/x86_64/nptl/tls.h` 依赖 nptl 目录，端口要提供一份最小 `tls.h` 并把 `TLS_TCB_AT_TP`/`TLS_DTV_AT_TP` 固定为 x86_64 变体。
- **IFUNC**：关掉多架构（`--disable-multi-arch`），避免依赖 `dl_runtime_resolve`。
- **vDSO**：没有 vDSO，`clock_gettime` 直接走系统调用；`gettimeofday` 由 `clock_gettime` 组合。
- **动态阶段**：`ld.so` 需要 `PT_INTERP`、`AT_BASE`、`AT_PHDR` 指向可读的程序头（当前 lld 布局下头不在 LOAD 段，动态阶段改用能映射头的链接脚本），以及内核栈从 32 页扩到 256 页。
- **locale/iconv**：首轮 `--disable-nls`，locale 数据用内置 C/POSIX，不打包 locale-archive。

## 顺序

1. binutils（已完成：`x86_64-zhaoxu-{as,ld,ar,ranlib,objcopy,objdump,strip,nm}` 2.47.20260726，目标表补丁在 `tools/binutils.py`）；
2. GCC 阶段 1（`tools/gcc.py`：C + libgcc，`--without-headers`）；
3. 端口树骨架 + configure 门 1；
4. `glibc_hello` 门 2；
5. libc 面补齐（stdio/malloc/dirent/env/time）门 3；
6. 动态 `ld.so`；
7. 接 btrfs-progs（阶段 4）与 csh（阶段 3）。
