# toybox on Zhaoxu OS

toybox 0.8.12 is built with its complete default configuration. Rule 39 does
not allow trimming it, so every toy in the source tree is compiled and linked;

no toy is disabled because a header or symbol is awkward.

## Layout

| Path | Job |
| --- | --- |
| `src/tools/toybox-build.sh` | regenerates the port headers, builds the support archives and links toybox |
| `src/user/lib/posix/` | the libc surface toybox calls into (one file per concern) |
| `src/third_party/bsd-regex/` | FreeBSD 15.1 regex sources, unmodified (rule 39) |
| `src/third_party/bsd-regex-port/` | the collate shim the regex engine needs |
| `src/third_party/freebsd-libcrypt/` | FreeBSD 15.1 libcrypt, libmd MD4/MD5 and SHA-2, unmodified |
| `src/third_party/freebsd-libcrypt-port/` | endian/cdefs/err headers and the `err(3)` implementation |
| `out/toybox.zxe` | packed executable, staged at `/bin/toybox` with `TOYBOX=1` |

## Prerequisites

The build runs inside WSL2 (Ubuntu 26.04) because the repository path has
spaces and autoconf rejects it:

- `clang`, `ld.lld`, `make`, `ar`, `python3`;
- the newlib install produced by `python3 tools/newlib.py`, at
  `$HOME/newlib/install/x86_64-elf`;
- an unpacked toybox 0.8.12 at `$HOME/toybox` (`src/` inside it is the tree).

## Build

```sh
wsl -d Ubuntu-26.04 -- bash -lc 'cd "/mnt/g/Apps/OS/x86/AMD64/Zhaoxu OS/src" && make TOYBOX=1 out/toybox.zxe'
make TOYBOX=1 iso                 # image with /bin/toybox
make TOYBOX=1 TOYBOX_BOOT_TEST=1 ISO_CMDLINE= iso   # boot test image
```

The boot test kernel runs, in order:

- `/bin/toybox mkpasswd -m sha512 -S saltstring password` — the serial line must
  contain the hash
  `$6$saltstring$adDbXsJjcDlq2662QPgd.tkSOVmnG9Tt3oXl4HR60SusC3AGjirnDenVZp3DGwLwqy6iYKCzannhaX9DR72nN1`;
- `/bin/toybox dmesg` — the kernel log ring holds the boot log (`[klog] N
  bytes buffered`);
- `/bin/toybox mkdir /data/NEWDIR` then `rmdir` — `[mkdir] exit 0 stat 0 dir 1`
  and `[rmdir] exit 0 stat -1`;
- `/bin/toybox mv /data/HELLO.TXT /data/RENAMED.TXT` — `[rename] exit 0 old -1
  new 0`.

The run ends with `[toybox] TOYBOX exit 0`.

## Platform adaptation

toybox is not trimmed, but three platform facts still need a branch:

- `lib/portability.h` and `lib/portability.c` treat `__ZHAOXU_OS__` like the
  BSD targets for `dev_major`/`dev_minor`, `getmntinfo`-based mount lists and
  the termios corners newlib omits.
- The port headers written by `tools/toybox-build.sh` below `$HOME/toybox/
  port-include` declare the calls newlib does not ship.  The same headers are
  kept in `src/user/lib/posix/include/` so native newlib programs see one
  contract.
- `link` is invoked through lld with `src/user/user.ld`, `-fpic`, and a
  version script that satisfies the `FBSD_1.0` symbol versions carried by the
  FreeBSD regex objects.

## What the calls really do

The glue under `src/user/lib/posix/` answers with the kernel's actual
capability; it never fakes success.

- `posix_net.c`: AF_UNIX stream sockets are real (kernel sockets 43-49),
  numeric `inet_pton`/`inet_ntop` and numeric `getaddrinfo` are real, DNS and
  AF_INET are refused with `EAFNOSUPPORT`/`EAI_SYSTEM` until the FreeBSD
  network stack is exposed to Ring 3.
- `posix_kqueue.c`: `kqueue`/`kevent` are served by the kernel epoll objects
  (READ/WRITE filters only).
- `posix_clock_extra.c`, `posix_proc_extra.c`, `posix_poll.c`: real RTC,
  uptime, CPU count, page size and `poll`.
- `posix_file_extra.c`: memfd-backed `mmap`, `munmap`, `ftruncate`; `fsync`
  succeeds because descriptor writes reach the VFS synchronously.
- `posix_path.c`/`posix_path_extra.c`: `mkdir`/`rmdir`/`rename` are real
  through syscalls 82/83/84 on the rootfs copy-up layer (an upper ramfs over
  the read-only initramfs); a lower-only file is copied up before it is
  renamed. The remaining metadata calls (`chmod`, ownership, timestamps)
  still report `ENOSYS`/`ENOTSUP`, and `chown` to uid/gid 0 succeeds because
  that is the only identity.
- `posix_mount.c`, `posix_xattr.c`, `posix_inotify.c`, `posix_utmp.c`,
  `posix_syslog.c`: report `ENOSYS`/`ENOTSUP` where the VFS has no mount
  table, attribute store, watch or login record.
- `posix_group.c`, `posix_account_r.c`: parse `/etc/group` and `/etc/shadow`
  through the VFS.
- `crypt()` is the FreeBSD 15.1 implementation (`src/third_party/
  freebsd-libcrypt/`): SHA-512, SHA-256 and MD5 crypt plus NT-hash, linked as
  `libfbcrypt.a`. `mkpasswd -m sha512 -S saltstring password` reproduces the
  `openssl passwd` hash exactly.
- `klogctl` reads the kernel's 64 KiB log ring through syscall 81, so `dmesg`
  shows the real boot log.

## Known gaps

- `crypt()` has no DES or Blowfish: FreeBSD keeps those in `secure/lib`,
  which is not vendored, so legacy `_`/`$2` hashes cannot be verified.
- `sysinfo` reads `/proc/meminfo`; load averages stay zero.
- The 16 MiB `sbrk` arena in `libgloss.c` means a toybox process reserves
  about 17 MiB of zeroed BSS at load time.

Copyright © 2026 Nexsteaduser. All Rights Reserved.
