#!/usr/bin/env python3
"""Check a Zhaoxu OS ISO against the media layout RULES.md requires.

usage: iso-check.py out/zhaoxu-os_x64_<version>_<build>_ld.iso

Lists the image with xorriso and compares it with the directory list from
RULES.md (the ISO section).  Missing or extra top-level layout entries are
reported; the exit code is 1 when anything required is absent.
"""

import subprocess
import sys
from pathlib import Path

CONF = Path(__file__).resolve().parent.parent / "boot" / "limine.conf"

REQUIRED_FILES = [
    "boot/limine/limine-bios.sys",
    "boot/limine/limine-bios-cd.bin",
    "boot/limine/kernel.elf",
    "boot/limine/limine-uefi-cd.bin",
    "boot/limine/limine.conf",
    "boot/limine/MiSans.ttf",
    "boot/limine/WALL.RGB",
    "EFI/BOOT/BOOTX64.EFI",
    "EFI/BOOT/limine.conf",
    "casper/filesystem.squashfs",
    "casper/initrd.img",
    "casper/kernel.elf",
    "casper/manifest",
    "casper/vga10h.kext",
    "casper/README.diskdefines",
    "casper/uuid",
    ".disk/casper-uuid-generic",
    ".disk/info",
    ".disk/README",
    "preseed/os.seed",
    "README.html",
    "autorun.inf",
    "md5sum.txt",
]

REQUIRED_DIRS = [
    "amd64",
    "dists/jammy/main",
    "dists/jammy/restricted",
    "dists/jammy/universe",
    "dists/jammy/multiverse",
    "pool/main",
    "pool/restricted",
    "pool/universe",
    "pool/multiverse",
]


def listing(iso: str, kind: str) -> set[str]:
    """Return the ISO's paths of one type ('f' or 'd'), without a leading /."""
    result = subprocess.run(
        ["xorriso", "-indev", iso, "-find", "/", "-type", kind,
         "-exec", "echo"],
        check=False, capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise SystemExit(f"xorriso failed on {iso}: {result.stderr.strip()}")
    paths = set()
    for line in result.stdout.splitlines():
        line = line.strip().strip("'")
        if line.startswith("/") and line != "/":
            paths.add(line[1:])
    return paths


def main(argv: list[str]) -> int:
    """Compare the image with the required layout and print a verdict."""
    if len(argv) != 2:
        raise SystemExit("usage: iso-check.py <iso>")

    files = listing(argv[1], "f")
    dirs = listing(argv[1], "d")

    missing_files = [path for path in REQUIRED_FILES if path not in files]
    missing_dirs = [path for path in REQUIRED_DIRS if path not in dirs]
    extra_files = sorted(files - set(REQUIRED_FILES))

    # Every path and module the boot config names must be on the media.
    referenced = []
    for line in CONF.read_text(encoding="utf-8").splitlines():
        key, _, value = line.strip().partition(":")
        if key in ("path", "module_path"):
            referenced.append(value.strip().replace("boot():/", ""))
    dangling = sorted({path for path in referenced if path not in files})

    for path in missing_files:
        print(f"missing file: {path}")
    for path in missing_dirs:
        print(f"missing dir : {path}")
    for path in extra_files:
        print(f"extra file  : {path}")
    for path in dangling:
        print(f"dangling conf reference: {path}")

    if missing_files or missing_dirs or extra_files or dangling:
        print(f"iso-check: FAIL ({len(missing_files)} files and "
              f"{len(missing_dirs)} dirs missing, {len(extra_files)} extra "
              f"files, {len(dangling)} dangling references)")
        return 1
    print(f"iso-check: PASS ({len(REQUIRED_FILES)} files, "
          f"{len(REQUIRED_DIRS)} dirs, no extras)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
