#!/usr/bin/env python3
"""Emit a minimal ZXE v1 shared library used by the dynamic-link smoke test.

The library exports ztest_value (returns 42) from a CODE segment.

usage: zxe-fixture-lib.py out/libztest.zxe
"""

import struct
import sys
from pathlib import Path

MAGIC = b"ZXE\x00"
HEADER = 128
DIR = 24
SEG = 48
EXP = 24
PAGE = 4096

# mov eax, 42; ret
CODE = bytes([0xB8, 42, 0, 0, 0, 0xC3])


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        raise SystemExit("usage: zxe-fixture-lib.py out.zxe")

    strings = b"\x00" + b"ztest_value\x00"
    name_offset = 1

    dir_end = HEADER + DIR * 3
    segments_at = (dir_end + 7) & ~7
    exports_at = segments_at + SEG
    strings_at = exports_at + EXP
    payload_at = (strings_at + len(strings) + 7) & ~7
    image_size = PAGE * 2
    file_size = payload_at + len(CODE)

    out = bytearray()
    out += MAGIC
    out += struct.pack("<6H", 1, 0x3E, HEADER, 1, DIR, 3)  # shared library
    out += struct.pack("<4Q", file_size, 0, image_size, HEADER)
    out += b"\x00" * 16                                     # build id
    out += struct.pack("<2I", 1, 0)
    out += b"\x00" * 56
    if len(out) != HEADER:
        raise SystemExit("header size")

    out += struct.pack("<IIQII", 1, 1, segments_at, 1, SEG)
    out += struct.pack("<IIQII", 4, 1, exports_at, 1, EXP)
    out += struct.pack("<IIQII", 6, 1, strings_at, len(strings), 1)

    out += b"\x00" * (segments_at - len(out))
    out += struct.pack("<IIQQQQQ", 1, 0b101, payload_at, len(CODE), PAGE, PAGE, PAGE)
    out += struct.pack("<IIQII", name_offset, 1, PAGE, 0, 1)
    out += strings
    out += b"\x00" * (payload_at - len(out))
    out += CODE

    if len(out) != file_size:
        raise SystemExit(f"internal size {len(out)} != {file_size}")

    Path(argv[1]).write_bytes(out)
    print(f"fixture library: {file_size} bytes -> {argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
