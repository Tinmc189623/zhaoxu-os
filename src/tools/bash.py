#!/usr/bin/env python3
"""Cross-build GNU bash 5.3 for Zhaoxu OS.

The source is vendored in third_party/bash (bash-5.3.tar.gz from
https://ftp.gnu.org/gnu/bash/, signed by Chet Ramey, key
7C0135FB088AAF6C66C650B9BB5869F064EA74AB).

autoconf rejects source paths containing spaces, so the vendored tree is
copied to $HOME/bash-build/source and configured out of tree against the
newlib install prepared by tools/newlib.py.

    python3 tools/bash.py          # copy, configure and build out/bash.elf
    python3 tools/bash.py --check  # report whether out/bash.elf exists

Run inside WSL2 with clang, lld and the newlib install present.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDORED = ROOT / "third_party" / "bash"
WORK = Path.home() / "bash-build"
SOURCE = WORK / "source"
BUILD = WORK / "build"
XBIN = WORK / "xbin"
REPO = WORK / "repo"
NEWLIB = Path(os.environ.get("NEWLIB_ROOT", Path.home() / "newlib" / "install" / "x86_64-elf"))
USER_LD = REPO / "user" / "elf.ld"
POSIX_NAMES = ["libgloss", "posix_termios", "posix_signal", "posix_account",
               "posix_path", "posix_dirent", "posix_rlimit", "posix_time",
               "posix_select"]
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


def patch_network_fallback() -> None:
    """Replace bash's network fallback with ENOSYS; Zhaoxu has no network stack."""
    path = SOURCE / "lib" / "sh" / "netopen.c"
    text = path.read_text(encoding="utf-8")
    old = '  internal_error (_("network operations not supported"));\n  return -1;'
    new = "  errno = ENOSYS;\n  return -1;"
    if old not in text:
        raise SystemExit("netopen.c fallback not found")
    text = text.replace(old, new, 1)
    text = text.replace("#else /* !HAVE_NETWORK */",
                        "#else /* !HAVE_NETWORK */\n#include <errno.h>", 1)
    path.write_text(text, encoding="utf-8")


def wrappers() -> None:
    """Create the cross-tool shims; ZXE binaries enter through crt0's _start."""
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
        f'-D_GNU_SOURCE -I{NEWLIB / "include"} '
        f'-I{REPO / "user" / "lib" / "posix" / "include"} "$@"',
    )


def prepare() -> None:
    """Copy the vendored source to a space-free path and build POSIX objects."""
    if not (NEWLIB / "lib" / "libc.a").is_file():
        raise SystemExit("newlib is missing; run: python3 tools/newlib.py")

    WORK.mkdir(parents=True, exist_ok=True)
    if REPO.exists() or REPO.is_symlink():
        REPO.unlink()
    REPO.symlink_to(ROOT)

    shutil.rmtree(SOURCE, ignore_errors=True)
    SOURCE.mkdir(parents=True, exist_ok=True)
    run(["cp", "-a", f"{VENDORED}/.", str(SOURCE)])
    patch_network_fallback()

    # The vendored tree is normalized to LF at checkout, which updates source
    # mtimes; keep the shipped configure newer than its inputs so make does not
    # try to regenerate it (autoconf is not part of the build host).
    for generated in ("configure", "config.h.in", "buildconf.h"):
        path = SOURCE / generated
        if path.is_file():
            path.touch()

    targets = [f"build/newlib/{name}.o" for name in POSIX_NAMES]
    targets.append("build/newlib/crt0.o")
    run(["make", "-C", str(REPO), f"-j{os.cpu_count() or 4}", *targets])
    wrappers()


def build() -> None:
    """Configure and build bash against newlib, then pack the ZXE image."""
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
        CRT0, *POSIX_OBJS, NEWLIB / "lib" / "libc.a", NEWLIB / "lib" / "libm.a",
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
        "ac_cv_c_undeclared_builtin_options": "none needed",
        "bash_cv_func_sigsetjmp": "missing",
        "bash_cv_posix_signals": "yes",
        "ac_cv_func_times": "yes",
        "bash_cv_getcwd_malloc": "yes",
        "bash_cv_termcap_lib": "gnutermcap",
    }

    run([str(SOURCE / "configure"),
         "--host=x86_64-elf", "--build=x86_64-pc-linux-gnu",
         "--without-bash-malloc", "--disable-nls", "--disable-job-control",
         "--without-curses"], chdir=BUILD, env=env)

    # Host tools must not link the target crt0/libc; their generated
    # Makefiles append $(LIBS), which belongs only on the bash link line.
    for directory in ("support", "builtins"):
        makefile = BUILD / directory / "Makefile"
        if not makefile.is_file():
            continue
        text = makefile.read_text(encoding="utf-8").replace(" $(LIBS)", "")
        lines = ["" if line.startswith("LIBS_FOR_BUILD = ") else line
                 for line in text.splitlines()]
        makefile.write_text("\n".join(lines) + "\n", encoding="utf-8")

    run(["make", f"-j{os.cpu_count() or 4}"], chdir=BUILD, env=env)

    binary = BUILD / "bash"
    if not binary.is_file():
        raise SystemExit("bash binary was not produced")
    output = REPO / "out" / "bash.elf"
    shutil.copy2(binary, output)
    print("bash ELF: out/bash.elf")


def main(argv: list[str]) -> int:
    if "--check" in argv:
        path = ROOT / "out" / "bash.elf"
        print(str(path) if path.is_file() else "missing")
        return 0 if path.is_file() else 1
    build()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
