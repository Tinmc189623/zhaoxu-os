#!/usr/bin/env python3
"""Generate docs/PROJECT_TREE.md from the tree on disk.

usage: python3 src/tools/tree-doc.py [--depth N]

Walks the repository, skips build output and version-control data, and writes
one Markdown document: the top-level tree, the src/ breakdown with what each
area owns, and per-area file counts.  Descriptions come from NOTES below, so a
new directory shows up in the tree even before someone describes it.
"""

import argparse
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent      # repository root
SKIP_DIRS = {".git", "build", "out", "node_modules", "__pycache__",
             ".claude", ".superpowers", "iso-root"}
MAX_DEPTH = 3

NOTES = {
    "src": "Build root; every make target runs from here.",
    "src/kernel": "Yuanshuo microkernel: CPU, memory, scheduling, IPC, VFS core.",
    "src/kernel/arch": "x86_64 entry, GDT/IDT, SMP, APIC, context switch.",
    "src/kernel/boot": "Limine protocol bridge; fills struct boot_info.",
    "src/kernel/drivers": "In-kernel timer and interrupt glue only; hardware lives in Ring3.",
    "src/kernel/fs": "VFS core, initramfs, FAT32, ext4 support, installer.",
    "src/kernel/gfx": "Z64 1.0 renderer shared with the desktop.",
    "src/kernel/ipc": "Ports, kernel endpoints, zfd, UNIX sockets, memfd, pipes, epoll.",
    "src/kernel/mem": "PMM, VMM, heap.",
    "src/kernel/personality": "M1 personality API and the FreeBSD port side (OS_5P).",
    "src/kernel/proc": "Processes, threads, syscalls, tty.",
    "src/boot": "Limine configuration booted from the ISO and the ESP.",
    "src/user": "Ring3 programs and libraries; ZXE v1 PIE images.",
    "src/user/lib": "POSIX glue, Wayland compat, software OpenGL and Vulkan.",
    "src/apps": "Ring3 applications: drivers, ext4fs, display, ADE desktop.",
    "src/apps/ADE": "Aqua Desktop Environment on the Desktop Window Compositor.",
    "src/initramfs": "Embedded root filesystem: /bin, /etc, /usr, /lib, /data.",
    "src/tools": "Python/sh/ps1 host and guest tooling (rule 6).",
    "src/tests": "Host-side detection tests.",
    "src/third_party": "Vendored third-party sources, used as shipped (rule 39).",
    "docs": "Designs in docs/superpowers/, install guide, architecture document.",
    "docs/superpowers/specs": "Design specifications, one per subsystem.",
    "docs/superpowers/plans": "Staged implementation plans.",
    "archive": "Retired sources kept for history; not part of any build.",
}


def include(path: Path) -> bool:
    """Skip build output, VCS data and cache directories."""
    return path.name not in SKIP_DIRS


def walk(directory: Path, prefix: str, depth: int, lines: list[str]) -> None:
    """Render one directory level as an ASCII tree."""
    entries = sorted((p for p in directory.iterdir() if include(p)),
                     key=lambda p: (p.is_file(), p.name.lower()))
    for index, entry in enumerate(entries):
        last = index == len(entries) - 1
        branch = "└── " if last else "├── "
        relative = entry.relative_to(ROOT).as_posix()
        note = NOTES.get(relative)
        label = entry.name + ("/" if entry.is_dir() else "")
        line = prefix + branch + label
        if note:
            line += "  — " + note
        lines.append(line)
        if entry.is_dir() and depth < MAX_DEPTH:
            walk(entry, prefix + ("    " if last else "│   "), depth + 1, lines)


def count_files(directory: Path) -> tuple[int, int]:
    """Return (files, directories) under one path, skipping build output."""
    files = 0
    dirs = 0
    for current, subdirs, names in os.walk(directory):
        subdirs[:] = [name for name in subdirs if name not in SKIP_DIRS]
        dirs += len(subdirs)
        files += len(names)
    return files, dirs


def main() -> int:
    """Write docs/PROJECT_TREE.md."""
    global MAX_DEPTH
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--depth", type=int, default=MAX_DEPTH)
    args = parser.parse_args()
    MAX_DEPTH = args.depth

    lines: list[str] = ["# Project tree", "",
                        "Generated from the working tree by "
                        "`src/tools/tree-doc.py`.  Build output (`build/`, "
                        "`out/`) and version-control data are skipped.", "",
                        "```text", "Zhaoxu OS/"]
    walk(ROOT, "", 0, lines)
    lines.append("```")
    lines += ["", "## Areas", "",
              "| Path | Files | Directories | What it holds |",
              "| --- | --- | --- | --- |"]
    for relative in sorted(NOTES):
        path = ROOT / relative
        if not path.is_dir():
            continue
        files, dirs = count_files(path)
        lines.append(f"| `{relative}/` | {files} | {dirs} | {NOTES[relative]} |")
    lines.append("")

    target = ROOT / "docs" / "PROJECT_TREE.md"
    target.write_text("\n".join(lines), encoding="utf-8")
    print(f"tree   : wrote {target.relative_to(ROOT)} "
          f"({len(lines)} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
