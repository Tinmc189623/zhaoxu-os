#!/usr/bin/env python3
"""Convert the default wallpaper into a raw RGB888 frame for Aqua Desktop Environment.

usage: wallpaper-raw.py <source-image> <output.rgb> [width height]

The runtime has no image decoder; the build emits a fixed-size raw frame that
Aqua Desktop Environment scales with integer nearest-neighbour sampling.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

DISTRO = os.environ.get("WSL_DISTRO", "Ubuntu-26.04")
WINDOWS = os.name == "nt" or "cygwin" in sys.platform


def have_ffmpeg() -> bool:
    """True when ffmpeg answers on the host PATH."""
    try:
        subprocess.run(
            ["ffmpeg", "-version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        return shutil.which("ffmpeg") is not None
    except OSError:
        return False


def windows_to_wsl(path: Path) -> str:
    """Translate a Windows path to its WSL absolute form."""
    absolute = str(path.resolve()).replace("\\", "/")
    result = subprocess.run(
        ["wsl.exe", "-d", DISTRO, "--", "wslpath", "-a", absolute],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(f"wslpath failed for {path}")
    return result.stdout.strip()


def main(argv: list[str]) -> int:
    if not 3 <= len(argv) <= 5:
        raise SystemExit(
            "usage: wallpaper-raw.py source.jpg out.rgb [width height]"
        )

    source = Path(argv[1])
    output = Path(argv[2])
    width = int(argv[3]) if len(argv) > 3 else 1920
    height = int(argv[4]) if len(argv) > 4 else 1080

    if not source.is_file():
        raise SystemExit(f"missing {source}")
    if width <= 0 or height <= 0:
        raise SystemExit("width/height must be positive")

    output.parent.mkdir(parents=True, exist_ok=True)
    scale = f"scale={width}:{height}:flags=bilinear"

    if WINDOWS and not have_ffmpeg():
        command = [
            "wsl.exe", "-d", DISTRO, "--", "ffmpeg", "-y", "-loglevel", "error",
            "-i", windows_to_wsl(source), "-vf", scale,
            "-pix_fmt", "rgb24", "-f", "rawvideo", windows_to_wsl(output),
        ]
    else:
        command = [
            "ffmpeg", "-y", "-loglevel", "error", "-i", str(source),
            "-vf", scale, "-pix_fmt", "rgb24", "-f", "rawvideo", str(output),
        ]

    if subprocess.run(command, check=False).returncode != 0:
        raise SystemExit(f"ffmpeg failed for {source}")

    expected = width * height * 3
    actual = output.stat().st_size if output.is_file() else 0
    if actual != expected:
        raise SystemExit(f"wallpaper size {actual} != {expected}")
    print(f"wallpaper: {width}x{height} rgb24 ({actual} bytes) -> {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
