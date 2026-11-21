#!/usr/bin/env python3
"""Convert a constrained x86-64 ELF64 PIE plus ZXE JSON metadata sections.

usage: zxe-pack.py input.elf output.zxe [--shared]
"""

import hashlib
import json
import struct
import sys
from pathlib import Path

MAGIC = b"ZXE\x00"
VERSION = 1
MACHINE_X86_64 = 0x003E
HEADER_SIZE = 128
DIRECTORY_SIZE = 24
MAX_FILE = 256 * 1024 * 1024
MAX_SEGMENTS = 32
MAX_DEPS = 64
MAX_IMPORTS = 4096
MAX_EXPORTS = 4096
MAX_RELOCS = 65536
MAX_STRINGS = 1024 * 1024
PAGE = 4096

TABLES = [
    (1, "segments", 48),
    (2, "dependencies", 16),
    (3, "imports", 16),
    (4, "exports", 24),
    (5, "relocations", 24),
    (6, "strings", 1),
]

RELOCATIONS = {"RELATIVE": 1, "64": 2, "GLOB_DAT": 3, "JUMP_SLOT": 4}
ELF_RELOCATIONS = {1: "64", 6: "GLOB_DAT", 7: "JUMP_SLOT", 8: "RELATIVE"}


def fail(message: str) -> None:
    """Abort the pack with a clear message."""
    raise SystemExit(message)


def align8(value: int) -> int:
    """Round up to the 8-byte table alignment."""
    return (value + 7) & ~7


def parse_elf(data: bytes) -> tuple[int, list[dict], list[dict]]:
    """Validate the ELF64 header and return entry, segments and sections."""
    if len(data) > MAX_FILE:
        fail("ELF input exceeds 256 MiB")
    if len(data) < 64 or data[0:4] != b"\x7fELF" or data[4] != 2 \
            or data[5] != 1 or data[6] != 1:
        fail("not an ELF64 little-endian image")

    e_type, e_machine, e_version = struct.unpack_from("<HHI", data, 16)
    if not (e_type == 3 and e_machine == 62 and e_version == 1):
        fail("expected ELF64 PIE (ET_DYN) for x86-64")

    entry, phoff, shoff = struct.unpack_from("<3Q", data, 24)
    (_e_flags, ehsize, phentsize, phnum, shentsize, shnum, shstrndx) = \
        struct.unpack_from("<IHHHHHH", data, 48)
    if not (ehsize >= 64 and phentsize >= 56 and shentsize >= 64
            and phnum > 0 and shnum > 0 and shstrndx < shnum):
        fail("invalid ELF header sizes")

    ph_end = phoff + phentsize * phnum
    sh_end = shoff + shentsize * shnum
    if ph_end < phoff or ph_end > len(data):
        fail("ELF program-header table out of bounds")
    if sh_end < shoff or sh_end > len(data):
        fail("ELF section-header table out of bounds")

    programs = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        (p_type, flags, file_off, vaddr, _paddr, file_size, mem_size,
         align) = struct.unpack_from("<IIQQQQQQ", data, offset)
        programs.append((p_type, flags, file_off, vaddr, file_size, mem_size, align))
    if any(program[0] != 1 for program in programs):
        fail("ELF input may contain only PT_LOAD program headers")

    segments = []
    for _type, flags, file_off, vaddr, file_size, mem_size, align in programs:
        if mem_size == 0:
            continue
        if flags & ~7:
            fail("PT_LOAD has invalid permission bits")
        if not flags & 4:
            fail("PT_LOAD must be readable")
        if (flags & 3) == 3:
            fail("PT_LOAD has W+X permissions")
        if file_size > mem_size:
            fail("PT_LOAD file_size exceeds mem_size")
        if file_off + file_size < file_off or file_off + file_size > len(data):
            fail("PT_LOAD file range out of bounds")
        if vaddr % PAGE:
            fail("PT_LOAD RVA must be 4 KiB aligned")
        if align < PAGE or (align & (align - 1)) != 0:
            fail("PT_LOAD alignment must be at least 4 KiB")

        if file_size == 0:
            if not flags & 2:
                fail("zero-fill PT_LOAD must be writable")
            segment_type, segment_flags = 4, 3
        elif flags & 1:
            segment_type, segment_flags = 1, 5
        elif flags & 2:
            segment_type, segment_flags = 3, 3
        else:
            segment_type, segment_flags = 2, 1

        mem_size = (mem_size + PAGE - 1) & ~(PAGE - 1)
        segments.append({
            "type": segment_type,
            "flags": segment_flags,
            "source_offset": file_off,
            "file_size": file_size,
            "mem_size": mem_size,
            "rva": vaddr,
            "alignment": PAGE,
        })

    if not segments:
        fail("ELF has no loadable segments")
    if len(segments) > MAX_SEGMENTS:
        fail("too many loadable segments (max 32)")

    segments.sort(key=lambda segment: segment["rva"])
    for index in range(1, len(segments)):
        previous = segments[index - 1]
        if previous["rva"] + previous["mem_size"] > segments[index]["rva"]:
            fail("rounded PT_LOAD memory ranges overlap")

    sections = []
    for index in range(shnum):
        offset = shoff + index * shentsize
        (name, sh_type, flags, address, file_off, size, link, info, align,
         entsize) = struct.unpack_from("<IIQQQQIIQQ", data, offset)
        if sh_type != 8 and (file_off + size < file_off or file_off + size > len(data)):
            fail("ELF section out of bounds")
        sections.append({
            "name_offset": name, "type": sh_type, "flags": flags,
            "address": address, "offset": file_off, "size": size,
            "link": link, "info": info, "align": align, "entsize": entsize,
        })

    shstr = sections[shstrndx]
    if shstr["type"] != 3:
        fail("invalid ELF section-name string table")
    names = data[shstr["offset"]:shstr["offset"] + shstr["size"]]
    for section in sections:
        start = section["name_offset"]
        if start >= len(names):
            fail("ELF section name outside string table")
        end = names.find(b"\x00", start)
        if end < 0:
            fail("unterminated ELF section name")
        section["name"] = names[start:end].decode("utf-8", errors="replace")

    return entry, segments, sections


