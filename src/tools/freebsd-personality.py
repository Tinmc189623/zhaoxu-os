#!/usr/bin/env python3
"""Fetch, verify and cross-build the FreeBSD 15.1 personality kernel input.

    python3 src/tools/freebsd-personality.py --fetch     # download + verify
    python3 src/tools/freebsd-personality.py --extract   # unpack to build/freebsd/src
    python3 src/tools/freebsd-personality.py --overlay   # apply the ZHAOXU overlay
    python3 src/tools/freebsd-personality.py --build     # cross-build buildkernel
    python3 src/tools/freebsd-personality.py --status    # report state only
    python3 src/tools/freebsd-personality.py             # fetch + extract + overlay + build

The upstream tree stays untouched (rule 39): it is unpacked under build/, the
overlay is copied in, and the repository never stores FreeBSD sources.
FREEBSD_SRC_URL can point at any mirror; the file is still checked against the
SHA-256 published in the 15.1-RELEASE MANIFEST.
"""

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

VERSION = "15.1-RELEASE"
SRC_URL = os.environ.get("FREEBSD_SRC_URL") or (
    f"https://download.freebsd.org/releases/amd64/amd64/{VERSION}/src.txz"
)
SRC_SHA256 = "cf5762da53fd52e1eaf0f9ceee9bf58cbe314c821031d0d9ffa76823185a89e1"

ROOT = Path(__file__).resolve().parent.parent          # src/
WORK = Path(os.environ.get("FREEBSD_WORK") or ROOT / "build" / "freebsd").expanduser()
ARCHIVE = WORK / f"src-{VERSION}.txz"
TREE = WORK / "src"
OBJ = WORK / "obj"
OVERLAY = ROOT / "kernel" / "personality" / "overlay"
KERNCONF = "ZHAOXU"


def source_root() -> Path | None:
    """Locate sys/amd64/conf/MINIMAL inside the unpacked archive."""
    for candidate in (TREE / "usr" / "src", TREE):
        if (candidate / "sys" / "amd64" / "conf" / "MINIMAL").is_file():
            return candidate
    return None


def sha256(path: Path) -> str | None:
    """SHA-256 of a file, or None when it does not exist."""
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch() -> bool:
    """Download the pinned source tarball and verify it."""
    WORK.mkdir(parents=True, exist_ok=True)

    if ARCHIVE.is_file():
        if sha256(ARCHIVE) == SRC_SHA256:
            print(f"fetch  : cached {ARCHIVE}")
            return True
        print("fetch  : checksum mismatch on cache, refetching")
        ARCHIVE.unlink()

    partial = ARCHIVE.with_suffix(ARCHIVE.suffix + ".part")
    partial.unlink(missing_ok=True)
    print(f"fetch  : {SRC_URL}")
    done = subprocess.run(
        ["curl", "--fail", "--location", "--retry", "3", "--output", str(partial), SRC_URL],
        check=False,
    ).returncode == 0

    if not done or not partial.is_file():
        print("fetch  : curl failed")
        return False

    actual = sha256(partial)
    if actual != SRC_SHA256:
        print(f"fetch  : SHA-256 mismatch\n  expected {SRC_SHA256}\n  actual   {actual}")
        partial.unlink(missing_ok=True)
        return False

    partial.replace(ARCHIVE)
    print(f"fetch  : verified {actual}")
    return True


def extract() -> bool:
    """Unpack the verified tarball into build/freebsd/src."""
    if sha256(ARCHIVE) != SRC_SHA256:
        print("extract: archive missing or unverified; run --fetch first")
        return False
    existing = source_root()
    if existing:
        print(f"extract: cached {existing}")
        return True

    shutil.rmtree(TREE, ignore_errors=True)
    TREE.mkdir(parents=True, exist_ok=True)
    if subprocess.run(["tar", "-xJf", str(ARCHIVE), "-C", str(TREE)],
                      check=False).returncode != 0 or not source_root():
        print("extract: tar failed or sys/amd64/conf/MINIMAL not found")
        return False
    print(f"extract: {source_root()}")
    return True


