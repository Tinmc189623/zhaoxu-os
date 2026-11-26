#!/usr/bin/env python3
"""Resolve and verify Fedora source RPMs for the Zhaoxu OS ports.

The owner directive (2026-11-26) sources btrfs from Fedora 45 and ports glibc,
gcc, gdb, binutils, llvm, lua and python from the same tree.  This tool reads
the release's signed repodata, finds each source package, downloads its SRPM,
checks the SHA-256 published in primary.xml, and records the exact provenance
in a ledger.  Nothing is guessed: an unresolved package is an error.

usage:
    python3 tools/fedora-fetch.py --list btrfs-progs
    python3 tools/fedora-fetch.py --package btrfs-progs glibc gcc
    python3 tools/fedora-fetch.py --release 45 --package btrfs-progs

Requires zstd on the host for primary.xml.zst (WSL2 Ubuntu ships it).
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import pathlib
import subprocess
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

# Measured 2026-11-26: mirrors.kernel.org throttled to 0 under load, while
# edge.kernel.org and Tsinghua served 0.7-2 MB/s.  Candidates are tried in
# order of reliability; FEDORA_MIRROR overrides the list.
MIRRORS = [
    os.environ.get("FEDORA_MIRROR",
                   "https://mirrors.edge.kernel.org/fedora/releases"),
    "https://mirrors.tuna.tsinghua.edu.cn/fedora/releases",
    "https://mirrors.kernel.org/fedora/releases",
]
COMMON_NS = "{http://linux.duke.edu/metadata/common}"
RPM_NS = "{http://linux.duke.edu/metadata/rpm}"
ROOT = pathlib.Path(__file__).resolve().parent.parent


def fetch(url: str, timeout: int = 120) -> bytes:
    """Download one URL; a failure names the URL."""
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.read()
    except (urllib.error.URLError, TimeoutError) as error:
        raise SystemExit(f"fetch failed: {url}: {error}") from error


def decompress_zstd(data: bytes) -> bytes:
    """Inflate a .zst payload through the host's zstd."""
    result = subprocess.run(["zstd", "-d", "-c"], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise SystemExit("zstd failed: " + result.stderr.decode(errors="replace"))
    return result.stdout


def tree_url(mirror: str, release: str) -> str:
    return f"{mirror}/{release}/Everything/source/tree"


def pick_mirror(release: str) -> str:
    """Return the first mirror whose repomd.xml downloads."""
    for mirror in MIRRORS:
        base = tree_url(mirror, release)
        try:
            fetch(f"{base}/repodata/repomd.xml", timeout=30)
            return base
        except SystemExit:
            print(f"mirror : {mirror} unavailable, trying next",
                  file=sys.stderr)
    raise SystemExit("no Fedora mirror answered")


def load_primary(release: str) -> tuple[ET.Element, str]:
    """Return the parsed primary metadata and the tree base URL."""
    base = pick_mirror(release)
    repomd = ET.fromstring(fetch(f"{base}/repodata/repomd.xml"))
    location = None
    checksum = None
    open_checksum = None
    for data in repomd.findall("{*}data"):
        if data.get("type") != "primary":
            continue
        found = data.find("{*}location")
        if found is not None:
            location = found.get("href")
        sum_tag = data.find("{*}checksum")
        if sum_tag is not None:
            checksum = sum_tag.text
        open_tag = data.find("{*}open-checksum")
        if open_tag is not None:
            open_checksum = open_tag.text
    if not location:
        raise SystemExit("repodata has no primary metadata")
    payload = fetch(f"{base}/{location}")
    actual = hashlib.sha256(payload).hexdigest()
    if checksum and actual != checksum:
        raise SystemExit(f"primary checksum mismatch: {actual} != {checksum}")
    raw = decompress_zstd(payload)
    if open_checksum and hashlib.sha256(raw).hexdigest() != open_checksum:
        raise SystemExit("primary open-checksum mismatch")
    return ET.fromstring(raw), base


def source_packages(root: ET.Element) -> list[dict[str, str]]:
    """Flatten primary.xml into source-package records."""
    packages = []
    for package in root.iter(f"{COMMON_NS}package"):
        arch = package.findtext(f"{COMMON_NS}arch")
        if arch != "src":
            continue
        name = package.findtext(f"{COMMON_NS}name") or ""
        version = package.find(f"{COMMON_NS}version")
        epoch = (version.get("epoch") or "0") if version is not None else "0"
        packages.append({
            "name": name,
            "epoch": epoch,
            "version": version.get("ver", "") if version is not None else "",
            "release": version.get("rel", "") if version is not None else "",
            "checksum": package.findtext(f"{COMMON_NS}checksum") or "",
            "location": package.find(f"{COMMON_NS}location").get("href")
                        if package.find(f"{COMMON_NS}location") is not None else "",
        })
    return packages


def resolve(packages: list[dict[str, str]], name: str) -> dict[str, str]:
    """Exact package-name match; a versioned name may carry a suffix."""
    matches = [p for p in packages if p["name"] == name]
    if not matches:
        matches = [p for p in packages if p["name"].startswith(name + ".")]
    if not matches:
        raise SystemExit(f"no source package named {name!r} in this release")
    matches.sort(key=lambda p: (p["version"], p["release"]))
    return matches[-1]


def ledger_path(release: str) -> pathlib.Path:
    return ROOT / "third_party" / "fedora" / release / "SOURCES.md"


def record(release: str, entry: str) -> None:
    """Append one provenance line unless the file already has it."""
    path = ledger_path(release)
    path.parent.mkdir(parents=True, exist_ok=True)
    existing = path.read_text(encoding="utf-8") if path.is_file() else (
        f"# Fedora {release} source ledger\n\n"
        "Generated by `tools/fedora-fetch.py`; every SRPM is verified against\n"
        "the SHA-256 published in the release's repodata before use.\n\n")
    if entry in existing:
        return
    path.write_text(existing + entry + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", default="45")
    parser.add_argument("--package", nargs="*", default=[])
    parser.add_argument("--list", dest="list_name")
    parser.add_argument("--dest", default=None,
                        help="directory for the SRPMs "
                             "(default third_party/fedora/<release>/SRPMS)")
    args = parser.parse_args()

    root, base = load_primary(args.release)
    packages = source_packages(root)
    print(f"fedora : release {args.release}, {len(packages)} source packages")

    if args.list_name:
        pattern = args.list_name
        for package in sorted(packages, key=lambda p: p["name"]):
            if pattern in package["name"]:
                print(f"{package['name']}-{package['version']}-"
                      f"{package['release']}")
        return 0

    if not args.package:
        parser.error("--package or --list is required")

    # Downloaded SRPM containers are build inputs, not vendored sources: they
    # live under build/ (git-ignored); only the provenance ledger is committed.
    destination = pathlib.Path(args.dest) if args.dest else (
        ROOT / "build" / "fedora" / args.release / "SRPMS")
    destination.mkdir(parents=True, exist_ok=True)

    for name in args.package:
        package = resolve(packages, name)
        filename = package["location"].rsplit("/", 1)[-1]
        url = f"{base}/{package['location']}"
        target = destination / filename
        partial = destination / (filename + ".part")
        if target.is_file():
            data = target.read_bytes()
        else:
            print(f"download: {filename}", flush=True)
            data = fetch(url, timeout=1800)
            # Only a verified body is promoted; an interrupted download
            # leaves a .part file the next run simply overwrites.
            partial.write_bytes(data)
            actual = hashlib.sha256(data).hexdigest()
            if actual != package["checksum"]:
                raise SystemExit(f"{filename}: sha256 {actual} != "
                                 f"{package['checksum']}")
            partial.rename(target)
        actual = hashlib.sha256(data).hexdigest()
        if actual != package["checksum"]:
            raise SystemExit(f"{filename}: sha256 {actual} != "
                             f"{package['checksum']}")
        print(f"verified: {filename} sha256 {actual}")
        record(args.release,
               f"- `{filename}` (`{package['name']} "
               f"{package['version']}-{package['release']}`) — "
               f"sha256 `{actual}` — {url}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
