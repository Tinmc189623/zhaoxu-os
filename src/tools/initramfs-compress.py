#!/usr/bin/env python3
"""Compress the ustar initramfs embedded in the kernel.

The BIOS stage 2 can only stage 448 KiB below the EBDA, and the live
initramfs is the largest file-backed part of kernel.elf.  Wrap the zlib
stream in a tiny ZIR1 header carrying the uncompressed size so the kernel
can allocate exactly the right amount and inflate it before mounting rootfs.

usage: initramfs-compress.py <input.tar> <output.z>
"""

import struct
import sys
import zlib
from pathlib import Path


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        raise SystemExit("usage: initramfs-compress.py input.tar output.z")

    data = Path(argv[1]).read_bytes()
    compressed = zlib.compress(data, level=9)
    header = b"ZIR1" + struct.pack("<I", len(data))
    Path(argv[2]).write_bytes(header + compressed)
    print(f"initramfs: {len(data)} -> {len(compressed)} bytes (zlib)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
