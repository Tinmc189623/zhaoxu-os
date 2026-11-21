#!/usr/bin/env python3
"""Validate packed ZXE v1 files against the native dynamic design spec.

Usage: python3 zxe-check.py file.zxe [file.zxe ...]
"""

import os
import sys
from itertools import combinations
from pathlib import Path

MAGIC = b"ZXE\x00"
VERSION = 1
MACHINE = 0x003E
HEADER_SIZE = 128
DIR_ENTRY = 24
PAGE = 4096
MAX_FILE = 256 * 1024 * 1024
MAX_SEGMENTS = 32
MAX_DEPS = 64
MAX_IMPORTS = 4096
MAX_EXPORTS = 4096
MAX_RELOCS = 65536
MAX_STRINGS = 1024 * 1024
IMAGE_WINDOW = 1024 * 1024 * 1024
TABLE_KINDS = {
    1: ("segments", 48), 2: ("dependencies", 16), 3: ("imports", 16),
    4: ("exports", 24), 5: ("relocations", 24), 6: ("strings", 1),
}
SEGMENT_PERMS = {1: 0b101, 2: 0b001, 3: 0b011, 4: 0b011}
RELOC_TYPES = (1, 2, 3, 4)
TABLE_LIMITS = {1: MAX_SEGMENTS, 2: MAX_DEPS, 3: MAX_IMPORTS,
                4: MAX_EXPORTS, 5: MAX_RELOCS, 6: MAX_STRINGS}
ESCAPES = {0x07: "\\a", 0x08: "\\b", 0x09: "\\t", 0x0A: "\\n",
           0x0B: "\\v", 0x0C: "\\f", 0x0D: "\\r", 0x1B: "\\e"}

failures = 0


def check(condition, path, message):
    """Record and print one failed validation."""
    global failures
    if condition:
        return
    failures += 1
    print(f"{path}: FAIL {message}")


def read_int(data, offset, size, signed=False):
    """Little-endian integer at offset; None when no byte remains."""
    chunk = data[offset:offset + size]
    if not chunk:
        return None
    chunk = chunk.ljust(size, b"\x00")
    return int.from_bytes(chunk, "little", signed=signed)


def read_fields(data, offset, sizes, signed=()):
    """Read a run of little-endian fields; None where the data runs out."""
    values = []
    for index, size in enumerate(sizes):
        values.append(read_int(data, offset, size, index in signed))
        offset += size
    return values


def to_s(value):
    """Ruby interpolation: nil prints as an empty string."""
    return "" if value is None else str(value)


def to_i(value):
    """Ruby Integer conversion: nil becomes zero."""
    return 0 if value is None else value


def hexstr(value):
    """Ruby Integer#to_s(16) with nil turning into an empty string."""
    return "" if value is None else format(value, "x")


def utf8_ok(raw):
    """True when raw is non-empty and decodes as strict UTF-8."""
    if not raw:
        return False
    try:
        (raw + b"\x00").decode("utf-8")
    except UnicodeDecodeError:
        return False
    return True


def ruby_inspect(raw):
    """Ruby String#inspect formatting for a binary string."""
    parts = []
    for index, byte in enumerate(raw):
        following = raw[index + 1] if index + 1 < len(raw) else None
        if byte == 0x22:
            parts.append('\\"')
        elif byte == 0x5C:
            parts.append("\\\\")
        elif byte == 0x23 and following in (0x7B, 0x24, 0x40):
            parts.append("\\#")
        elif byte in ESCAPES:
            parts.append(ESCAPES[byte])
        elif 0x20 <= byte <= 0x7E:
            parts.append(chr(byte))
        else:
            parts.append(f"\\x{byte:02X}")
    return '"' + "".join(parts) + '"'


