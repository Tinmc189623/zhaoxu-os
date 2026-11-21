#!/usr/bin/env python3
"""Compile and run the FS+ host format tests."""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FSDIR = ROOT / "fs" / "fsplus"
TESTDIR = ROOT / "tests" / "fsplus"
SOURCES = [
    FSDIR / "fsplus_crc32c.c",
    FSDIR / "fsplus_super.c",
    FSDIR / "fsplus_node.c",
    TESTDIR / "format_test.c",
]


def find_compiler() -> str:
    """First working C compiler from CC, clang, gcc, cc."""
    for candidate in [os.environ.get("CC"), "clang", "gcc", "cc"]:
        if not candidate:
            continue
        if subprocess.run(
            [candidate, "--version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode == 0:
            return candidate
    raise SystemExit("no C compiler found; set CC")


def run(command: list[str]) -> None:
    """Echo and execute one build step."""
    print("+ " + " ".join(command))
    if subprocess.run(command, check=False).returncode != 0:
        raise SystemExit("command failed: " + " ".join(command))


def main() -> int:
    compiler = find_compiler()
    suffix = ".exe" if os.name == "nt" else ""

    with tempfile.TemporaryDirectory(prefix="fsplus-test-") as tmp:
        binary = str(Path(tmp) / f"fsplus_format_test{suffix}")
        run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
             "-I", str(FSDIR), *[str(source) for source in SOURCES],
             "-o", binary]
        )
        run([binary])

    print("FS+ host test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
