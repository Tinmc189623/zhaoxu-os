#!/usr/bin/env python3
"""Render the A4 architecture document to PDF.

Source: docs/architecture/zhaoxu-os-architecture.html
Output: docs/architecture/zhaoxu-os-architecture.pdf

The layout rules live in the HTML (@page / CSS); this script only drives a
headless Chromium (Edge or Chrome) to print it, so the PDF matches what a
browser print preview shows.
"""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SOURCE = ROOT / "docs" / "architecture" / "zhaoxu-os-architecture.html"
TARGET = ROOT / "docs" / "architecture" / "zhaoxu-os-architecture.pdf"

BROWSERS = [
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
]


def find_browser() -> str | None:
    """First installed Chromium-family browser."""
    return next((path for path in BROWSERS if Path(path).is_file()), None)


def render(browser: str, source: Path, target: Path) -> bool:
    """Print the HTML to PDF with an isolated browser profile."""
    target.parent.mkdir(parents=True, exist_ok=True)
    target.unlink(missing_ok=True)

    with tempfile.TemporaryDirectory(prefix="arch-doc-") as profile:
        args = [
            "--headless=new", "--disable-gpu", "--no-first-run",
            "--no-default-browser-check", "--no-pdf-header-footer",
            "--print-to-pdf-no-header", f"--user-data-dir={profile}",
            f"--print-to-pdf={target}", source.as_uri(),
        ]
        subprocess.run([browser, *args], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)

    return target.is_file() and target.stat().st_size > 0


def page_count(pdf_path: Path) -> int | None:
    """Best-effort page count from the raw PDF; None when unreadable."""
    try:
        matches = re.findall(rb"/Count\s+(\d+)", pdf_path.read_bytes())
    except OSError:
        return None
    return max((int(value) for value in matches), default=None)


def main() -> int:
    if not SOURCE.is_file():
        raise SystemExit(f"missing source: {SOURCE}")

    browser = find_browser()
    if not browser:
        raise SystemExit("no Edge/Chrome found; install one of: " + ", ".join(BROWSERS))

    print(f"render : {browser}")
    print(f"source : {SOURCE}")
    if not render(browser, SOURCE, TARGET):
        raise SystemExit("render failed: no PDF produced")

    pages = page_count(TARGET)
    size = TARGET.stat().st_size
    suffix = f", {pages} pages" if pages else ""
    print(f"output : {TARGET} ({size} bytes{suffix})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
