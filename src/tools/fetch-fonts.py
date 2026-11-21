#!/usr/bin/env python3
"""Fetch the system fonts into initramfs/data/.

MiSans / MiSans Global are distributed under Xiaomi's font licence, which
does not allow redistributing the font files, so they are downloaded at build
time and never committed.  Inter is OFL 1.1.

Run via `make fonts` from src/.
"""

import sys
import time
import urllib.request
import zipfile
from pathlib import Path

DEST = Path(__file__).resolve().parent.parent / "initramfs" / "data"

FONTS = [
    (
        "https://cdn.jsdelivr.net/gh/whjkd2005/MiSans-Fonts@main/MiSans-Regular.ttf",
        "MiSans-Regular.ttf",
    ),
    (
        "https://cdn.jsdelivr.net/gh/dsrkafuu/misans@main/raw/Latin/MiSansLatinVF.ttf",
        "MiSansLatinVF.ttf",
    ),
]

INTER_URL = "https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip"
INTER_ENTRY = "extras/otf/Inter-Regular.otf"


def download(url: str, path: Path, attempts: int = 3) -> None:
    """Download with retries; raises when every attempt fails."""
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(url, timeout=600) as response:
                path.write_bytes(response.read())
            return
        except OSError as error:
            if attempt == attempts:
                raise SystemExit(f"cannot download {url}: {error}") from error
            time.sleep(2 * attempt)


def fetch_font(url: str, name: str) -> None:
    """Skip files that are already present and non-empty."""
    target = DEST / name
    if target.is_file() and target.stat().st_size > 1024:
        return
    print(f"fetch {name}")
    download(url, target)
    if not target.is_file():
        raise SystemExit(f"cannot download {url}")


def main() -> int:
    DEST.mkdir(parents=True, exist_ok=True)

    for url, name in FONTS:
        fetch_font(url, name)

    inter = DEST / "Inter-Regular.otf"
    if not (inter.is_file() and inter.stat().st_size > 1024):
        archive = DEST / ".Inter.zip"
        fetch_font(INTER_URL, archive.name)
        try:
            with zipfile.ZipFile(archive) as bundle:
                inter.write_bytes(bundle.read(INTER_ENTRY))
        except (KeyError, zipfile.BadZipFile) as error:
            raise SystemExit(f"cannot extract Inter-Regular.otf: {error}") from error
        finally:
            archive.unlink(missing_ok=True)
        if inter.stat().st_size <= 1024:
            raise SystemExit("cannot extract Inter-Regular.otf")

    print("fonts ready")
    return 0


if __name__ == "__main__":
    sys.exit(main())