def apply_overlay() -> bool:
    """Copy the ZHAOXU kernel configuration over the unpacked tree."""
    root = source_root()
    if not root:
        print("overlay: source tree missing; run --extract first")
        return False
    if not OVERLAY.is_dir():
        print(f"overlay: {OVERLAY} missing")
        return False

    copied = 0
    for path in OVERLAY.rglob("*"):
        if not path.is_file():
            continue
        target = root / path.relative_to(OVERLAY)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        copied += 1

    if not (root / "sys" / "amd64" / "conf" / KERNCONF).is_file():
        print(f"overlay: {KERNCONF} config not present after copy")
        return False
    print(f"overlay: {copied} file(s), KERNCONF={KERNCONF}")
    return True


def tool_path(name: str) -> str | None:
    """Find an executable on PATH."""
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        candidate = Path(directory) / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def check_build_deps() -> bool:
    """clang/python3 are required; libarchive/bz2 headers are only a note."""
    missing = [name for name in ("clang", "python3") if not tool_path(name)]
    if missing:
        print(f"build  : missing {', '.join(missing)}")
        return False

    optional = [name for name, header in (("libarchive-dev", "/usr/include/archive.h"),
                                          ("libbz2-dev", "/usr/include/bzlib.h"))
                if not Path(header).is_file()]
    if optional:
        print(f"build  : note: {', '.join(optional)} not installed; "
              "buildworld tools would need them")
    return True


def build() -> bool:
    """Run kernel-toolchain then buildkernel through the upstream make.py."""
    root = source_root()
    if not root:
        print("build  : source tree missing; run --extract first")
        return False
    if not (root / "sys" / "amd64" / "conf" / KERNCONF).is_file():
        print(f"build  : {KERNCONF} config missing; run --overlay first")
        return False
    if not check_build_deps():
        return False

    maker = root / "tools" / "build" / "make.py"
    if not maker.is_file():
        print("build  : tools/build/make.py missing in the source tree")
        return False

    OBJ.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["MAKEOBJDIRPREFIX"] = str(OBJ)

    # Hosts without bzip2 can drop it into build/freebsd/hostbin/usr/bin.
    hostbin = WORK / "hostbin" / "usr" / "bin"
    if hostbin.is_dir():
        env["PATH"] = f"{hostbin}{os.pathsep}{env.get('PATH', '')}"

    clang = tool_path("clang")
    jobs = min(os.cpu_count() or 4, 8)

    for target in ("kernel-toolchain", "buildkernel"):
        args = [str(maker)]
        if clang:
            args += [f"--cross-bindir={Path(clang).parent}", "--cross-compiler-type=clang"]
        args += ["TARGET=amd64", "TARGET_ARCH=amd64", f"KERNCONF={KERNCONF}",
                 f"-j{jobs}", target]
        print(f"build  : {target} ({' '.join(args)})")
        if subprocess.run(["python3", *args], cwd=str(root), env=env).returncode != 0:
            return False
    return True


def status() -> None:
    """Print the fetch/extract/overlay state."""
    print(f"version: {VERSION}")
    print(f"archive: {ARCHIVE.stat().st_size} bytes" if ARCHIVE.is_file() else "archive: missing")
    print(f"sha256 : {sha256(ARCHIVE) or '-'}")
    print(f"pinned : {SRC_SHA256}")
    root = source_root()
    print(f"tree   : {root or 'missing'}")
    conf = root / "sys" / "amd64" / "conf" / KERNCONF if root else None
    print(f"kernconf: {'present' if conf and conf.is_file() else 'missing'}")
    print(f"obj    : {OBJ}")


def main(argv: list[str]) -> int:
    steps: list[str] = []
    for arg in argv[1:]:
        if arg in ("--fetch", "--extract", "--overlay", "--build", "--status"):
            steps.append(arg[2:])
        elif arg == "--clean":
            shutil.rmtree(WORK, ignore_errors=True)
            print(f"removed {WORK}")
            return 0
        else:
            raise SystemExit(f"unknown option {arg}")

    if not steps:
        steps = ["fetch", "extract", "overlay", "build"]

    for step in steps:
        ok = {"fetch": fetch, "extract": extract, "overlay": apply_overlay,
              "build": build}.get(step, lambda: (status(), True)[1])()
        if not ok:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
