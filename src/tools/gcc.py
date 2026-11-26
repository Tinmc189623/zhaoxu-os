#!/usr/bin/env python3
"""Cross-build GCC 16.2.1 (Fedora 45) stage 1 for x86_64-zhaoxu.

Source: `gcc-16.2.1-20260819.tar.xz` from `gcc-16.2.1-2.fc45.1.src.rpm`
(tools/fedora-fetch.py + tools/srpm-extract.py).  Stage 1 is a C-only
compiler plus libgcc for the target: enough to configure and build glibc.
GCC's own contrib/download_prerequisites supplies GMP/MPFR/MPC/ISL when the
host lacks matching development packages.

    python3 tools/gcc.py          # configure, build and install stage 1
    python3 tools/gcc.py --check  # report the installed cross compiler

Output: $HOME/gcc-build/install/bin/x86_64-zhaoxu-gcc.  The assembler and
linker come from tools/binutils.py ($HOME/binutils-build/install).
"""

import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRPM_DIR = ROOT / "build" / "fedora" / "45" / "gcc"
TARBALL = SRPM_DIR / "gcc-16.2.1-20260819.tar.xz"
WORK = Path.home() / "gcc-build"
SOURCE = WORK / "source"
BUILD = WORK / "build"
PREFIX = WORK / "install"
BINUTILS = Path.home() / "binutils-build" / "install" / "bin"
TARGET = "x86_64-zhaoxu"
# GCC's contrib/download_prerequisites verifies these against
# contrib/prerequisites.sha512 before use; fetching them from a nearby GNU
# mirror avoids the very slow gcc.gnu.org link.
PREREQUISITES = [
    ("gmp", "gmp-6.3.0.tar.bz2"),
    ("mpfr", "mpfr-4.2.2.tar.bz2"),
    ("mpc", "mpc-1.3.1.tar.gz"),
    ("isl", "isl-0.24.tar.bz2"),
    ("gettext", "gettext-0.22.tar.gz"),
]
GNU_MIRROR = os.environ.get("ZHAOXU_GNU_MIRROR",
                            "https://mirrors.tuna.tsinghua.edu.cn/gnu")

CONFIGURE_ARGS = [
    f"--target={TARGET}",
    "--prefix=" + str(PREFIX),
    f"--with-as={BINUTILS / (TARGET + '-as')}",
    f"--with-ld={BINUTILS / (TARGET + '-ld')}",
    "--without-headers",
    "--with-newlib",
    "--enable-languages=c",
    "--disable-shared",
    "--disable-threads",
    "--disable-libssp",
    "--disable-libgomp",
    "--disable-libatomic",
    "--disable-libquadmath",
    "--disable-libvtv",
    "--disable-libstdcxx",
    "--disable-libsanitizer",
    "--disable-nls",
    "--disable-multilib",
    "--disable-bootstrap",
    "--disable-werror",
    "--enable-static",
]


def run(command: list[str], chdir: Path = BUILD,
        env: dict[str, str] | None = None) -> None:
    """Echo and execute one step, aborting on failure."""
    print("+ " + " ".join(str(part) for part in command), flush=True)
    merged = dict(os.environ)
    if env:
        merged.update(env)
    result = subprocess.run([str(part) for part in command], cwd=str(chdir),
                            env=merged, check=False)
    if result.returncode != 0:
        raise SystemExit("command failed: " + " ".join(str(part) for part in command))


def check() -> int:
    """Report the installed cross compiler and its libgcc."""
    compiler = PREFIX / "bin" / f"{TARGET}-gcc"
    if not compiler.is_file():
        print(f"missing {compiler}")
        return 1
    version = subprocess.run([str(compiler), "--version"], capture_output=True,
                             text=True, check=False)
    print(version.stdout.splitlines()[0] if version.stdout else "(no version)")
    libgcc = subprocess.run([str(compiler), "-print-libgcc-file-name"],
                            capture_output=True, text=True, check=False)
    print("libgcc: " + (libgcc.stdout.strip() or "(not found)"))
    if libgcc.returncode != 0 or not Path(libgcc.stdout.strip()).is_file():
        return 1
    print(f"gcc {TARGET}: OK")
    return 0


