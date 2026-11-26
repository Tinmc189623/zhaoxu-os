#!/usr/bin/env python3
"""Document the layout inside a Zhaoxu OS disk image.

usage: python3 src/tools/disk-doc.py out/target.img [--out docs/DISK_IMAGE.md]

Reads the image directly: MBR or GPT partition table, FAT32 directory tree of
the ESP, and the ext4 superblock summary of the root volume.  The Markdown it
writes is the on-disk layout, not the project tree.
"""

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 512

GPT_TYPE_NAMES = {
    "c12a7328-f81f-11d2-ba4b-00a0c93ec93b": "EFI System (FAT32)",
    "21686148-6449-6f6e-744e-656564454649": "BIOS boot",
    "0fc63daf-8483-4772-8e79-3d69d8477de4": "Linux data (ext4)",
    "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7": "Basic data (FAT32)",
    # Older installer builds wrote the second group transposed.
    "0fc63daf-8384-4772-8e79-3d69d8477de4": "Linux data (ext4, older build)",
}


def guid(raw: bytes) -> str:
    """Format one 16-byte GPT GUID in the usual textual order."""
    a, b, c = struct.unpack("<IHH", raw[:8])
    # The last two groups are stored big-endian, unlike the first three.
    return (f"{a:08x}-{b:04x}-{c:04x}-{raw[8:10].hex()}-"
            f"{raw[10:16].hex()}")


def read_at(stream, offset: int, size: int) -> bytes:
    """Read a byte range from the image."""
    stream.seek(offset)
    return stream.read(size)


def partitions(stream) -> list[dict]:
    """Return the partition list from GPT or MBR."""
    mbr = read_at(stream, 0, SECTOR)
    entries = []
    for index in range(4):
        entry = mbr[446 + index * 16: 446 + (index + 1) * 16]
        kind = entry[4]
        start = struct.unpack_from("<I", entry, 8)[0]
        count = struct.unpack_from("<I", entry, 12)[0]
        if kind and count:
            entries.append({"scheme": "MBR", "index": index + 1,
                            "type": f"0x{kind:02x}", "start": start,
                            "sectors": count, "name": ""})
    if not entries or entries[0]["type"] != "0xee":
        return entries

    # Protective MBR: parse the real GPT header at LBA 1.
    header = read_at(stream, SECTOR, SECTOR)
    if header[0:8] != b"EFI PART":
        return entries
    entry_lba = struct.unpack_from("<Q", header, 72)[0]
    entry_count = struct.unpack_from("<I", header, 80)[0]
    entry_size = struct.unpack_from("<I", header, 84)[0]
    table = read_at(stream, entry_lba * SECTOR, entry_count * entry_size)
    gpt = []
    for index in range(entry_count):
        raw = table[index * entry_size:(index + 1) * entry_size]
        if not raw or raw[:16] == b"\x00" * 16:
            continue
        start = struct.unpack_from("<Q", raw, 32)[0]
        end = struct.unpack_from("<Q", raw, 40)[0]
        type_guid = guid(raw[:16])
        name = raw[56:128].decode("utf-16le", "replace").split("\x00")[0]
        gpt.append({"scheme": "GPT", "index": index + 1,
                    "type": GPT_TYPE_NAMES.get(type_guid, type_guid),
                    "start": start, "sectors": end - start + 1,
                    "name": name})
    return gpt


