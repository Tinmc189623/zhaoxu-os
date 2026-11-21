#!/usr/bin/env python3
"""Detect the host platform and resolve the tools the Zhaoxu OS build needs.

Supported hosts (rule: the build must work on all four):

    linux    -- native Linux
    wsl      -- Linux inside WSL2 (reported separately so callers can
                translate Windows paths)
    macos    -- native macOS (Homebrew LLVM + QEMU + xorriso/mtools)
    windows  -- native Windows (MSYS2 / Git Bash toolchain)

The module never mutates the caller's environment; build.py and run.py build
the environment they need from the returned values.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

HOSTS = ("linux", "wsl", "macos", "windows")
WINDOWS_RUBY = re.compile(r"(mswin|mingw|cygwin)")

# MSYS2 and Git for Windows both put their POSIX sh here.
BASH_CANDIDATES = [
    "C:/msys64/usr/bin/bash.exe",
    "C:/msys2/usr/bin/bash.exe",
    "C:/Program Files/Git/bin/bash.exe",
    "C:/Program Files (x86)/Git/bin/bash.exe",
]

# edk2/OVMF firmware names, in the order that QEMU packages ship them.
OVMF_CODE_NAMES = ["OVMF_CODE_4M.fd", "OVMF_CODE.fd", "edk2-x86_64-code.fd"]
OVMF_VARS_NAMES = ["OVMF_VARS_4M.fd", "OVMF_VARS.fd", "edk2-i386-vars.fd"]


class ZosHostError(RuntimeError):
    """Raised when the host cannot be classified."""


def default_platform() -> str:
    """Ruby-style platform string for the running interpreter."""
    if os.name == "nt":
        return "windows"
    if sys.platform == "darwin":
        return "darwin"
    return "linux"


def detect(env: dict[str, str] | None = None,
           platform: str | None = None,
           proc_version: str | None = None) -> str:
    """Classify the host from the platform string plus the environment."""
    env = os.environ if env is None else env
    platform = default_platform() if platform is None else platform

    if WINDOWS_RUBY.search(platform) or platform in ("win32", "windows"):
        return "windows"
    if "darwin" in platform:
        return "macos"
    if "linux" not in platform:
        raise ZosHostError(f"unsupported platform: {platform}")

    if proc_version is None:
        try:
            proc_version = Path("/proc/version").read_text(encoding="utf-8")
        except OSError:
            proc_version = ""

    if (env.get("WSL_DISTRO_NAME") or env.get("WSL_INTEROP")
            or re.search("microsoft", proc_version, re.I)):
        return "wsl"
    return "linux"


def windows_ruby(platform: str | None = None) -> bool:
    """True for MSYS2/MinGW/Cygwin interpreters."""
    platform = default_platform() if platform is None else platform
    return bool(WINDOWS_RUBY.search(platform) or platform in ("win32", "windows"))


def native_windows(env: dict[str, str] | None = None,
                   platform: str | None = None) -> bool:
    """True when this process runs on Windows itself."""
    return windows_ruby(platform)


def find_executable(name: str, *extra_dirs: str,
                    env: dict[str, str] | None = None) -> str | None:
    """Search PATH and optional extra directories for a command."""
    if "/" in name or "\\" in name:
        return str(Path(name).expanduser().resolve())

    env = os.environ if env is None else env
    extensions = [""]
    if windows_ruby():
        pathext = env.get("PATHEXT", ".EXE;.CMD;.BAT;.COM")
        extensions += [ext.lower() for ext in pathext.split(";")]
        extensions += [".exe", ".cmd", ".bat"]
        extensions = list(dict.fromkeys(extensions))

    directories = [d for d in extra_dirs if d]
    directories += env.get("PATH", "").split(os.pathsep)

    for directory in directories:
        if not directory:
            continue
        for extension in extensions:
            candidate = Path(directory) / (name + extension)
            if candidate.is_file():
                return str(candidate.resolve())
    return None


def bash(env: dict[str, str] | None = None) -> str | None:
    """The bash.exe used to run POSIX recipes on native Windows."""
    env = os.environ if env is None else env
    found = find_executable("bash", env.get("ZOS_BASH") or "", env=env)
    if found:
        return found
    for candidate in BASH_CANDIDATES:
        if Path(candidate).is_file():
            return candidate
    return None


def msys2_root(env: dict[str, str] | None = None) -> str | None:
    """Locate the MSYS2 installation root."""
    env = os.environ if env is None else env
    for root in (env.get("MSYS2_ROOT"), "C:/msys64"):
        if not root:
            continue
        probe = Path(root.replace("\\", "/")) / "usr" / "bin" / "bash.exe"
        if probe.is_file():
            return root
    return None


def to_posix_path(path: str) -> str:
    """Convert C:\\a\\b to /c/a/b for MSYS2/Git-Bash and WSL."""
    text = str(path).replace("\\", "/")
    match = re.fullmatch(r"([A-Za-z]):/(.*)", text)
    if match:
        return f"/{match.group(1).lower()}/{match.group(2)}"
    return str(Path(text).expanduser().resolve())


def short_path(path: str) -> str:
    """8.3 short path for tools under directories with spaces (Windows)."""
    text = str(path)
    if " " not in text or not windows_ruby():
        return text

    try:
        result = subprocess.run(
            ["cmd", "/c", f'for %I in ("{text}") do @echo %~sI'],
            capture_output=True, text=True, check=False,
        )
    except OSError:
        return text
    output = result.stdout.strip()
    return output.splitlines()[0].strip() if output else text


def windows_path_dirs(env: dict[str, str] | None = None) -> list[str]:
    """Directories a native Windows build must prepend to PATH."""
    env = os.environ if env is None else env
    root = msys2_root(env=env)
    dirs = []
    if root:
        dirs.append(str(Path(root) / "usr" / "bin"))
        dirs.append(str(Path(root) / "mingw64" / "bin"))
    if env.get("ZOS_TOOL_DIR"):
        dirs.append(env["ZOS_TOOL_DIR"])
    return dirs


def macos_path_dirs(env: dict[str, str] | None = None) -> list[str]:
    """Homebrew keg-only LLVM/lld and prefix bin directories."""
    env = os.environ if env is None else env
    prefixes = [env.get("HOMEBREW_PREFIX"), "/opt/homebrew", "/usr/local"]
    dirs = []
    for prefix in prefixes:
        if not prefix:
            continue
        dirs += [
            str(Path(prefix) / "opt" / "llvm" / "bin"),
            str(Path(prefix) / "opt" / "lld" / "bin"),
            str(Path(prefix) / "bin"),
        ]
    return [directory for directory in dirs if Path(directory).is_dir()]


def ovmf_roots(host: str, env: dict[str, str] | None = None) -> list[str]:
    """Firmware search roots for one host."""
    env = os.environ if env is None else env
    if host == "macos":
        prefixes = [env.get("HOMEBREW_PREFIX"), "/opt/homebrew", "/usr/local"]
        dirs = [str(Path(prefix) / "share" / "qemu")
                for prefix in prefixes if prefix]
        return dirs + ["/usr/share/OVMF", "/usr/share/edk2/ovmf"]
    if host == "windows":
        root = msys2_root(env=env)
        dirs = []
        if root:
            base = Path(root.replace("\\", "/")) / "mingw64" / "share"
            dirs += [
                str(base / "qemu"),
                str(base / "edk2-ovmf" / "x64"),
                str(base / "edk2-ovmf"),
            ]
        return dirs + ["C:/Program Files/qemu/share", "C:/Program Files/qemu"]
    return ["/usr/share/OVMF", "/usr/share/edk2/ovmf", "/usr/share/qemu"]


def first_file(directories: list[str], names: list[str]) -> str | None:
    """First existing directory/name combination."""
    for directory in directories:
        for name in names:
            candidate = Path(directory) / name
            if candidate.is_file():
                return str(candidate)
    return None


def ovmf(host: str | None = None,
         env: dict[str, str] | None = None) -> tuple[str, str] | None:
    """First complete code/vars OVMF pair for the host."""
    host = detect(env=env) if host is None else host
    roots = ovmf_roots(host, env)
    code = first_file(roots, OVMF_CODE_NAMES)
    vars_ = first_file(roots, OVMF_VARS_NAMES)
    return (code, vars_) if code and vars_ else None


def shell_quote(value: object) -> str:
    """Quote a value for `bash -lc` using single quotes."""
    return "'" + str(value).replace("'", "'\"'\"'") + "'"


def make_assignment(name: str, value: object | None) -> str | None:
    """Build a VAR=value prefix for the make command line."""
    if value is None or str(value) == "":
        return None
    return f"{name}={shell_quote(value)}"