def patch_target_tables() -> None:
    """Teach GCC's config.sub and target tables the zhaoxu OS name.

    The target reuses the freestanding x86_64 ELF support (no libc headers,
    newlib-style stdint), which is exactly what stage 1 needs.
    """
    config_sub = SOURCE / "config.sub"
    text = config_sub.read_text(encoding="utf-8")
    if "| zhaoxu*" not in text:
        for needle, replacement in (
                ("\t| zephyr* \\\n", "\t| zephyr* \\\n\t| zhaoxu* \\\n"),
                ("| zephyr* \\\n", "| zephyr* | zhaoxu* \\\n")):
            if needle in text:
                text = text.replace(needle, replacement, 1)
                config_sub.write_text(text, encoding="utf-8")
                break
        else:
            raise SystemExit("config.sub: zephyr entry not found")

    gcc_config = SOURCE / "gcc" / "config.gcc"
    text = gcc_config.read_text(encoding="utf-8")
    if "x86_64-*-zhaoxu*" not in text:
        needle = "x86_64-*-elf*)\n"
        if needle not in text:
            raise SystemExit("gcc/config.gcc: x86_64 elf entry not found")
        gcc_config.write_text(
            text.replace(needle, "x86_64-*-elf* | x86_64-*-zhaoxu*)\n", 1),
            encoding="utf-8")

    libgcc_host = SOURCE / "libgcc" / "config.host"
    text = libgcc_host.read_text(encoding="utf-8")
    if "x86_64-*-zhaoxu*" not in text:
        needle = "x86_64-*-elf* | x86_64-*-rtems*)\n"
        if needle not in text:
            raise SystemExit("libgcc/config.host: x86_64 elf entry not found")
        libgcc_host.write_text(
            text.replace(needle,
                         "x86_64-*-elf* | x86_64-*-rtems* | x86_64-*-zhaoxu*)\n", 1),
            encoding="utf-8")
    print("gcc: added the zhaoxu target to config.sub, config.gcc and libgcc")


def fetch_prerequisites() -> None:
    """Place the prerequisite tarballs where GCC's script expects them.

    isl ships inside the Fedora GCC SRPM; the rest come from the GNU mirror,
    cached in $HOME/gcc-build/prereq so retries do not re-download them.  The
    script still verifies every file against contrib/prerequisites.sha512.
    """
    cache = WORK / "prereq"
    cache.mkdir(parents=True, exist_ok=True)
    local_isl = SRPM_DIR / "isl-0.24.tar.bz2"
    for package, name in PREREQUISITES:
        cached = cache / name
        if not cached.is_file() and package == "isl" and local_isl.is_file():
            shutil.copyfile(local_isl, cached)
        if not cached.is_file():
            url = f"{GNU_MIRROR}/{package}/{name}"
            print(f"fetching {url}")
            try:
                with urllib.request.urlopen(url, timeout=30) as response, \
                        cached.open("wb") as output:
                    shutil.copyfileobj(response, output, 1 << 20)
            except OSError as error:
                print(f"warning: {url}: {error}; leaving it to download_prerequisites")
                cached.unlink(missing_ok=True)
                continue
        shutil.copyfile(cached, SOURCE / name)


def prepare() -> None:
    """Unpack GCC into a space-free directory and fetch its prerequisites."""
    if not TARBALL.is_file():
        raise SystemExit(f"{TARBALL} is missing; run tools/fedora-fetch.py gcc "
                         "and tools/srpm-extract.py first")
    for tool in ("as", "ld"):
        if not (BINUTILS / f"{TARGET}-{tool}").is_file():
            raise SystemExit("cross binutils missing; run: python3 tools/binutils.py")
    shutil.rmtree(SOURCE, ignore_errors=True)
    shutil.rmtree(BUILD, ignore_errors=True)
    SOURCE.mkdir(parents=True)
    run(["tar", "-xJf", str(TARBALL), "-C", str(SOURCE),
         "--strip-components=1"], chdir=WORK)
    patch_target_tables()
    fetch_prerequisites()
    script = SOURCE / "contrib" / "download_prerequisites"
    if script.is_file() and not (SOURCE / "mpfr").exists():
        # The script pipes archives through bzip2 for the .bz2 packages; the
        # WSL image often lacks the bzip2 binary, so provide a tiny Python
        # shim on PATH instead of touching the host packages.
        shim_dir = WORK / "bin"
        shim_dir.mkdir(parents=True, exist_ok=True)
        shim = shim_dir / "bzip2"
        shim.write_text(
            "#!/usr/bin/env python3\n"
            "import bz2, sys\n"
            "data = sys.stdin.buffer.read()\n"
            "sys.stdout.buffer.write(bz2.decompress(data))\n",
            encoding="utf-8")
        shim.chmod(0o755)
        environment = dict(os.environ)
        environment["PATH"] = f"{shim_dir}:{environment.get('PATH', '')}"
        print("+ " + str(script))
        if subprocess.run([str(script)], cwd=str(SOURCE),
                          env=environment, check=False).returncode != 0:
            raise SystemExit("prerequisite download failed")
    BUILD.mkdir(parents=True, exist_ok=True)


def build() -> int:
    """Configure, compile and install the stage 1 compiler and libgcc."""
    prepare()
    # libgcc's make rules call the target ar/ranlib/nm by name; put the
    # cross binutils directory on PATH so those resolve.
    environment = dict(os.environ)
    environment["PATH"] = f"{BINUTILS}:{environment.get('PATH', '')}"
    run([str(SOURCE / "configure"), *CONFIGURE_ARGS], env=environment)
    jobs = str(os.cpu_count() or 4)
    run(["make", f"-j{jobs}", "all-gcc", "all-target-libgcc"], env=environment)
    run(["make", "install-gcc", "install-target-libgcc"], env=environment)
    return check()


def main() -> int:
    """Parse arguments and run the requested step."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="only report the installed cross compiler")
    options = parser.parse_args()
    if options.check:
        return check()
    return build()


if __name__ == "__main__":
    sys.exit(main())