def read_metadata(data: bytes, sections: list[dict]) -> dict[str, list]:
    """Parse the .zxe.* JSON metadata sections."""
    metadata: dict[str, list] = {}
    for name in (".zxe.deps", ".zxe.imports", ".zxe.exports", ".zxe.relocs"):
        matching = [section for section in sections if section["name"] == name]
        if len(matching) > 1:
            fail(f"duplicate ELF metadata section {name}")
        if not matching:
            continue
        section = matching[0]
        if section["flags"] & 2:
            fail(f"metadata section {name} must be non-allocating")
        payload = data[section["offset"]:section["offset"] + section["size"]]
        if len(payload) > MAX_STRINGS:
            fail(f"metadata section {name} exceeds 1 MiB")
        try:
            parsed = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            fail(f"invalid JSON in {name}: {error}")
        if not isinstance(parsed, list):
            fail(f"{name} must contain a JSON array")
        metadata[name] = parsed
    return metadata


class Strings:
    """Deduplicating name table starting with the empty string."""

    def __init__(self) -> None:
        self.data = b"\x00"
        self.offsets = {b"": 0}

    def add(self, value: str) -> int:
        """Return the offset of value, appending it when new."""
        if not isinstance(value, str):
            fail("symbol/library name must be UTF-8")
        encoded = value.encode("utf-8")
        if not encoded or len(encoded) > 255:
            fail("name is empty or longer than 255 bytes")
        if b"\x00" in encoded:
            fail("name contains NUL")
        if encoded in self.offsets:
            return self.offsets[encoded]
        offset = len(self.data)
        self.data += encoded + b"\x00"
        self.offsets[encoded] = offset
        return offset


