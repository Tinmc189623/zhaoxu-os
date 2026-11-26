#!/usr/bin/env python3
"""Validate a userland driver bundle (.udrv).

usage: udrv-check.py <bundle-dir> [...]

Checks the bundle layout rule 49 requires: a manifest naming an executable,
and that executable carrying the ZXE magic.  Prints one line per bundle and
exits 1 when any bundle is invalid.
"""

import sys
from pathlib import Path

ZXE_MAGIC = b"ZXE\x00"
REQUIRED_KEYS = ("name", "version", "abi", "executable")


def check(bundle: Path) -> list[str]:
    """Return the problems found in one bundle; an empty list means valid."""
    problems = []
    if not bundle.is_dir():
        return [f"{bundle}: not a directory"]
    manifest = bundle / "manifest"
    if not manifest.is_file():
        return [f"{bundle}: missing manifest"]
    values = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key.strip()] = value.strip()
    for key in REQUIRED_KEYS:
        if not values.get(key):
            problems.append(f"{bundle}: manifest has no {key}=")
    if values.get("abi") and values["abi"] != "ZXE-1":
        problems.append(f"{bundle}: unsupported abi {values['abi']}")
    executable = bundle / values.get("executable", "executable")
    if not executable.is_file():
        problems.append(f"{bundle}: missing {executable.name}")
    elif executable.read_bytes()[:4] != ZXE_MAGIC:
        problems.append(f"{bundle}: {executable.name} is not a ZXE image")
    return problems


def main(argv: list[str]) -> int:
    """Check every bundle named on the command line."""
    if len(argv) < 2:
        raise SystemExit("usage: udrv-check.py <bundle-dir> [...]")
    failed = 0
    for argument in argv[1:]:
        problems = check(Path(argument))
        if problems:
            failed += 1
            for problem in problems:
                print(problem)
        else:
            print(f"udrv   : {argument} OK")
    print(f"udrv   : {len(argv) - 1 - failed}/{len(argv) - 1} bundle(s) valid")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
