#!/usr/bin/env python3
"""Unpack a source RPM without rpm2cpio or cpio on the host.

The RPM lead and header are parsed directly to locate the compressed payload;
the payload is inflated with the host's zstd/xz/gzip (whichever its magic
names) and the resulting cpio "newc" archive is expanded in Python.  Paths
are confined to the destination directory.

usage:
    python3 tools/srpm-extract.py out.btrfs-progs.src.rpm build/fedora/45
"""
from __future__ import annotations

import os
import pathlib
import shutil
import stat
import struct
import subprocess
import sys

LEAD_MAGIC = b"\xed\xab\xee\xdb"
HEADER_MAGIC = b"\x8e\xad\xe8\x01"

COMPRESSORS = {
    b"\x28\xb5\x2f\xfd": ["zstd", "-d", "-c"],
    b"\xfd\x37\x7a\x58\x5a\x00": ["xz", "-d", "-c"],
    b"\x1f\x8b": ["gzip", "-d", "-c"],
    b"\x42\x5a\x68": ["bzip2", "-d", "-c"],
}


def header_end(data: bytes, offset: int) -> int:
    """Return the end of one RPM header at offset."""
    if data[offset:offset + 4] != HEADER_MAGIC:
        raise SystemExit(f"bad header magic at {offset}")
    index_count, data_size = struct.unpack(">II",
                                           data[offset + 8:offset + 16])
    return offset + 16 + index_count * 16 + data_size


def payload_offset(data: bytes) -> int:
    """Return the offset of the compressed payload inside the RPM.

    Layout: 96-byte lead, signature header (padded to 8 bytes), main header,
    then the payload.
    """
    if data[:4] != LEAD_MAGIC:
        raise SystemExit("not an RPM: bad lead magic")
    signature_end = header_end(data, 96)
    main = (signature_end + 7) & ~7
    offset = header_end(data, main)
    if offset >= len(data):
        raise SystemExit("RPM payload offset out of range")
    return offset


def decompress(payload: bytes) -> bytes:
    """Inflate the payload with the compressor its magic names."""
    for magic, command in COMPRESSORS.items():
        if payload.startswith(magic):
            result = subprocess.run(command, input=payload,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
            if result.returncode != 0:
                raise SystemExit(command[0] + " failed: " +
                                 result.stderr.decode(errors="replace"))
            return result.stdout
    # Some payloads are stored uncompressed cpio.
    if payload.startswith(b"070701") or payload.startswith(b"070702"):
        return payload
    raise SystemExit("unknown RPM payload compression")


def parse_newc(archive: bytes, destination: pathlib.Path) -> int:
    """Expand a cpio newc archive under destination; return file count."""
    position = 0
    count = 0
    destination = destination.resolve()
    while position + 110 <= len(archive):
        if archive[position:position + 6] not in (b"070701", b"070702"):
            raise SystemExit(f"bad cpio header at {position}")
        fields = [int(archive[position + 6 + i * 8:position + 14 + i * 8], 16)
                  for i in range(13)]
        mode, file_size, name_size = fields[1], fields[6], fields[11]
        name_start = position + 110
        name = archive[name_start:name_start + name_size - 1].decode(
            "utf-8", errors="replace")
        data_start = name_start + name_size
        data_start = (data_start + 3) & ~3
        body = archive[data_start:data_start + file_size]
        position = (data_start + file_size + 3) & ~3
        if name == "TRAILER!!!":
            break

        relative = pathlib.PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise SystemExit(f"refusing unsafe path {name!r}")
        target = destination.joinpath(*relative.parts)
        if not str(target.resolve()).startswith(str(destination)):
            raise SystemExit(f"refusing escape path {name!r}")

        if stat.S_ISDIR(mode):
            target.mkdir(parents=True, exist_ok=True)
        elif stat.S_ISLNK(mode):
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_symlink() or target.exists():
                target.unlink()
            os.symlink(body.decode("utf-8", errors="surrogateescape"), target)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
            if stat.S_ISREG(mode):
                os.chmod(target, stat.S_IMODE(mode) or 0o644)
        count += 1
    return count


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    source = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    data = source.read_bytes()
    archive = decompress(data[payload_offset(data):])
    destination.mkdir(parents=True, exist_ok=True)
    count = parse_newc(archive, destination)
    print(f"srpm   : {source.name} -> {destination} ({count} entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
