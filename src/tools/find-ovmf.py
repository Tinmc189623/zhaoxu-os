#!/usr/bin/env python3
"""Locate the host QEMU OVMF firmware (CODE + VARS) and print it as JSON.

  { "Code": "...", "Vars": "...", "Source": "..." }

ZHAOXU_OVMF_CODE / ZHAOXU_OVMF_VARS win over any discovery.  Older
RAVENEL_OVMF_* variables are still honoured.
"""

import json
import os
import sys
from pathlib import Path

CODE_PATTERNS = ["edk2-x86_64-code*.fd", "OVMF_CODE*.fd", "OVMF.fd"]
VARS_PATTERNS = ["edk2-i386-vars*.fd", "edk2-x86_64-vars*.fd", "OVMF_VARS*.fd"]


def find_first(roots: list[Path], patterns: list[str]) -> Path | None:
    """Return the first matching firmware file under any root."""
    for pattern in patterns:
        for root in roots:
            if not root.is_dir():
                continue
            for hit in sorted(root.rglob(pattern)):
                if hit.is_file():
                    return hit
    return None


def env_pair(names: list[tuple[str, str]]) -> tuple[Path, Path, str] | None:
    """Read a matching CODE/VARS pair from the environment."""
    for code_name, vars_name in names:
        code = os.environ.get(code_name)
        vars_ = os.environ.get(vars_name)
        if not code or not vars_:
            continue
        code_path, vars_path = Path(code), Path(vars_)
        if code_path.is_file() and vars_path.is_file():
            return code_path, vars_path, f"environment {code_name} / {vars_name}"
    return None


def qemu_share_dirs() -> list[Path]:
    """QEMU's own share directory next to the emulator binary."""
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        for name in ("qemu-system-x86_64", "qemu-system-x86_64.exe"):
            candidate = Path(directory) / name
            if candidate.is_file():
                return [candidate.parent / "share"]
    return []


def common_roots() -> list[Path]:
    """Well-known OVMF installation roots."""
    roots = []
    for variable in ("ProgramFiles", "ProgramFiles(x86)"):
        base = os.environ.get(variable)
        if base:
            roots.append(Path(base) / "qemu")
    roots += [Path("/usr/share/OVMF"), Path("/usr/share/edk2"), Path("/usr/share/qemu")]
    return roots


def main() -> int:
    found = env_pair(
        [
            ("ZHAOXU_OVMF_CODE", "ZHAOXU_OVMF_VARS"),
            ("RAVENEL_OVMF_CODE", "RAVENEL_OVMF_VARS"),
        ]
    )

    if not found:
        for roots in (qemu_share_dirs(), common_roots()):
            code = find_first(roots, CODE_PATTERNS)
            vars_ = find_first(roots, VARS_PATTERNS)
            if code and vars_:
                found = (code, vars_, str(roots[0]))
                break

    if not found:
        print(
            "OVMF firmware not found (need edk2-x86_64-code.fd and edk2-i386-vars.fd).",
            file=sys.stderr,
        )
        print(
            "Install QEMU or set ZHAOXU_OVMF_CODE / ZHAOXU_OVMF_VARS.",
            file=sys.stderr,
        )
        return 1

    print(
        json.dumps(
            {"Code": str(found[0]), "Vars": str(found[1]), "Source": found[2]},
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
