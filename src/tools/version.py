#!/usr/bin/env python3
"""Sync version.cfg (repo root) into every place the product surfaces it.

Run after editing version.cfg:

    python3 tools/version.py        (from src/, or via `make version`)

version.cfg is the single source of truth.  Its schema is:

    name           = "Zhaoxu OS"   OS display name                 (required)
    name_cn        = null          optional Chinese OS name
    kernel         = "Yuanshuo"    kernel display name             (required)
    kernel_cn      = null          optional Chinese kernel name
    os_version     = 0.5.0         OS version: README, ISO         (required)
    os_5p_kernel_version = 0.5.0   hybrid kernel version: version.h (required;
                                   the older key `kernel_version` is accepted)
    yuanshuo_version = 0.5.0       microkernel version              (optional)
    build_date     = "2026-11-16"  build date, YYYY-MM-DD           (required)
    codename       = "Yuanshuo"    release codename                (required)

Values may be quoted strings, bare numbers, or null.  `#` starts a trailing
comment outside quotes.
"""

import datetime
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src"
CFG_PATH = ROOT / "version.cfg"


def fail(message: str) -> None:
    """Abort the sync with a clear message."""
    raise SystemExit(message)


def parse_cfg(path: Path) -> dict[str, str | None]:
    """Parse the simple key = value schema used by version.cfg."""
    values: dict[str, str | None] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, separator, rest = line.partition("=")
        if not separator:
            continue
        key = key.strip()
        value = rest.strip()
        if value.startswith('"'):
            end = value.find('"', 1)
            if end < 0:
                fail(f"{path}: unterminated string for {key}")
            value = value[1:end]
        else:
            value = re.sub(r"\s+#.*$", "", value).strip()
            if value in ("", "null"):
                value = None
        values[key] = value
    return values


def required(cfg: dict[str, str | None], key: str) -> str:
    """Fetch a mandatory version field."""
    value = cfg.get(key)
    if not value:
        fail(f"version.cfg: missing '{key}'")
    return value


def patch(path: Path, pairs: list[tuple[str, str]]) -> None:
    """Apply every (pattern, replacement) pair exactly once."""
    text = path.read_text(encoding="utf-8")
    for pattern, replacement in pairs:
        updated, count = re.subn(pattern, lambda _match: replacement, text,
                                 count=1, flags=re.MULTILINE)
        if not count:
            fail(f"{path}: pattern not found: {pattern}")
        text = updated
    path.write_text(text, encoding="utf-8")
    print(f"updated {path.relative_to(ROOT).as_posix()}")


