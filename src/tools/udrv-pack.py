#!/usr/bin/env python3
"""Pack a ZXE image into a userland driver bundle (.udrv).

usage: udrv-pack.py --name <driver> [--version N] <input.zxe> <bundle-dir>

A .udrv is a bundle (rule 49): a directory with a text manifest and the ZXE
executable the manifest names.  The kernel resolves `<bundle>/executable`
when it is asked to start the bundle path.
"""

import argparse
import sys
from pathlib import Path

ZXE_MAGIC = b"ZXE\x00"


def main() -> int:
    """Write the bundle directory and its manifest."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("bundle")
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", type=int, default=1)
    args = parser.parse_args()

    image = Path(args.image)
    if not image.is_file():
        raise SystemExit(f"udrv-pack: missing {image}")
    data = image.read_bytes()
    if data[:4] != ZXE_MAGIC:
        raise SystemExit(f"udrv-pack: {image} is not a ZXE image "
                         f"(magic {data[:4]!r})")

    bundle = Path(args.bundle)
    bundle.mkdir(parents=True, exist_ok=True)
    (bundle / "executable").write_bytes(data)
    manifest = (f"name={args.name}\n"
                f"version={args.version}\n"
                f"abi=ZXE-1\n"
                f"executable=executable\n")
    (bundle / "manifest").write_text(manifest, encoding="utf-8")
    print(f"udrv   : {bundle} ({len(data)} bytes executable, "
          f"{args.name} v{args.version})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