def fat32_tree(stream, start_lba: int, sectors: int) -> list[str]:
    """List a FAT32 volume as text lines, or [] when it is not FAT32."""
    base = start_lba * SECTOR
    boot = read_at(stream, base, SECTOR)
    if boot[510:512] != b"\x55\xaa" or boot[82:87] != b"FAT32":
        return []
    bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
    sectors_per_cluster = boot[13]
    reserved = struct.unpack_from("<H", boot, 14)[0]
    fat_count = boot[16]
    fat_sectors = struct.unpack_from("<I", boot, 36)[0]
    root_cluster = struct.unpack_from("<I", boot, 44)[0]
    label = boot[71:82].decode("ascii", "replace").strip()
    cluster_bytes = bytes_per_sector * sectors_per_cluster
    data_start = base + (reserved + fat_count * fat_sectors) * bytes_per_sector
    fat_base = base + reserved * bytes_per_sector
    total_clusters = (sectors * bytes_per_sector) // cluster_bytes

    def next_cluster(cluster: int) -> int:
        entry = read_at(stream, fat_base + cluster * 4, 4)
        value = struct.unpack("<I", entry)[0] & 0x0FFFFFFF
        return value if value < 0x0FFFFFF8 and value < total_clusters else 0

    def cluster_data(cluster: int) -> bytes:
        return read_at(stream, data_start + (cluster - 2) * cluster_bytes,
                       cluster_bytes)

    entries: list[tuple[str, int]] = []
    pending = [(root_cluster, "")]
    while pending:
        cluster, prefix = pending.pop()
        long_name = ""
        while cluster:
            data = cluster_data(cluster)
            for offset in range(0, len(data), 32):
                entry = data[offset:offset + 32]
                if len(entry) < 32 or entry[0] == 0:
                    continue
                if entry[0] == 0xE5:
                    long_name = ""
                    continue
                if entry[11] == 0x0F:      # long-file-name slot
                    chunk = (entry[1:11] + entry[14:26] +
                             entry[28:32]).decode("utf-16le", "replace")
                    long_name = chunk.split("\x00")[0] + long_name
                    continue
                name = entry[0:8].decode("ascii", "replace").rstrip()
                ext = entry[8:11].decode("ascii", "replace").rstrip()
                shown = long_name or (f"{name}.{ext}" if ext else name)
                long_name = ""
                if name in (".", "..") or entry[11] & 0x08:
                    continue
                size = struct.unpack_from("<I", entry, 28)[0]
                first = struct.unpack_from("<H", entry, 26)[0]
                first |= struct.unpack_from("<H", entry, 20)[0] << 16
                if entry[11] & 0x10:
                    entries.append((prefix + shown + "/", 0))
                    if first:
                        pending.append((first, prefix + shown + "/"))
                else:
                    entries.append((prefix + shown, size))
            cluster = next_cluster(cluster)
    lines = [f"volume label: {label or '(none)'}"]
    for path, size in sorted(entries):
        lines.append(f"{path}  {size} bytes" if size else path)
    return lines


def ext4_summary(stream, start_lba: int) -> str:
    """Return a one-line ext4 summary, or '' when the volume is not ext4."""
    base = start_lba * SECTOR
    superblock = read_at(stream, base + 1024, 1024)
    if len(superblock) < 256 or struct.unpack_from("<H", superblock, 56)[0] != 0xEF53:
        return ""
    label = superblock[120:136].decode("ascii", "replace").strip("\x00 ")
    blocks = struct.unpack_from("<I", superblock, 4)[0]
    block_size = 1024 << struct.unpack_from("<I", superblock, 24)[0]
    return (f"ext4 label: {label or '(none)'}, {blocks} blocks of "
            f"{block_size} bytes")


def main() -> int:
    """Write the Markdown layout for one image."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("--out", default="docs/DISK_IMAGE.md")
    args = parser.parse_args()

    image = Path(args.image)
    if not image.is_file():
        raise SystemExit(f"missing image {image}")

    parts = partitions(image.open("rb"))
    lines = [f"# Disk image layout: `{image.name}`", "",
             f"Source image: `{image}` ({image.stat().st_size} bytes, "
             f"{image.stat().st_size // SECTOR} sectors).", "",
             "Generated by `src/tools/disk-doc.py` from the image itself.", ""]

    if not parts:
        lines += ["The image carries no partition table.", ""]
    else:
        scheme = parts[0]["scheme"]
        lines += [f"## Partition table ({scheme})", "",
                  "| # | Type | Start LBA | Sectors | Size | Name |",
                  "| --- | --- | --- | --- | --- | --- |"]
        with image.open("rb") as stream:
            for part in parts:
                size = part["sectors"] * SECTOR
                shown = (f"{size // (1024 * 1024)} MiB" if size >= 1 << 20
                         else f"{size // 1024} KiB")
                lines.append(f"| {part['index']} | {part['type']} | "
                             f"{part['start']} | {part['sectors']} | "
                             f"{shown} | {part['name'] or '-'} |")
        lines.append("")

        with image.open("rb") as stream:
            for part in parts:
                label = part["type"] + (f" ({part['name']})" if part["name"] else "")
                lines += [f"## Partition {part['index']}: {label}", ""]
                tree = fat32_tree(stream, part["start"], part["sectors"])
                if tree:
                    lines += ["```text"] + tree + ["```", ""]
                    continue
                summary = ext4_summary(stream, part["start"])
                if summary:
                    lines += [summary, ""]
                    continue
                lines += ["No FAT32 or ext4 signature on this partition.", ""]

    target = Path(args.out)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines), encoding="utf-8")
    print(f"disk-doc: wrote {target} ({len(lines)} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
