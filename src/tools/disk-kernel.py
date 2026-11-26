#!/usr/bin/env python3
"""Refresh the kernel and Limine entry on the installed debug image.

usage: python3 src/tools/disk-kernel.py [image]
           [--kernel out/kernel.elf] [--font initramfs/data/MiSans-Regular.ttf]
           [--wallpaper build/ade/wallpaper.rgb] [--mtools-spec]

out/target.img is the one installed system under test: install it once, then
boot it directly after every kernel change.  This tool rewrites the EFI System
partition in place through mtools, using the same /boot/limine layout the
installer writes, so the debug image never drifts from a fresh install.
"""

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SECTOR = 512
ESP_TYPE = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b"


def gpt_guid(raw: bytes) -> str:
    """Format one 16-byte GPT GUID in textual order."""
    a, b, c = struct.unpack("<IHH", raw[:8])
    return f"{a:08x}-{b:04x}-{c:04x}-{raw[8:10].hex()}-{raw[10:16].hex()}"


def esp_location(image: Path) -> tuple[int, int]:
    """Return (byte offset, 1-based partition number) of the ESP.

    The partition number is what Limine's `boot(N)` prefix expects; empty GPT
    entries are skipped so the count matches the firmware's numbering.
    """
    with image.open("rb") as stream:
        mbr = stream.read(SECTOR)
        if len(mbr) < SECTOR:
            raise SystemExit(f"{image}: image is too small for a partition table")
        if mbr[446 + 4] != 0xEE:
            # Plain MBR: take the first FAT32 partition (type 0x0c/0x0b).
            for index in range(4):
                entry = mbr[446 + index * 16: 446 + (index + 1) * 16]
                if entry[4] in (0x0B, 0x0C):
                    return struct.unpack_from("<I", entry, 8)[0] * SECTOR, index + 1
            raise SystemExit(f"{image}: no FAT32 partition in the MBR")
        header = stream.read(SECTOR)
        if header[0:8] != b"EFI PART":
            raise SystemExit(f"{image}: protective MBR without a GPT header")
        table_lba = struct.unpack_from("<Q", header, 72)[0]
        count = struct.unpack_from("<I", header, 80)[0]
        size = struct.unpack_from("<I", header, 84)[0]
        stream.seek(table_lba * SECTOR)
        table = stream.read(count * size)
    number = 0
    for index in range(count):
        entry = table[index * size:(index + 1) * size]
        if len(entry) < 128 or entry[:16] == b"\x00" * 16:
            continue
        number += 1
        if gpt_guid(entry[:16]) == ESP_TYPE:
            return struct.unpack_from("<Q", entry, 32)[0] * SECTOR, number
    raise SystemExit(f"{image}: no EFI System partition in the GPT")


def limine_conf(partition_number: int) -> str:
    """The single installed entry: kernel, font and wallpaper under /boot/limine."""
    return (
        "timeout: 0\n"
        "serial: yes\n"
        "\n"
        "/Start\n"
        "    protocol: limine\n"
        f"    path: boot({partition_number}):/boot/limine/KERNEL.ELF\n"
        f"    module_path: boot({partition_number}):/boot/limine/MiSans.ttf\n"
        f"    module_path: boot({partition_number}):/boot/limine/WALL.RGB\n"
        "    resolution: 1920x1080x32\n"
    )


def run_mtools(tool: str, spec: str, *arguments: str) -> None:
    """Run one mtools command against the image spec.

    stdin is /dev/null so an unexpected confirmation prompt fails fast instead
    of blocking a headless build.
    """
    command = [tool, "-i", spec, *arguments]
    result = subprocess.run(command, stdin=subprocess.DEVNULL,
                            capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        raise SystemExit(f"{' '.join(command)} failed: {result.stderr.strip()}")


def main() -> int:
    """Parse arguments, write the kernel, assets and config onto the ESP."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", nargs="?", default="out/target.img")
    parser.add_argument("--kernel", default="out/kernel.elf")
    parser.add_argument("--font", default="initramfs/data/MiSans-Regular.ttf")
    parser.add_argument("--wallpaper", default="build/ade/wallpaper.rgb")
    parser.add_argument("--partition", type=int, default=0,
                        help="ESP partition number (default: discover it)")
    options = parser.parse_args()

    image = Path(options.image)
    if not image.is_file():
        raise SystemExit(f"{image}: no such image; run 'make run-install' once")
    offset, partition_number = esp_location(image)
    if options.partition > 0:
        partition_number = options.partition

    mcopy = shutil.which("mcopy")
    mmd = shutil.which("mmd")
    mdir = shutil.which("mdir")
    if not mcopy or not mmd or not mdir:
        raise SystemExit("mtools is required (mcopy, mmd, mdir); install the mtools package")

    spec = f"{image}@@{offset}"
    # mmd on an existing directory prompts on stdin and hangs when stdin is
    # not a terminal, so probe with mdir first.
    for directory in ("::/boot", "::/boot/limine"):
        probe = subprocess.run([mdir, "-i", spec, directory],
                               capture_output=True, text=True)
        if probe.returncode == 0:
            continue
        result = subprocess.run([mmd, "-i", spec, directory],
                                stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            raise SystemExit(f"mmd {directory} failed: {result.stderr.strip()}")

    for source, target in ((options.kernel, "::/boot/limine/KERNEL.ELF"),
                           (options.font, "::/boot/limine/MiSans.ttf"),
                           (options.wallpaper, "::/boot/limine/WALL.RGB")):
        path = Path(source)
        if not path.is_file():
            raise SystemExit(f"{path}: missing input file")
        run_mtools(mcopy, spec, "-o", str(path), target)

    with tempfile.NamedTemporaryFile("w", suffix=".conf", delete=False,
                                     encoding="utf-8") as handle:
        handle.write(limine_conf(partition_number))
        conf_path = handle.name
    run_mtools(mcopy, spec, "-o", conf_path, "::/limine.conf")
    run_mtools(mcopy, spec, "-o", conf_path, "::/EFI/BOOT/limine.conf")
    Path(conf_path).unlink()

    kernel_size = Path(options.kernel).stat().st_size
    print(f"disk-kernel: {image} ESP at byte {offset}, partition {partition_number}")
    print(f"disk-kernel: wrote /boot/limine/KERNEL.ELF ({kernel_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
