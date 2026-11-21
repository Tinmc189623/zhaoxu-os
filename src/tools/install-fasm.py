#!/usr/bin/env python3
"""Fetch the Ubuntu fasm package into this directory without root."""

import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(args: list[str]) -> bool:
    """Run a host tool, discarding its output."""
    return subprocess.run(
        args, cwd=HERE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode == 0


def main() -> int:
    if not run(["apt-get", "download", "fasm"]):
        raise SystemExit("apt-get download fasm failed")

    debs = sorted(HERE.glob("fasm_*.deb"))
    if not debs:
        raise SystemExit("no fasm_*.deb was downloaded")
    deb = debs[-1]

    if not run(["dpkg-deb", "-x", str(deb), str(HERE / "root")]):
        raise SystemExit("dpkg-deb extraction failed")

    extracted = HERE / "root" / "usr" / "bin" / "fasm"
    extracted.chmod(0o755)
    shutil.copy2(extracted, HERE / "fasm")
    shutil.rmtree(HERE / "root", ignore_errors=True)
    deb.unlink(missing_ok=True)

    version = subprocess.run(
        [str(HERE / "fasm")], capture_output=True, text=True, check=False
    )
    print(version.stdout.splitlines()[:2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