def validate(path):
    """Run every ZXE v1 check for one file."""
    if not os.path.isfile(path):
        sys.exit(f"missing {path}")
    data = Path(path).read_bytes()

    check(len(data) <= MAX_FILE, path, "file exceeds 256 MiB")
    check(len(data) >= HEADER_SIZE, path, "shorter than the fixed header")
    magic = data[0:4]
    check(magic == MAGIC, path, f"bad magic {ruby_inspect(magic)}")
    version, machine, header_size, flags, dir_entry, dir_count = \
        read_fields(data, 4, [2, 2, 2, 2, 2, 2])
    check(version == VERSION, path, f"version {to_s(version)} != 1")
    check(machine == MACHINE, path,
          f"machine 0x{hexstr(machine)} != 0x3e")
    check(header_size == HEADER_SIZE, path,
          f"header_size {to_s(header_size)} != 128")
    check((flags & ~1) == 0, path,
          f"undefined header flags 0x{hexstr(flags)}")
    shared = (flags & 1) != 0
    check(dir_entry == DIR_ENTRY, path,
          f"directory entry size {to_s(dir_entry)} != 24")
    check(1 <= dir_count <= 16, path,
          f"directory_count {to_s(dir_count)} out of range")
    file_size, entry_rva, image_size, dir_offset = \
        read_fields(data, 16, [8, 8, 8, 8])
    check(file_size == len(data), path,
          f"file_size {to_s(file_size)} != {len(data)}")
    check((dir_offset % 8) == 0, path, "directory offset not 8-byte aligned")
    check(dir_offset == HEADER_SIZE, path,
          "directory does not follow the header")
    api_major = read_int(data, 64, 4)
    check(api_major == 1, path, f"api_abi_major {to_s(api_major)} != 1")
    reserved0 = read_int(data, 68, 4)
    check(reserved0 == 0, path, "reserved0 not zero")
    check(data[72:128] == b"\x00" * 56, path, "reserved1 not zero")
    check(image_size > 0 and image_size <= IMAGE_WINDOW, path,
          f"image_size {to_s(image_size)} outside 1 GiB window")
    check((image_size % PAGE) == 0, path, "image_size not page aligned")
    if shared:
        check(entry_rva == 0, path, "shared image entry_rva must be zero")
    else:
        check(entry_rva < image_size, path, "entry_rva outside image")

    dir_end = dir_offset + dir_count * DIR_ENTRY
    check(dir_end <= len(data), path, "directory table truncated")
    dirs = []
    for i in range(dir_count):
        kind, dflags, offset, count, entry_size = \
            read_fields(data, dir_offset + i * DIR_ENTRY, [4, 4, 8, 4, 4])
        dirs.append({"kind": kind, "flags": dflags, "offset": offset,
                     "count": count, "entry_size": entry_size})
    kinds = [d["kind"] for d in dirs]
    check(len(set(kinds)) == len(dirs), path, "duplicate table kind")

    ranges = []
    for d in dirs:
        check((d["flags"] & ~1) == 0, path, "undefined directory flags")
        check((d["offset"] % 8) == 0, path,
              f"table {to_s(d['kind'])} offset not aligned")
        check(d["offset"] >= dir_end, path,
              f"table {to_s(d['kind'])} overlaps directory")
        expected = TABLE_KINDS.get(d["kind"])
        if expected is None:
            check((d["flags"] & 1) == 0, path,
                  f"unknown required table kind {to_s(d['kind'])}")
            check(d["entry_size"] > 0, path,
                  "unknown table entry_size zero")
        else:
            check(d["entry_size"] == expected[1], path,
                  f"table {expected[0]} entry_size "
                  f"{to_s(d['entry_size'])} != {expected[1]}")
            limit = TABLE_LIMITS[d["kind"]]
            check(d["count"] <= limit, path,
                  f"table {expected[0]} count {to_s(d['count'])} > {limit}")
        ends = d["offset"] + to_i(d["count"]) * d["entry_size"]
        check(ends >= d["offset"] and ends <= len(data), path,
              f"table {to_s(d['kind'])} out of bounds")
        ranges.append((d["offset"], ends))
    for a, b in combinations(ranges, 2):
        check(a[1] <= b[0] or b[1] <= a[0], path, "tables overlap")
    metadata_end = max((r[1] for r in ranges), default=0)

    tables = {}
    for d in dirs:
        if d["kind"] in TABLE_KINDS:
            tables[d["kind"]] = (d["offset"], d["count"])
    check(1 in tables and tables[1][1] > 0, path, "missing segment table")
    check(6 in tables and tables[6][1] > 0, path, "missing string table")

    strings_offset, strings_size = tables[6]
    strings = data[strings_offset:strings_offset + strings_size]

    def read_name(offset):
        """Decode a NUL-terminated, non-empty UTF-8 name of at most 255 bytes."""
        if offset >= len(strings):
            return None
        finish = strings.find(b"\x00", offset)
        if finish < 0:
            return None
        raw = strings[offset:finish]
        if not raw or len(raw) > 255 or not utf8_ok(raw):
            return None
        return raw

    segments = []
    seg_offset, seg_count = tables[1]
    for i in range(seg_count):
        typ, perms, file_offset, fsize, msize, rva, alignment = \
            read_fields(data, seg_offset + i * 48, [4, 4, 8, 8, 8, 8, 8])
        check(typ in SEGMENT_PERMS, path,
              f"segment {i} has unknown type {to_s(typ)}")
        check(perms == SEGMENT_PERMS.get(typ), path,
              f"segment {i} permission mismatch")
        check((perms & 0b110) != 0b110, path,
              f"segment {i} is writable and executable")
        check(alignment == PAGE, path,
              f"segment {i} alignment {to_s(alignment)} != 4096")
        check((rva % PAGE) == 0, path, f"segment {i} RVA not page aligned")
        check(msize > 0 and (msize % PAGE) == 0, path,
              f"segment {i} memory size not page aligned")
        check(fsize <= msize, path,
              f"segment {i} file size exceeds memory size")
        check((fsize == 0) if typ == 4 else (fsize > 0), path,
              f"segment {i} zero/data file length mismatch")
        check((file_offset % 8) == 0, path,
              f"segment {i} file offset not aligned")
        check(file_offset >= metadata_end and file_offset <= len(data), path,
              f"segment {i} file offset outside payload area")
        check(file_offset + fsize <= len(data), path,
              f"segment {i} payload truncated")
        check(rva + msize <= image_size, path,
              f"segment {i} exceeds image_size")
        check(rva + msize > rva, path, f"segment {i} range overflows")
        segments.append({"type": typ, "rva": rva, "mem": msize,
                         "file": fsize, "offset": file_offset})
    for a, b in combinations(segments, 2):
        check(a["rva"] + a["mem"] <= b["rva"] or
              b["rva"] + b["mem"] <= a["rva"], path,
              "segment memory ranges overlap")
        if a["file"] == 0 or b["file"] == 0:
            continue
        check(a["offset"] + a["file"] <= b["offset"] or
              b["offset"] + b["file"] <= a["offset"], path,
              "segment file ranges overlap")
    check(max((s["rva"] + s["mem"] for s in segments), default=None) ==
          image_size, path, "image_size does not cover the segments")
    if not shared:
        code = [s for s in segments if s["type"] == 1]
        check(any(s["rva"] <= entry_rva < s["rva"] + s["mem"]
                  for s in code), path,
              "entry_rva not inside a CODE segment")

    imports = []
    if 3 in tables:
        import_offset, import_count = tables[3]
        for i in range(import_count):
            dependency_index, name_offset, abi, iflags = \
                read_fields(data, import_offset + i * 16, [4, 4, 4, 4])
            check(read_name(name_offset) is not None, path,
                  f"import {i} has invalid name")
            check(abi > 0, path, f"import {i} ABI major zero")
            check((iflags & ~1) == 0, path,
                  f"import {i} undefined flags")
            imports.append({"dependency_index": dependency_index, "abi": abi})
    if 2 in tables:
        dep_offset, dep_count = tables[2]
        for i in range(dep_count):
            name_offset, abi, dflags, reserved = \
                read_fields(data, dep_offset + i * 16, [4, 4, 4, 4])
            name = read_name(name_offset)
            check(name is not None, path,
                  f"dependency {i} has invalid name")
            if name is not None:
                check(b"/" not in name and b"\\" not in name and
                      b":" not in name, path,
                      f"dependency {i} name contains a path")
                check(name != b"." and name != b"..", path,
                      f"dependency {i} name is a path")
            check(abi > 0, path, f"dependency {i} ABI major zero")
            check(dflags == 0 and reserved == 0, path,
                  f"dependency {i} reserved fields set")
        for imp in imports:
            check(imp["dependency_index"] < dep_count, path,
                  f"import dependency index "
                  f"{to_s(imp['dependency_index'])} out of range")
    if 4 in tables:
        export_offset, export_count = tables[4]
        seen = set()
        for i in range(export_count):
            name_offset, abi, value_rva, size, eflags = \
                read_fields(data, export_offset + i * 24, [4, 4, 8, 4, 4])
            name = read_name(name_offset)
            check(name is not None, path, f"export {i} has invalid name")
            check(abi > 0, path, f"export {i} ABI major zero")
            check(eflags == 1, path, f"export {i} is not GLOBAL")
            check(value_rva < image_size, path,
                  f"export {i} value outside image")
            check(size == 0 or size <= image_size - value_rva, path,
                  f"export {i} size exceeds image")
            if name is not None:
                key = (name, abi)
                check(key not in seen, path,
                      f"duplicate export {name.decode('utf-8')}")
                seen.add(key)
    if 5 in tables:
        reloc_offset, reloc_count = tables[5]
        for i in range(reloc_count):
            target, rtype, symbol_index, _addend = \
                read_fields(data, reloc_offset + i * 24, [8, 4, 4, 8],
                            signed={3})
            check(rtype in RELOC_TYPES, path,
                  f"relocation {i} type {to_s(rtype)} unsupported")
            check((target % 8) == 0, path,
                  f"relocation {i} target not 8-byte aligned")
            check(target + 8 <= image_size, path,
                  f"relocation {i} target outside image")
            writable = any(s["type"] in (3, 4) and target >= s["rva"] and
                           target + 8 <= s["rva"] + s["mem"]
                           for s in segments)
            check(writable, path, f"relocation {i} target is not writable")
            if rtype == 1:
                check(symbol_index == 0, path,
                      f"RELATIVE relocation {i} has a symbol")
            else:
                check(symbol_index < len(imports), path,
                      f"relocation {i} symbol index out of range")
                check(symbol_index < len(imports) and
                      imports[symbol_index]["abi"] > 0, path,
                      f"relocation {i} import ABI invalid")
    relocations = sum(d["count"] for d in dirs if d["kind"] == 5)
    shared_text = ", shared" if shared else ""
    print(f"{path}: OK ({len(segments)} segments, {len(imports)} imports, "
          f"{relocations} relocations, image {image_size} bytes{shared_text})")


def main(argv):
    """Validate every path given on the command line."""
    for path in argv:
        validate(path)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
