#!/usr/bin/env python3
"""Vendor fs/btrfs from the Fedora 45 kernel source tarball.

Rule 39 keeps third-party sources byte-identical: this tool copies
`fs/btrfs/**` out of the pristine kernel tarball without touching a byte,
hashes every extracted file, and writes a provenance ledger.  The kernel
tree is not built here; the port consumes these sources behind the rule-51
VFS provider.

usage:
    python3 tools/btrfs-vendor.py ~/linux-7.2.11.tar.xz
    python3 tools/btrfs-vendor.py linux.tar.xz third_party/fedora/45/kernel
"""
from __future__ import annotations

import hashlib
import pathlib
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def sha256_bytes(data: bytes) -> str:
    """Return the lowercase SHA-256 of one buffer."""
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    source = pathlib.Path(sys.argv[1]).expanduser()
    destination = (pathlib.Path(sys.argv[2]) if len(sys.argv) > 2
                   else ROOT / "third_party" / "fedora" / "45" / "kernel")
    if not source.is_file():
        raise SystemExit(f"missing kernel tarball: {source}")

    tarball_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    extracted: list[tuple[str, str]] = []

    with tarfile.open(source, "r:*") as archive:
        for member in archive:
            if not member.isfile():
                continue
            name = member.name
            marker = "/fs/btrfs/"
            if marker not in name:
                continue
            relative = "fs/btrfs/" + name.split(marker, 1)[1]
            payload = archive.extractfile(member)
            if payload is None:
                continue
            data = payload.read()
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            extracted.append((relative, sha256_bytes(data)))

    if not extracted:
        raise SystemExit(f"no fs/btrfs files in {source}")

    ledger = destination / "SOURCES.md"
    lines = [
        "# Fedora 45 kernel btrfs source ledger",
        "",
        "Vendored by `tools/btrfs-vendor.py` from the pristine kernel tarball",
        "extracted out of the Fedora 45 kernel SRPM.  Files are byte-identical",
        "to the tarball (rule 39); the SRPM's patch set is recorded separately",
        "by `tools/fedora-fetch.py` and is not applied here.",
        "",
        f"- source: `{source.name}` — sha256 `{tarball_hash}`",
        f"- files: {len(extracted)}",
        "",
    ]
    for relative, digest in sorted(extracted):
        lines.append(f"- `{relative}` — sha256 `{digest}`")
    ledger.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"vendored {len(extracted)} files from {source.name} "
          f"into {destination}")
    print(f"ledger: {ledger}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
