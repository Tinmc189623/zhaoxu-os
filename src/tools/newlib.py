#!/usr/bin/env python3
"""Build newlib 4.6 for x86_64-elf with the host clang.

The ZXE user ABI is position independent, so libc.a must be built with
-fPIC.  autoconf rejects source paths containing spaces, so the tree lives
in $HOME/newlib instead of the repository build directory.

    python3 tools/newlib.py          # download, build and install
    python3 tools/newlib.py --check  # print the install path if present
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

WORK = Path.home() / "newlib"
INSTALL = WORK / "install" / "x86_64-elf"
VERSION = "4.6.0.20260123"


def run(command: list[str], chdir: Path = WORK, env: dict[str, str] | None = None) -> None:
    """Echo and execute one step, aborting on failure."""
    print("+ " + " ".join(str(part) for part in command))
    merged = dict(os.environ)
    if env:
        merged.update(env)
    if subprocess.run([str(part) for part in command], cwd=str(chdir), env=merged,
                      check=False).returncode != 0:
        raise SystemExit("command failed: " + " ".join(str(part) for part in command))


def capture(command: list[str], chdir: Path = WORK, env: dict[str, str] | None = None) -> str:
    """Run a command and return its combined output."""
    merged = dict(os.environ)
    if env:
        merged.update(env)
    result = subprocess.run([str(part) for part in command], cwd=str(chdir),
                            env=merged, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit("command failed: " + " ".join(command) + "\n" + result.stdout + result.stderr)
    return result.stdout + result.stderr


def write_wrapper(path: Path, body: str) -> None:
    """Write one shell shim autoconf calls by name."""
    path.write_text("#!/bin/sh\n" + body + "\n", encoding="utf-8")
    path.chmod(0o755)


def fetch() -> None:
    """Unpack the pinned newlib source package."""
    deb = WORK / "newlib.deb"
    if not deb.is_file():
        run(["apt-get", "download", "newlib-source"])
        candidates = sorted(WORK.glob("newlib-source_*.deb"))
        if not candidates:
            raise SystemExit("apt-get download produced no package")
        shutil.move(str(candidates[-1]), str(deb))

    shutil.rmtree(WORK / "pkg", ignore_errors=True)
    (WORK / "pkg").mkdir(parents=True, exist_ok=True)
    run(["dpkg-deb", "-x", str(deb), str(WORK / "pkg")])

    tarballs = sorted((WORK / "pkg" / "usr" / "src").rglob("newlib-*.tar.*"))
    if not tarballs:
        raise SystemExit("newlib tarball not found in the source package")

    source = WORK / "src"
    if source.is_dir():
        return
    source.mkdir(parents=True, exist_ok=True)
    run(["tar", "-xf", str(tarballs[0]), "-C", str(source), "--strip-components=1"])


def wrappers() -> None:
    """Create the x86_64-elf compiler shims."""
    bindir = WORK / "xbin"
    bindir.mkdir(parents=True, exist_ok=True)
    target = "--target=x86_64-unknown-elf"

    write_wrapper(
        bindir / "x86_64-elf-gcc",
        f'exec clang {target} -ffreestanding -fno-builtin -fuse-ld=lld '
        f'-Wl,-e,main "$@"',
    )
    shutil.copy2(bindir / "x86_64-elf-gcc", bindir / "x86_64-elf-cc")
    write_wrapper(bindir / "x86_64-elf-as",
                  f'exec clang {target} -c -x assembler "$@"')
    write_wrapper(bindir / "x86_64-elf-ar", 'exec llvm-ar-21 "$@"')
    write_wrapper(bindir / "x86_64-elf-ranlib", 'exec llvm-ranlib-21 "$@"')
    write_wrapper(bindir / "x86_64-elf-objcopy", 'exec llvm-objcopy-21 "$@"')


def build() -> None:
    """Configure, build and install newlib."""
    WORK.mkdir(parents=True, exist_ok=True)
    fetch()
    wrappers()

    build_dir = WORK / "build"
    shutil.rmtree(build_dir, ignore_errors=True)
    shutil.rmtree(WORK / "install", ignore_errors=True)
    build_dir.mkdir(parents=True, exist_ok=True)

    env = {
        "PATH": f"{WORK / 'xbin'}:{os.environ.get('PATH', '')}",
        "CFLAGS_FOR_TARGET": "-fPIC -O2",
    }
    run([str(WORK / "src" / "configure"),
         "--target=x86_64-elf", f"--prefix={WORK / 'install'}",
         "--disable-multilib", "--disable-newlib-supplied-syscalls",
         "--disable-libgloss", "--disable-newlib-io-float",
         "--enable-newlib-io-long-long", "--enable-newlib-io-c99-formats"],
        chdir=build_dir, env=env)
    run(["make", f"-j{os.cpu_count() or 4}"], chdir=build_dir, env=env)
    run(["make", "install"], chdir=build_dir, env=env)
    print(f"newlib installed: {INSTALL}")


def main(argv: list[str]) -> int:
    if "--check" in argv:
        print(str(INSTALL) if INSTALL.is_dir() else "missing")
        return 0 if INSTALL.is_dir() else 1
    build()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
