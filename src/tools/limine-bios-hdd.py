#!/usr/bin/env python3
"""Extract Limine's embedded BIOS disk image into a raw binary.

usage: limine-bios-hdd.py <limine-bios-hdd.h> <out.bin>
"""

import re
import sys
from pathlib import Path


def extract_image(header_path: Path) -> bytes:
    """Pull the byte array out of the generated C header."""
    text = header_path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"binary_limine_hdd_bin_data\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise SystemExit("limine-bios-hdd: array not found")

    values = re.findall(r"0x([0-9a-fA-F]{2})", match.group(1))
    if not values:
        raise SystemExit("limine-bios-hdd: empty image")
    return bytes(int(value, 16) for value in values)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        raise SystemExit("usage: limine-bios-hdd.py <limine-bios-hdd.h> <out.bin>")

    image = extract_image(Path(argv[1]))
    Path(argv[2]).write_bytes(image)
    print(f"limine-bios-hdd: {len(image)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
