#!/usr/bin/env python3
"""Cross-build GNU binutils 2.47 (Fedora 45) for x86_64-zhaoxu.

Source provenance: `binutils-2.47-1.fc45.src.rpm` fetched by
tools/fedora-fetch.py and unpacked by tools/srpm-extract.py; the pristine
`binutils-2.47.tar.xz` inside it is used as-is (no Fedora patches).

    python3 tools/binutils.py          # unpack, configure, build, install
    python3 tools/binutils.py --check  # report the installed tool versions

The output lands in $HOME/binutils-build/install: as, ld, ar, ranlib,
objcopy, objdump, strip and nm prefixed with x86_64-zhaoxu-.  Run inside
WSL2; the build needs a host C compiler and make.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRPM_DIR = ROOT / "build" / "fedora" / "45" / "binutils"
TARBALL = SRPM_DIR / "binutils-2.47.tar.xz"
WORK = Path.home() / "binutils-build"
SOURCE = WORK / "source"
BUILD = WORK / "build"
PREFIX = WORK / "install"
TARGET = "x86_64-zhaoxu"

CONFIGURE_ARGS = [
    f"--target={TARGET}",
    "--disable-nls",
    "--disable-werror",
    "--disable-gdb",
    "--disable-gdbserver",
    "--disable-gprofng",
    "--disable-gold",
    "--disable-sim",
    "--disable-libdecnumber",
    "--disable-readline",
    "--disable-shared",
    "--enable-static",
    "--without-zstd",
    "--without-debuginfod",
]


def run(command: list[str], chdir: Path = BUILD) -> None:
    """Echo and execute one step, aborting on failure."""
    print("+ " + " ".join(str(part) for part in command), flush=True)
    result = subprocess.run([str(part) for part in command], cwd=str(chdir),
                            check=False)
    if result.returncode != 0:
        raise SystemExit("command failed: " + " ".join(str(part) for part in command))


def installed_tools() -> list[Path]:
    """The cross tools this build is expected to install."""
    names = ["as", "ld", "ar", "ranlib", "objcopy", "objdump", "strip", "nm"]
    return [PREFIX / "bin" / f"{TARGET}-{name}" for name in names]


def check() -> int:
    """Report whether every cross tool exists and prints a version."""
    missing = [tool for tool in installed_tools() if not tool.is_file()]
    for tool in installed_tools():
        if tool in missing:
            continue
        result = subprocess.run([str(tool), "--version"], capture_output=True,
                                text=True, check=False)
        line = result.stdout.splitlines()[0] if result.stdout else "(no output)"
        print(f"{tool.name}: {line}")
    if missing:
        print("missing: " + ", ".join(tool.name for tool in missing))
        return 1
    print(f"binutils {TARGET}: OK")
    return 0


def patch_target_tables() -> None:
    """Teach config.sub, bfd and ld the zhaoxu target; upstream lacks it."""
    config_sub = SOURCE / "config.sub"
    text = config_sub.read_text(encoding="utf-8")
    if "| zhaoxu*" not in text:
        needle = "\t| zephyr* \\\n"
        if needle not in text:
            raise SystemExit("config.sub: zephyr entry not found")
        text = text.replace(needle, needle + "\t| zhaoxu* \\\n", 1)
        config_sub.write_text(text, encoding="utf-8")

    bfd_config = SOURCE / "bfd" / "config.bfd"
    text = bfd_config.read_text(encoding="utf-8")
    if "x86_64-*-zhaoxu*" not in text:
        needle = "x86_64-*-gnu*)"
        if needle not in text:
            raise SystemExit("bfd/config.bfd: x86_64-*-gnu* entry not found")
        text = text.replace(needle, "x86_64-*-gnu* | x86_64-*-zhaoxu*)", 1)
        bfd_config.write_text(text, encoding="utf-8")

    ld_config = SOURCE / "ld" / "configure.tgt"
    text = ld_config.read_text(encoding="utf-8")
    if "x86_64-*-zhaoxu*" not in text:
        needle = "x86_64-*-dragonfly*)\ttarg_emul=elf_x86_64"
        if needle not in text:
            raise SystemExit("ld/configure.tgt: dragonfly entry not found")
        text = text.replace(
            needle,
            "x86_64-*-dragonfly* | x86_64-*-zhaoxu*)\ttarg_emul=elf_x86_64", 1)
        ld_config.write_text(text, encoding="utf-8")

    gas_config = SOURCE / "gas" / "configure.tgt"
    text = gas_config.read_text(encoding="utf-8")
    if "i386-*-zhaoxu*" not in text:
        needle = "  i386-*-phoenix*)\t\t\tfmt=elf ;;\n"
        if needle not in text:
            raise SystemExit("gas/configure.tgt: i386 phoenix entry not found")
        text = text.replace(needle, needle + "  i386-*-zhaoxu*)\t\t\tfmt=elf ;;\n",
                            1)
        gas_config.write_text(text, encoding="utf-8")
    print("binutils: added the zhaoxu target to config.sub, bfd, ld and gas")


def prepare() -> None:
    """Unpack the pristine tarball into a space-free work directory."""
    if not TARBALL.is_file():
        raise SystemExit(f"{TARBALL} is missing; fetch and extract the SRPM:\n"
                         "  python3 tools/fedora-fetch.py binutils\n"
                         "  python3 tools/srpm-extract.py "
                         "build/fedora/45/SRPMS/binutils-2.47-1.fc45.src.rpm "
                         "build/fedora/45/binutils")
    shutil.rmtree(SOURCE, ignore_errors=True)
    shutil.rmtree(BUILD, ignore_errors=True)
    SOURCE.mkdir(parents=True)
    run(["tar", "-xJf", str(TARBALL), "-C", str(SOURCE),
         "--strip-components=1"], chdir=WORK)
    patch_target_tables()
    BUILD.mkdir(parents=True, exist_ok=True)


def build() -> int:
    """Configure, compile, install and verify the cross binutils."""
    prepare()
    run([str(SOURCE / "configure"), f"--prefix={PREFIX}", *CONFIGURE_ARGS])
    jobs = str(os.cpu_count() or 4)
    run(["make", f"-j{jobs}"])
    run(["make", "install"])
    return check()


def main() -> int:
    """Parse arguments and run the requested step."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="only report the installed cross tools")
    options = parser.parse_args()
    if options.check:
        return check()
    return build()


if __name__ == "__main__":
    sys.exit(main())