def main() -> int:
    if not CFG_PATH.is_file():
        fail(f"missing {CFG_PATH}")

    cfg = parse_cfg(CFG_PATH)

    # The retired keys are rejected loudly instead of silently ignored.
    if "version" in cfg and not ("os_version" in cfg and
                                 ("os_5p_kernel_version" in cfg or
                                  "kernel_version" in cfg)):
        fail("version.cfg: use 'os_version' and 'os_5p_kernel_version', not "
             "'version'")
    if "build" in cfg and "build_date" not in cfg:
        fail("version.cfg: use 'build_date' (YYYY-MM-DD), not 'build'")

    os_version = required(cfg, "os_version")
    kernel_version = cfg.get("os_5p_kernel_version") or cfg.get("kernel_version")
    if not kernel_version:
        fail("version.cfg: missing 'os_5p_kernel_version'")
    build_date = required(cfg, "build_date")
    codename = required(cfg, "codename")
    kernel = required(cfg, "kernel")
    name = required(cfg, "name")
    name_cn = cfg.get("name_cn") or name

    for key, value in (("os_version", os_version),
                       ("os_5p_kernel_version", kernel_version)):
        if not re.fullmatch(r"\d+(\.\d+)+", value):
            fail(f"version.cfg: {key} '{value}' is not a dotted version")

    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", build_date):
        fail(f"version.cfg: build_date '{build_date}' is not YYYY-MM-DD")
    try:
        parsed = datetime.date.fromisoformat(build_date)
    except ValueError:
        fail(f"version.cfg: build_date '{build_date}' is not a valid date")
    if parsed.strftime("%Y-%m-%d") != build_date:
        fail(f"version.cfg: build_date '{build_date}' is not YYYY-MM-DD")

    patch(
        SRC / "kernel" / "include" / "version.h",
        [
            (r"/\* Synced from [^*]*\*/",
             "/* Synced from version.cfg by tools/version.py (make version). */"),
            (r'#define ZHAOXU_VERSION\s+"[^"]*"',
             f'#define ZHAOXU_VERSION         "{os_version}"'),
            (r'#define ZHAOXU_CODENAME\s+"[^"]*"',
             f'#define ZHAOXU_CODENAME        "{codename}"'),
            (r'#define ZHAOXU_KERNEL\s+"[^"]*"',
             f'#define ZHAOXU_KERNEL          "{kernel}"'),
            (r'#define ZHAOXU_KERNEL_VERSION\s+"[^"]*"',
             f'#define ZHAOXU_KERNEL_VERSION  "{kernel_version}"'),
            (r'#define ZHAOXU_BUILD\s+"[^"]*"',
             f'#define ZHAOXU_BUILD           "{build_date}"'),
        ],
    )

    # The shell banner (motd) intentionally carries no OS name or version:
    # rule 57 forbids showing them at boot, so it is not a version surface.
    patch(
        SRC / "initramfs" / "etc" / "version",
        [(rf"\A{re.escape(name)}[^\n]*",
          f'{name} {os_version} "{codename}" build {build_date}')],
    )

    patch(
        SRC / "initramfs" / "etc" / "os-release",
        [
            (r'^PRETTY_NAME=".*"$',
             f'PRETTY_NAME="{name} {os_version} 「{codename}」 (build {build_date})"'),
            (r'^VERSION=".*"$', f'VERSION="{os_version} 「{codename}」"'),
            (r"^VERSION_ID=.*$", f"VERSION_ID={os_version}"),
            (r"^VERSION_CODENAME=.*$", f"VERSION_CODENAME={codename}"),
            (r"^BUILD_ID=.*$", f"BUILD_ID={build_date}"),
            (r"^KERNEL_NAME=.*$", f"KERNEL_NAME={kernel}"),
            (r"^KERNEL_VERSION=.*$", f"KERNEL_VERSION={kernel_version}"),
        ],
    )

    patch(
        SRC / "initramfs" / "etc" / "initrd-release",
        [
            (r'^PRETTY_NAME=".*"$',
             f'PRETTY_NAME="{name} {os_version} 「{codename}」 initramfs '
             f'(build {build_date})"'),
            (r'^VERSION=".*"$', f'VERSION="{os_version} 「{codename}」"'),
            (r"^VERSION_ID=.*$", f"VERSION_ID={os_version}"),
            (r"^VERSION_CODENAME=.*$", f"VERSION_CODENAME={codename}"),
            (r"^BUILD_ID=.*$", f"BUILD_ID={build_date}"),
            (r"^KERNEL_NAME=.*$", f"KERNEL_NAME={kernel}"),
            (r"^KERNEL_VERSION=.*$", f"KERNEL_VERSION={kernel_version}"),
        ],
    )

    patch(
        SRC / "initramfs" / "etc" / "issue",
        [(r"^Zhaoxu OS [0-9.]+ 「[^」]*」 \\n \\l$",
          f"{name} {os_version} 「{codename}」 \\n \\l")],
    )

    patch(
        SRC / "initramfs" / "etc" / "issue.net",
        [(r"^Zhaoxu OS [0-9.]+ 「[^」]*」$",
          f"{name} {os_version} 「{codename}」")],
    )

    patch(
        ROOT / "README.md",
        [(r"\*\*Current version: [0-9.]+ 「[^」]*」, kernel [0-9.]+, build [0-9-]+\*\*",
          f"**Current version: {os_version} 「{codename}」, kernel {kernel_version}, "
          f"build {build_date}**")],
    )

    print(
        f'version.cfg -> {name} {os_version} "{codename}", '
        f"{kernel} kernel {kernel_version}, build {build_date} synchronized"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
