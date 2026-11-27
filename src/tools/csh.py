#!/usr/bin/env python3
"""Cross-build tcsh 6.24.16 as the Zhaoxu OS csh (rule 1).

The source is the pristine tarball from Fedora 45's tcsh SRPM, vendored at
third_party/tcsh.  Like tools/bash.py, the build is out of tree in
$HOME/csh-build against the newlib install, the POSIX objects from
user/lib/posix, and the ncurses 6.5 libtinfo built by tools/posix-ports.py
for line editing.

    python3 tools/csh.py          # configure, build, produce out/csh.elf
    python3 tools/csh.py --check  # report whether out/csh.elf exists

Run inside WSL2 with clang, lld and the newlib install present.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDORED = ROOT / "third_party" / "tcsh"
TARBALL = VENDORED / "tcsh-6.24.16.tar.gz"
WORK = Path.home() / "csh-build"
SOURCE = WORK / "source"
BUILD = WORK / "build"
XBIN = WORK / "xbin"
REPO = WORK / "repo"
NEWLIB = Path(os.environ.get("NEWLIB_ROOT",
                             Path.home() / "newlib" / "install" / "x86_64-elf"))
TINFO = Path(os.environ.get("TINFO_ROOT",
                            Path.home() / "ncurses-build" / "lib" / "libtinfo.a"))
USER_LD = REPO / "user" / "elf.ld"
# Same set the Makefile compiles: every POSIX glue source provides symbols
# tcsh probes at configure time (setpgid, tcgetpgrp, mmap, clock_gettime).
POSIX_NAMES = sorted(path.stem for path in
                     (ROOT / "user" / "lib" / "posix").glob("*.c"))
OBJDIR = REPO / "build" / "newlib"
CRT0 = OBJDIR / "crt0.o"
POSIX_OBJS = [OBJDIR / f"{name}.o" for name in POSIX_NAMES]


def run(command: list[str], chdir: Path = WORK,
        env: dict[str, str] | None = None) -> None:
    """Echo and execute one step, aborting on failure."""
    print("+ " + " ".join(str(part) for part in command))
    merged = dict(os.environ)
    if env:
        merged.update(env)
    if subprocess.run([str(part) for part in command], cwd=str(chdir), env=merged,
                      check=False).returncode != 0:
        raise SystemExit("command failed: " + " ".join(str(part) for part in command))


def wrapper(name: str, body: str) -> None:
    """Write one shell shim autoconf invokes by name."""
    XBIN.mkdir(parents=True, exist_ok=True)
    path = XBIN / name
    path.write_text("#!/bin/sh\n" + body + "\n", encoding="utf-8")
    path.chmod(0o755)


def wrappers() -> None:
    """Create the cross-tool shims; the ELF enters through user/elf.ld."""
    target = "--target=x86_64-unknown-elf"
    wrapper("x86_64-elf-gcc", f'exec clang {target} -fuse-ld=lld "$@"')
    wrapper("x86_64-elf-cc", f'exec clang {target} -fuse-ld=lld "$@"')
    wrapper("x86_64-elf-as", f'exec clang {target} -c -x assembler "$@"')
    wrapper("x86_64-elf-ar", 'exec llvm-ar-21 "$@"')
    wrapper("x86_64-elf-ranlib", 'exec llvm-ranlib-21 "$@"')
    wrapper("x86_64-elf-objcopy", 'exec llvm-objcopy-21 "$@"')
    wrapper(
        "x86_64-elf-cpp",
        f'exec clang {target} -E -ffreestanding -D_POSIX_C_SOURCE=200809L '
        f'-D_GNU_SOURCE -I{REPO / "user" / "lib" / "posix" / "include"} '
        f'-I{NEWLIB / "include"} "$@"',
    )


def prepare() -> None:
    """Extract tcsh to a space-free path and build the POSIX objects."""
    if not (NEWLIB / "lib" / "libc.a").is_file():
        raise SystemExit("newlib is missing; run: python3 tools/newlib.py")
    if not TINFO.is_file():
        raise SystemExit(f"{TINFO} is missing; build ncurses via tools/posix-ports.py")
    if not TARBALL.is_file():
        raise SystemExit(f"{TARBALL} is missing; unpack the Fedora tcsh SRPM")

    WORK.mkdir(parents=True, exist_ok=True)
    if REPO.exists() or REPO.is_symlink():
        REPO.unlink()
    REPO.symlink_to(ROOT)

    shutil.rmtree(SOURCE, ignore_errors=True)
    SOURCE.mkdir(parents=True, exist_ok=True)
    run(["tar", "-xzf", str(TARBALL), "-C", str(SOURCE),
         "--strip-components=1"])

    targets = [f"build/newlib/{name}.o" for name in POSIX_NAMES]
    targets.append("build/newlib/crt0.o")
    run(["make", "-C", str(REPO), f"-j{os.cpu_count() or 4}", *targets])
    wrappers()


def build() -> None:
    """Configure and build tcsh, then leave the ELF in out/csh.elf."""
    prepare()
    shutil.rmtree(BUILD, ignore_errors=True)
    BUILD.mkdir(parents=True, exist_ok=True)

    cflags = " ".join([
        "-O2", "-std=gnu11", "-ffreestanding", "-fPIE", "-fno-stack-protector",
        "-fno-asynchronous-unwind-tables", "-mno-red-zone",
        "-D_POSIX_C_SOURCE=200809L", "-D_GNU_SOURCE", "-D_POSIX_VERSION=200809L",
        f"-I{REPO / 'user' / 'lib' / 'posix' / 'include'}",
        f"-I{NEWLIB / 'include'}",
    ])
    ldflags = " ".join([
        "-nostdlib", "-Wl,--no-pie,-Bsymbolic,--build-id=none",
        f"-Wl,-T,{USER_LD}", "-Wl,-z,max-page-size=0x1000",
        "-Wl,--no-undefined",
    ])
    libs = " ".join(str(path) for path in [
        CRT0, *POSIX_OBJS, TINFO, NEWLIB / "lib" / "libc.a",
        NEWLIB / "lib" / "libm.a",
    ])

    env = {
        "PATH": f"{XBIN}:{os.environ.get('PATH', '')}",
        "CC": "x86_64-elf-gcc",
        "CPP": str(XBIN / "x86_64-elf-cpp"),
        "CFLAGS": cflags,
        "LDFLAGS": ldflags,
        "LIBS": libs,
        "CC_FOR_BUILD": "cc",
        "CFLAGS_FOR_BUILD": "-O2 -std=gnu17",
        # Out-of-tree gethost.c needs the build dir (config.h) and source dir.
        "CPPFLAGS_FOR_BUILD": "-I. -I" + str(SOURCE),
        "ac_cv_c_undeclared_builtin_options": "none needed",
        "ac_cv_search_tgetent": "none required",
    }

    run([str(SOURCE / "configure"),
         "--host=x86_64-unknown-linux-gnu", "--build=x86_64-pc-linux-gnu",
         "--disable-nls"], chdir=BUILD, env=env)

    run(["make", f"-j{os.cpu_count() or 4}"], chdir=BUILD, env=env)

    binary = BUILD / "tcsh"
    if not binary.is_file():
        raise SystemExit("tcsh binary was not produced")
    output = ROOT / "out" / "csh.elf"
    shutil.copy2(binary, output)
    print("csh ELF: out/csh.elf")


def main(argv: list[str]) -> int:
    if "--check" in argv:
        path = ROOT / "out" / "csh.elf"
        print(str(path) if path.is_file() else "missing")
        return 0 if path.is_file() else 1
    build()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
