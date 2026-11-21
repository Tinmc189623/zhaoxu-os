#!/usr/bin/env python3
"""Flatten kernel.elf into the raw image the legacy BIOS stage 2 reads.

The kernel is linked at KERNEL_VMA, so each PT_LOAD segment is placed at
(vaddr - KERNEL_VMA) and the gaps between segments become zeroes.  The output
is padded to a whole 2048-byte CD sector.

usage: kernel-flat.py <kernel.elf> <out.bin>
"""

import struct
import sys
from pathlib import Path

KERNEL_VMA = 0xFFFF_FFFF_8000_0000
CD_SECTOR = 2048
MAX_KERNEL_BYTES = 64 * 1024 * 1024


def load_segments(data: bytes) -> list[tuple[int, int, int]]:
    """Validate the ELF and return (virtual, offset, filesz) load records."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit("kernel-flat: not an ELF64 little-endian image")

    (entry,) = struct.unpack_from("<Q", data, 24)
    if entry != KERNEL_VMA:
        raise SystemExit("kernel-flat: entry is not KERNEL_VMA")

    (phoff,) = struct.unpack_from("<Q", data, 32)
    (phentsize,) = struct.unpack_from("<H", data, 54)
    (phnum,) = struct.unpack_from("<H", data, 56)
    if phoff == 0 or phnum == 0:
        raise SystemExit("kernel-flat: no program headers")

    segments = []
    for index in range(phnum):
        base = phoff + index * phentsize
        p_type, _flags, offset, vaddr, _paddr, filesz, _memsz, _align = \
            struct.unpack_from("<IIQQQQQQ", data, base)
        if p_type != 1 or filesz == 0:
            continue
        if vaddr < KERNEL_VMA:
            raise SystemExit("kernel-flat: segment below KERNEL_VMA")
        virtual = vaddr - KERNEL_VMA
        if offset + filesz > len(data):
            raise SystemExit("kernel-flat: segment outside file")
        segments.append((virtual, offset, filesz))

    if not segments:
        raise SystemExit("kernel-flat: no loadable segments")
    return segments


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        raise SystemExit("usage: kernel-flat.py <kernel.elf> <out.bin>")

    data = Path(argv[1]).read_bytes()
    segments = load_segments(data)

    span = max(virtual + filesz for virtual, _offset, filesz in segments)
    flat = bytearray(span)
    for virtual, offset, filesz in segments:
        flat[virtual:virtual + filesz] = data[offset:offset + filesz]

    padding = (-len(flat)) % CD_SECTOR
    if len(flat) + padding > MAX_KERNEL_BYTES:
        raise SystemExit(
            f"kernel-flat: {len(flat) + padding} bytes exceed the installer "
            f"limit ({MAX_KERNEL_BYTES})"
        )

    flat.extend(b"\x00" * padding)
    Path(argv[2]).write_bytes(flat)
    print(f"KERNEL.BIN: {len(flat)} bytes ({len(flat) // CD_SECTOR} CD sectors)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
