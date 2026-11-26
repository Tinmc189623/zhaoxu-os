#!/usr/bin/env python3
"""Vendor selected FreeBSD 15.1 kernel sources for the OS_5P personality.

The files are taken verbatim from a pinned FreeBSD src commit (no edits, per
rule 39) through the GitHub mirror, and each one's SHA-256 is recorded in
third_party/freebsd/<release>/SOURCES.md so the tree can be re-created and
audited.

usage:
    python3 tools/freebsd-vendor.py --set kld
    python3 tools/freebsd-vendor.py --set scsi --set e1000
    python3 tools/freebsd-vendor.py --path sys/kern/kern_linker.c

The commit defaults to the tip of `releng/15.1` resolved with
`git ls-remote`; pass --commit to pin a different one.
"""

import argparse
import hashlib
import json
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BRANCH = "releng/15.1"
MIRROR = "https://raw.githubusercontent.com/freebsd/freebsd-src"

# Source subsets per porting slice (paths relative to the FreeBSD src root).
SETS = {
    "kld": [
        "sys/kern/kern_linker.c",
        "sys/kern/kern_module.c",
        "sys/kern/link_elf.c",
        "sys/kern/link_elf_obj.c",
        "sys/sys/module.h",
        "sys/sys/linker.h",
        "sys/sys/linker_set.h",
    ],
    "scsi": [
        "sys/cam/cam.c",
        "sys/cam/cam_periph.c",
        "sys/cam/cam_sim.c",
        "sys/cam/cam_xpt.c",
        "sys/cam/scsi/scsi_da.c",
        "sys/cam/scsi/scsi_pass.c",
        "sys/dev/sym/sym_hipd.c",
        "sys/dev/sym/sym_hipd.h",
        "sys/dev/sym/sym_conf.h",
    ],
    "e1000": [
        "sys/dev/e1000/if_em.c",
        "sys/dev/e1000/if_em.h",
        "sys/dev/e1000/e1000_api.c",
        "sys/dev/e1000/e1000_mac.c",
        "sys/dev/e1000/e1000_nvm.c",
        "sys/dev/e1000/e1000_phy.c",
    ],
    "xhci": [
        "sys/dev/usb/controller/xhci.c",
        "sys/dev/usb/controller/xhci.h",
        "sys/dev/usb/controller/xhci_pci.c",
        "sys/dev/usb/usb_device.c",
        "sys/dev/usb/usb_transfer.c",
        "sys/dev/usb/usb_busdma.c",
        "sys/dev/usb/storage/umass.c",
    ],
}


def resolve_commit() -> str:
    """Resolve the branch tip to a full commit hash.

    git smart-protocol access is blocked on some networks, so the GitHub REST
    API is preferred; `git ls-remote` remains as a fallback.
    """
    api = ("https://api.github.com/repos/freebsd/freebsd-src/branches/"
           + BRANCH)
    try:
        request = urllib.request.Request(api, headers={"User-Agent": "zhaoxu-vendor"})
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = json.loads(response.read())
        commit = payload["commit"]["sha"]
        print(f"resolved {BRANCH} -> {commit} (GitHub API)")
        return commit
    except (OSError, KeyError, ValueError) as error:
        print(f"warning: {api}: {error}")

    try:
        result = subprocess.run(
            ["git", "ls-remote", "https://github.com/freebsd/freebsd-src.git",
             BRANCH],
            capture_output=True, text=True, timeout=30, check=False)
    except subprocess.TimeoutExpired:
        result = None
    if result and result.returncode == 0 and result.stdout.strip():
        commit = result.stdout.split()[0]
        print(f"resolved {BRANCH} -> {commit} (git ls-remote)")
        return commit
    raise SystemExit(f"cannot resolve {BRANCH}")


def fetch(commit: str, path: str, target: Path) -> str:
    """Download one file verbatim and return its SHA-256.

    The GitHub raw host occasionally times out on the TLS handshake, so the
    FreeBSD cgit instance (same repository, pinned by commit id) is the
    fallback.
    """
    urls = [
        f"{MIRROR}/{commit}/{path}",
        f"https://cgit.freebsd.org/src/plain/{path}?id={commit}",
    ]
    print(f"fetching {path}")
    data = None
    for url in urls:
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read()
            break
        except OSError as error:
            print(f"warning: {url}: {error}")
    if data is None:
        raise SystemExit(f"cannot fetch {path} from any mirror")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    return hashlib.sha256(data).hexdigest()


def write_ledger(output: Path, release: str, commit: str,
                 entries: list[tuple[str, str]]) -> None:
    """Record the pinned commit and every vendored file's hash."""
    ledger = output / "SOURCES.md"
    lines = [
        f"# FreeBSD {release} sources vendored for OS_5P",
        "",
        f"Upstream: `https://git.FreeBSD.org/src.git`, branch `{BRANCH}`, "
        f"commit `{commit}`.",
        "Files are copied verbatim (rule 39); this table is the provenance "
        "record.",
        "",
        "| File | SHA-256 |",
        "| --- | --- |",
    ]
    for path, digest in sorted(entries):
        lines.append(f"| `{path}` | `{digest}` |")
    ledger.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"ledger: {ledger}")


def main() -> int:
    """Parse arguments and vendor the requested files."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release", default="15.1")
    parser.add_argument("--commit", default=None)
    parser.add_argument("--set", action="append", default=[], choices=sorted(SETS),
                        help="vendor one named subset (repeatable)")
    parser.add_argument("--path", action="append", default=[],
                        help="vendor one extra source path (repeatable)")
    parser.add_argument("--output", default=None)
    options = parser.parse_args()

    paths = list(options.path)
    for name in options.set:
        paths.extend(SETS[name])
    if not paths:
        parser.error("choose at least one --set or --path")

    commit = options.commit or resolve_commit()
    output = Path(options.output) if options.output else \
        ROOT / "third_party" / "freebsd" / options.release
    entries = []
    for path in paths:
        entries.append((path, fetch(commit, path, output / path)))
    write_ledger(output, options.release, commit, entries)
    print(f"vendored {len(entries)} files into {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