def main(argv: list[str]) -> int:
    if not (len(argv) == 3 or (len(argv) == 4 and argv[3] == "--shared")):
        fail("usage: zxe-pack.py input.elf output.zxe [--shared]")

    input_path, output_path = Path(argv[1]), Path(argv[2])
    shared = "--shared" in argv[1:]
    if not input_path.is_file():
        fail(f"missing input file: {input_path}")

    data = input_path.read_bytes()
    entry, segments, sections = parse_elf(data)

    image_size = max(segment["rva"] + segment["mem_size"] for segment in segments)
    if image_size > 1024 * 1024 * 1024:
        fail("image exceeds 1 GiB")
    if not shared:
        if not any(segment["type"] == 1 and segment["rva"] <= entry
                   < segment["rva"] + segment["mem_size"] for segment in segments):
            fail("entry point is outside executable PT_LOAD")

    metadata = read_metadata(data, sections)
    dependencies = metadata.get(".zxe.deps", [])
    imports = metadata.get(".zxe.imports", [])
    export_specs = metadata.get(".zxe.exports", [])
    reloc_specs = metadata.get(".zxe.relocs", [])

    if len(dependencies) > MAX_DEPS:
        fail("too many dependencies")
    if len(imports) > MAX_IMPORTS:
        fail("too many imports")
    if len(export_specs) > MAX_EXPORTS:
        fail("too many exports")
    if len(reloc_specs) > MAX_RELOCS:
        fail("too many relocations")

    strings = Strings()

    dependency_rows = []
    for item in dependencies:
        if not isinstance(item, dict):
            fail("dependency must be an object")
        name_offset = strings.add(item["name"])
        abi = int(item["abi_major"])
        if not 0 < abi <= 0xFFFF_FFFF:
            fail("dependency ABI major must be positive")
        dependency_rows.append(struct.pack("<IIII", name_offset, abi, 0, 0))
    dependency_table = b"".join(dependency_rows)

    import_rows = []
    for item in imports:
        if not isinstance(item, dict):
            fail("import must be an object")
        dependency_index = int(item["dependency_index"])
        if not 0 <= dependency_index < len(dependencies):
            fail("import dependency index out of bounds")
        abi = int(item["abi_major"])
        if abi != int(dependencies[dependency_index]["abi_major"]):
            fail("import ABI does not match its dependency")
        flags = 1 if item.get("weak") is True else 0
        import_rows.append(
            struct.pack("<IIII", dependency_index, strings.add(item["name"]), abi, flags)
        )
    import_table = b"".join(import_rows)

    # Rewrite ELF dynamic relocations into ZXE relocation records.
    for section in sections:
        if section["type"] != 4 or not section["flags"] & 2:
            continue
        if section["entsize"] != 24:
            fail("ELF RELA entry size must be 24")
        if section["size"] % 24:
            fail("ELF RELA section size is not a multiple of 24")
        if section["link"] >= len(sections):
            fail("ELF RELA symbol table index is invalid")
        symbol_table = sections[section["link"]]
        if symbol_table["type"] not in (2, 11) or symbol_table["entsize"] != 24:
            fail("ELF RELA link is not a symbol table")
        symbol_strings = sections[symbol_table["link"]]
        if symbol_strings["type"] != 3:
            fail("ELF symbol string table is invalid")
        symbol_names = data[symbol_strings["offset"]:
                            symbol_strings["offset"] + symbol_strings["size"]]

        for index in range(section["size"] // 24):
            offset = section["offset"] + index * 24
            target, info, addend = struct.unpack_from("<QQq", data, offset)
            elf_type = info & 0xFFFF_FFFF
            symbol_index = info >> 32
            type_name = ELF_RELOCATIONS.get(elf_type)
            if not type_name:
                fail(f"unsupported ELF dynamic relocation type {elf_type}")

            if type_name == "RELATIVE":
                if symbol_index:
                    fail("ELF RELATIVE relocation has nonzero symbol index")
                zxe_symbol_index = 0
            else:
                symbol_count = symbol_table["size"] // 24
                if symbol_index >= symbol_count:
                    fail("ELF relocation symbol index out of bounds")
                symbol_offset = symbol_table["offset"] + symbol_index * 24
                (name_offset,) = struct.unpack_from("<I", data, symbol_offset)
                if name_offset >= len(symbol_names):
                    fail("ELF relocation symbol name out of bounds")
                name_end = symbol_names.find(b"\x00", name_offset)
                if name_end < 0:
                    fail("ELF relocation symbol name is unterminated")
                symbol_name = symbol_names[name_offset:name_end]
                matches = [i for i, item in enumerate(imports)
                           if item["name"].encode("utf-8") == symbol_name]
                if len(matches) != 1:
                    fail(f"no unique ZXE import for ELF symbol "
                         f"{symbol_name.decode('utf-8', errors='replace')}")
                zxe_symbol_index = matches[0]

            reloc_specs.append({
                "target_rva": target,
                "type": type_name,
                "symbol_index": zxe_symbol_index,
                "addend": addend,
            })

    if len(reloc_specs) > MAX_RELOCS:
        fail("too many relocations")

    symbols: dict[bytes, tuple[int, int]] = {}
    for table in sections:
        if table["type"] != 2:
            continue
        if table["entsize"] != 24 or table["link"] >= len(sections):
            continue
        strtab = sections[table["link"]]
        if strtab["type"] != 3:
            continue
        names = data[strtab["offset"]:strtab["offset"] + strtab["size"]]
        for index in range(table["size"] // 24):
            offset = table["offset"] + index * 24
            name_offset, info, _other, shndx, value, size = \
                struct.unpack_from("<IBBHQQ", data, offset)
            if name_offset >= len(names) or shndx == 0:
                continue
            end_name = names.find(b"\x00", name_offset)
            if end_name < 0:
                continue
            name = names[name_offset:end_name]
            if not name or (info >> 4) != 1:
                continue
            if name in symbols:
                fail(f"duplicate global ELF symbol {name.decode(errors='replace')}")
            symbols[name] = (value, size)

    export_rows = []
    for item in export_specs:
        if not isinstance(item, dict):
            fail("export must be an object")
        abi = int(item["abi_major"])
        symbol_name = str(item["symbol"]).encode("utf-8")
        if item.get("global", True) is not True:
            fail("export must request GLOBAL visibility")
        symbol = symbols.get(symbol_name)
        if symbol is None:
            fail(f"exported ELF symbol not found: {item['symbol']}")
        value, symbol_size = symbol
        segment = next((segment for segment in segments
                        if segment["rva"] <= value
                        < segment["rva"] + segment["mem_size"]), None)
        if segment is None:
            fail(f"exported symbol outside PT_LOAD: {item['symbol']}")
        kind = item.get("type", "function")
        size = int(item.get("size", symbol_size))
        if kind != "function" and size <= 0:
            fail("non-function export requires nonzero size")
        if kind not in ("function", "object"):
            fail(f"unsupported export type {kind}")
        export_rows.append(
            struct.pack("<IIQII", strings.add(item["name"]), abi, value, size, 1)
        )
    export_table = b"".join(export_rows)

    relocation_rows = []
    for item in reloc_specs:
        if not isinstance(item, dict):
            fail("relocation must be an object")
        type_name = str(item["type"])
        reloc_type = RELOCATIONS.get(type_name)
        if not reloc_type:
            fail(f"unsupported ZXE relocation {type_name}")
        target = int(item["target_rva"])
        symbol_index = int(item.get("symbol_index", 0))
        addend = int(item.get("addend", 0))
        if reloc_type == 1 and symbol_index != 0:
            fail("RELATIVE relocation symbol_index must be zero")
        if reloc_type != 1 and not 0 <= symbol_index < len(imports):
            fail("non-RELATIVE relocation symbol index out of bounds")

        writable = any(segment["type"] in (3, 4)
                       and segment["rva"] <= target
                       and target + 8 <= segment["rva"] + segment["mem_size"]
                       for segment in segments)
        if target % 8 or not writable:
            fail("relocation target must be aligned and writable")
        if not -(1 << 63) <= addend < (1 << 63):
            fail("relocation addend out of signed 64-bit range")
        relocation_rows.append(struct.pack("<QIIq", target, reloc_type,
                                           symbol_index, addend))
    relocation_table = b"".join(relocation_rows)

    if len(strings.data) > MAX_STRINGS:
        fail("string table exceeds 1 MiB")

    segment_payloads = [data[segment["source_offset"]:
                             segment["source_offset"] + segment["file_size"]]
                        for segment in segments]

    # Placeholder segment table: the same size as the final one, so the
    # directory targets are computed against the real layout.
    table_blobs = [
        b"".join(struct.pack(
            "<IIQQQQQ", segment["type"], segment["flags"], 0,
            segment["file_size"], segment["mem_size"], segment["rva"],
            segment["alignment"],
        ) for segment in segments),
        dependency_table,
        import_table,
        export_table,
        relocation_table,
        strings.data,
    ]
    counts = [len(segments), len(dependencies), len(imports),
              len(export_specs), len(reloc_specs), len(strings.data)]

    directory = bytearray()
    cursor = HEADER_SIZE + DIRECTORY_SIZE * len(TABLES)
    for kind, _name, entry_size in TABLES:
        cursor = align8(cursor)
        directory += struct.pack("<IIQII", kind, 1, cursor,
                                 counts[kind - 1], entry_size)
        cursor += len(table_blobs[kind - 1])

    payload_cursor = align8(cursor)
    segment_file_offsets = []
    for segment in segments:
        payload_cursor = align8(payload_cursor)
        segment_file_offsets.append(payload_cursor)
        payload_cursor += segment["file_size"]
    file_size = payload_cursor
    if file_size > MAX_FILE:
        fail("ZXE output exceeds 256 MiB")

    segment_rows = []
    for segment, file_offset in zip(segments, segment_file_offsets):
        segment_rows.append(struct.pack(
            "<IIQQQQQ", segment["type"], segment["flags"], file_offset,
            segment["file_size"], segment["mem_size"], segment["rva"],
            segment["alignment"],
        ))
    table_blobs[0] = b"".join(segment_rows)

    out = bytearray()
    out += MAGIC
    out += struct.pack("<6H", VERSION, MACHINE_X86_64, HEADER_SIZE,
                       1 if shared else 0, DIRECTORY_SIZE, len(TABLES))
    out += struct.pack("<4Q", file_size, 0 if shared else entry,
                       image_size, HEADER_SIZE)
    out += hashlib.sha256(data).digest()[:16]
    out += struct.pack("<2I", 1, 0)
    out += b"\x00" * 56
    if len(out) != HEADER_SIZE:
        fail("internal header size mismatch")

    out += directory
    for index, (_kind, _name, _entry_size) in enumerate(TABLES):
        (target,) = struct.unpack_from(
            "<Q", bytes(directory), index * DIRECTORY_SIZE + 8)
        out += b"\x00" * (target - len(out))
        out += table_blobs[index]

    for payload, target in zip(segment_payloads, segment_file_offsets):
        out += b"\x00" * (target - len(out))
        out += payload

    if len(out) != file_size:
        fail(f"internal output size mismatch ({len(out)} != {file_size})")

    output_path.write_bytes(bytes(out))
    print(f"zxe: v1 {len(segments)} segments, {len(dependencies)} deps, "
          f"{len(imports)} imports, {len(export_specs)} exports, "
          f"{len(reloc_specs)} relocs, {file_size} bytes -> {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
