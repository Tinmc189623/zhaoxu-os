#!/usr/bin/env python3
"""Populate the ISO metadata files required by AGENTS.md.

usage: iso-manifest.py iso-root os-version codename kernel kernel-version build-date

Writes casper/{manifest,uuid,README.diskdefines}, .disk/{info,README,
casper-uuid-generic}, preseed/os.seed, README.html and autorun.inf, then an
md5sum.txt over every file in the ISO root.
"""

import hashlib
import sys
import uuid
from pathlib import Path


def write(path: Path, text: str, root: Path) -> None:
    """Write one metadata file, creating its directory."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    try:
        shown = path.relative_to(Path.cwd())
    except ValueError:
        shown = path
    print(f"iso: wrote {shown if shown != path else path.relative_to(root)}")


def main(argv: list[str]) -> int:
    if len(argv) != 7:
        raise SystemExit(
            "usage: iso-manifest.py iso-root os-version codename kernel "
            "kernel-version build-date"
        )

    root, version, codename, kernel, kernel_version, build_date = argv[1:]
    root_path = Path(root.replace("\\", "/").rstrip("/"))
    if not root_path.is_dir():
        raise SystemExit(f"missing ISO root {root_path}")

    image_uuid = str(uuid.uuid4())
    casper = root_path / "casper"
    disk = root_path / ".disk"
    for directory in (casper, disk, root_path / "preseed"):
        directory.mkdir(parents=True, exist_ok=True)

    write(casper / "uuid", f"{image_uuid}\n", root_path)
    write(
        casper / "manifest",
        f"""Zhaoxu OS {version} "{codename}" build {build_date}
kernel {kernel} {kernel_version}
file          .        <version>
file          bin      <directory>
file          etc      <directory>
file          lib      <directory>
file          usr      <directory>
""",
        root_path,
    )
    write(
        casper / "README.diskdefines",
        f"""#define DISKNAME  Zhaoxu OS {version} "{codename}"
#define KERNEL  {kernel} {kernel_version}
#define TYPE  binary
#define ARCH  amd64
#define ARCHSUFFIX  amd64
""",
        root_path,
    )

    write(
        disk / "info",
        f'Zhaoxu OS {version} "{codename}" build {build_date} - Release amd64 '
        f"({kernel} {kernel_version})\n",
        root_path,
    )
    write(disk / "README", "This disc is a Zhaoxu OS live/install image.\n", root_path)
    write(disk / "casper-uuid-generic", f"{image_uuid}\n", root_path)

    write(
        root_path / "preseed" / "os.seed",
        """# Zhaoxu OS unattended install defaults (ZhaoxuBootMgr / installer).
zhaoxu zhaoxu/language string zh_CN
zhaoxu zhaoxu/keyboard string us
zhaoxu zhaoxu/filesystem string ext4
zhaoxu zhaoxu/boot string gpt
zhaoxu zhaoxu/confirm boolean true
""",
        root_path,
    )

    write(
        root_path / "README.html",
        f"""<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>Zhaoxu OS {version}</title></head>
<body><h1>Zhaoxu OS {version} &laquo;{codename}&raquo;</h1>
<p>{kernel} {kernel_version} &middot; Aqua Desktop Environment &middot; Desktop Window Manager &middot; ZAF</p>
<ul><li>UEFI: EFI/BOOT/BOOTX64.EFI (Limine) &rarr; boot/limine/kernel.elf</li>
<li>BIOS: boot/limine/limine-bios-cd.bin (El Torito) &rarr; boot/limine/kernel.elf</li>
<li>Live root: casper/filesystem.squashfs</li></ul>
</body></html>
""",
        root_path,
    )

    write(
        root_path / "autorun.inf",
        "[autorun]\nicon=README.html\nlabel=Zhaoxu OS\n",
        root_path,
    )

    sums = []
    for path in sorted(root_path.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root_path).as_posix()
        if relative == "md5sum.txt":
            continue
        digest = hashlib.md5(path.read_bytes()).hexdigest()
        sums.append(f"{digest}  {relative}")

    write(root_path / "md5sum.txt", "\n".join(sums) + "\n", root_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
