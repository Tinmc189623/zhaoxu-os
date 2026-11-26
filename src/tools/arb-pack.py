#!/usr/bin/env python3
"""Build an Arbor package (.arb) from a staging directory.

usage: arb-pack.py <staging-dir> <output.arb>

The staging directory holds pkg.toml and a payload/ tree; the archive stores
them under <name>-<version>/ exactly as docs/ARBOR.md specifies.  Entries are
written in sorted order with fixed metadata so the same input always produces
the same archive bytes.
"""

import sys
import tarfile
import tomllib
from pathlib import Path


def fail(message: str) -> "NoReturn":
    """Abort with a message on stderr."""
    raise SystemExit(f"arb-pack: {message}")


def load_manifest(staging: Path) -> dict:
    """Read and validate pkg.toml."""
    manifest = staging / "pkg.toml"
    if not manifest.is_file():
        fail(f"{manifest} is missing")
    with manifest.open("rb") as stream:
        data = tomllib.load(stream)
    package = data.get("package", {})
    for key in ("name", "version", "description", "license", "arch"):
        if not package.get(key):
            fail(f"pkg.toml has no package.{key}")
    return package


def main(argv: list[str]) -> int:
    """Pack the staging directory into the output archive."""
    if len(argv) != 3:
        raise SystemExit("usage: arb-pack.py <staging-dir> <output.arb>")

    staging = Path(argv[1])
    output = Path(argv[2])
    if not staging.is_dir():
        fail(f"{staging} is not a directory")
    package = load_manifest(staging)
    prefix = f"{package['name']}-{package['version']}"

    payload = staging / "payload"
    if not payload.is_dir():
        fail("payload/ is missing")

    entries = [staging / "pkg.toml"]
    entries += sorted(p for p in payload.rglob("*") if p.is_file())
    for entry in entries:
        relative = entry.relative_to(staging).as_posix()
        if ".." in Path(relative).parts:
            fail(f"unsafe path {relative}")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w", format=tarfile.PAX_FORMAT) as archive:
        for entry in entries:
            relative = entry.relative_to(staging).as_posix()
            info = archive.gettarinfo(str(entry), f"{prefix}/{relative}")
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            with entry.open("rb") as stream:
                archive.addfile(info, stream)

    print(f"arb-pack: wrote {output} ({output.stat().st_size} bytes, "
          f"{prefix}, {len(entries)} file(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
