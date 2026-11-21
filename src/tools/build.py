#!/usr/bin/env python3
"""Cross-platform build entry point for Zhaoxu OS.

The build itself is the GNU Makefile in this directory; this script detects
the host (native Linux, macOS, Windows via MSYS2, or WSL2) and runs the same
`make` with the right tool paths and environment:

    python3 tools/build.py                     # build everything (make all)
    python3 tools/build.py iso                 # any make target / variable
    python3 tools/build.py --jobs 8 run-iso
    python3 tools/build.py --check             # report tool status
    python3 tools/build.py --host wsl          # Windows: force the WSL path
    python3 tools/build.py --host native       # Windows: force MSYS2/MinGW

Linux/macOS run make directly.  Windows prefers a registered WSL2 distro
(the Makefile runs inside it); native MSYS2/Git Bash is only the fallback
when the distro lacks make/cc/clang/ld/objcopy.
"""

import os
import posixpath
import re
import subprocess
import sys
from pathlib import Path

import host
import native

SRC = Path(__file__).resolve().parent.parent.as_posix()

# Logical build tools and the commands that satisfy them, per host.  The
# first existing candidate wins.
TOOL_CANDIDATES = {
    "linux": {
        "make": ["make"], "python": ["python3", "python"],
        "cc": ["gcc", "cc"], "cxx": ["g++", "c++"],
        "gcc-ada": ["gcc-14", "gcc-15", "gcc-16", "gnatgcc", "x86_64-elf-gcc"],
        "ld": ["ld.lld", "lld"], "objcopy": ["objcopy", "llvm-objcopy"],
        "clang": ["clang"], "lld-link": ["lld-link"],
        "fasm": ["tools/fasm", "fasm"], "tar": ["tar"],
        "mkfs.vfat": ["mkfs.vfat", "mkfs.fat"], "mmd": ["mmd"],
        "mcopy": ["mcopy"], "mksquashfs": ["mksquashfs"],
        "xorriso": ["xorriso"], "qemu-system-x86_64": ["qemu-system-x86_64"],
    },
    "wsl": {},
    "macos": {
        "make": ["make"], "python": ["python3", "python"],
        "cc": ["clang", "cc"], "cxx": ["clang++", "c++"],
        "gcc-ada": ["x86_64-elf-gcc", "gnatgcc", "gnat"],
        "ld": ["ld.lld", "lld"], "objcopy": ["llvm-objcopy", "objcopy"],
        "clang": ["clang"], "lld-link": ["lld-link"],
        "fasm": ["fasm", "tools/fasm"], "tar": ["tar"],
        "mkfs.vfat": ["mkfs.vfat", "mkfs.fat"], "mmd": ["mmd"],
        "mcopy": ["mcopy"], "mksquashfs": ["mksquashfs"],
        "xorriso": ["xorriso"], "qemu-system-x86_64": ["qemu-system-x86_64"],
    },
    "windows": {
        "make": ["make"], "python": ["python", "python3"],
        "cc": ["clang", "cc", "gcc"], "cxx": ["clang++", "c++", "g++"],
        "gcc-ada": ["x86_64-elf-gcc", "gnatgcc"],
        "ld": ["ld.lld", "lld"], "objcopy": ["llvm-objcopy", "objcopy"],
        "clang": ["clang"], "lld-link": ["lld-link"],
        "fasm": ["fasm.exe", "fasm", "tools/fasm.exe", "tools/fasm"],
        "tar": ["tar"], "mkfs.vfat": ["mkfs.vfat", "mkfs.fat"],
        "mmd": ["mmd"], "mcopy": ["mcopy"], "mksquashfs": ["mksquashfs"],
        "xorriso": ["xorriso"], "qemu-system-x86_64": ["qemu-system-x86_64"],
    },
}

SCOPE_GROUPS = {
    "base": ["make", "python"],
    "kernel": ["cc", "cxx", "ld", "objcopy", "clang", "lld-link", "fasm", "tar"],
    "ada": ["gcc-ada"],
    "iso": ["mkfs.vfat", "mmd", "mcopy", "mksquashfs", "xorriso"],
    "run": ["qemu-system-x86_64"],
}

INSTALL_HINTS = {
    "linux": {
        "make": "apt install make", "python": "apt install python3",
        "cc": "apt install gcc", "cxx": "apt install g++",
        "gcc-ada": "apt install gnat-14 (or the gnat matching gcc)",
        "ld": "apt install lld", "objcopy": "apt install binutils",
        "clang": "apt install clang", "lld-link": "apt install lld",
        "fasm": "repository ships tools/fasm", "tar": "apt install tar",
        "mkfs.vfat": "apt install dosfstools", "mmd": "apt install mtools",
        "mcopy": "apt install mtools", "mksquashfs": "apt install squashfs-tools",
        "xorriso": "apt install xorriso",
        "qemu-system-x86_64": "apt install qemu-system-x86",
    },
    "macos": {
        "make": "xcode-select --install", "python": "brew install python",
        "cc": "xcode-select --install", "cxx": "xcode-select --install",
        "gcc-ada": "AdaCore GNAT, then ADA=...", "ld": "brew install lld",
        "objcopy": "brew install llvm", "clang": "brew install llvm",
        "lld-link": "brew install lld", "fasm": "FASM=... (macOS build)",
        "tar": "system tar", "mkfs.vfat": "brew install dosfstools",
        "mmd": "brew install mtools", "mcopy": "brew install mtools",
        "mksquashfs": "brew install squashfs", "xorriso": "brew install xorriso",
        "qemu-system-x86_64": "brew install qemu",
    },
    "windows": {
        "make": "pacman -S make (MSYS2)",
        "python": "winget install Python.Python.3.12",
        "cc": "pacman -S mingw-w64-x86_64-clang",
        "cxx": "pacman -S mingw-w64-x86_64-clang",
        "gcc-ada": "x86_64-elf GNAT, then ADA=...",
        "ld": "pacman -S mingw-w64-x86_64-lld",
        "objcopy": "pacman -S mingw-w64-x86_64-llvm",
        "clang": "pacman -S mingw-w64-x86_64-clang",
        "lld-link": "pacman -S mingw-w64-x86_64-lld",
        "fasm": "winget install fasm.fasm", "tar": "pacman -S tar",
        "mkfs.vfat": "pacman -S mingw-w64-x86_64-dosfstools",
        "mmd": "pacman -S mingw-w64-x86_64-mtools",
        "mcopy": "pacman -S mingw-w64-x86_64-mtools",
        "mksquashfs": "pacman -S squashfs-tools", "xorriso": "pacman -S xorriso",
        "qemu-system-x86_64": "pacman -S mingw-w64-x86_64-qemu",
    },
}


def take(argv):
    """Pop the next argument, or None when the list is empty."""
    return argv.pop(0) if argv else None


def main(argv):
    """CLI entry point; returns the process exit code."""
    try:
        options = parse_options(list(argv))
        if options is None:
            return 0

        host_name = check_host(options) if options["check"] else resolve_host(options)
        if options["check"]:
            return check(host_name, options)
        return run_build(host_name, options)
    except host.ZosHostError as error:
        print(f"build: {error}", file=sys.stderr)
        return 1


def check_host(options):
    """Host selected for --check; needs no usable toolchain."""
    detected = host.detect()
    requested = options["host"] or ""
    if requested in ("auto", ""):
        return detected
    if requested == "native":
        return "windows" if detected == "windows" else detected
    if requested == "wsl":
        return "wsl"
    if requested in ("linux", "macos", "windows"):
        return requested
    raise host.ZosHostError(f"unknown --host {requested}")


def parse_options(argv):
    """Parse the build CLI; returns the options dict or None for --help."""
    options = {"jobs": None, "headless": None, "host": "auto", "distro": None,
               "check": None, "args": []}
    while argv:
        argument = argv.pop(0)
        if argument in ("--jobs", "-j"):
            options["jobs"] = int(take(argv))
        elif re.fullmatch(r"-j(\d+)", argument):
            options["jobs"] = int(re.fullmatch(r"-j(\d+)", argument).group(1))
        elif argument == "--headless":
            options["headless"] = 1
        elif argument == "--gui":
            options["headless"] = 0
        elif argument == "--host":
            options["host"] = take(argv)
        elif re.fullmatch(r"--host=(.+)", argument):
            options["host"] = re.fullmatch(r"--host=(.+)", argument).group(1)
        elif argument == "--distro":
            options["distro"] = take(argv)
        elif re.fullmatch(r"--distro=(.+)", argument):
            options["distro"] = re.fullmatch(r"--distro=(.+)", argument).group(1)
        elif argument == "--check":
            options["check"] = "build"
        elif re.fullmatch(r"--check=(.+)", argument):
            options["check"] = re.fullmatch(r"--check=(.+)", argument).group(1)
        elif argument in ("--help", "-h"):
            print(help_text())
            return None
        else:
            options["args"] = [argument] + argv
            break
    return options


def help_text():
    """Usage text for --help."""
    return """usage: python3 tools/build.py [options] [make target/variables...]

  --jobs N, -jN      parallel make jobs
  --headless         build without GOP graphics (HEADLESS=1)
  --gui              build with GOP graphics (HEADLESS=0)
  --host auto|native|wsl|linux|macos|windows
  --distro NAME      WSL distro (default: WSL_DISTRO_NAME or Ubuntu-26.04)
  --check[=scope]    report tool status; scope: base|kernel|ada|iso|run|all
  --help

examples:
  python3 tools/build.py
  python3 tools/build.py iso
  python3 tools/build.py --host wsl run-iso
"""


def resolve_host(options):
    """Host that will actually run make, or raise when none is usable."""
    detected = host.detect()
    requested = options["host"] or ""
    selected = None
    if requested in ("auto", ""):
        if detected == "windows":
            # Windows prefers WSL2; native MSYS2 is only the fallback.
            if wsl_usable(options["distro"]):
                selected = "wsl"
            elif native_windows_usable():
                selected = "windows"
            elif native_windows_build():
                selected = "windows"
            elif wsl_available():
                selected = "wsl"
        else:
            selected = detected
    elif requested == "native":
        if detected == "windows":
            if native_windows_usable() or native_windows_build():
                selected = "windows"
            else:
                raise host.ZosHostError(
                    "no native Windows toolchain found (run --check)")
        else:
            selected = detected
    elif requested == "wsl":
        if detected in ("wsl", "windows"):
            selected = "wsl"
        else:
            raise host.ZosHostError("WSL is only available from Windows")
    elif requested in ("linux", "macos", "windows"):
        selected = requested
    else:
        raise host.ZosHostError(f"unknown --host {requested}")
    if selected is None:
        raise host.ZosHostError("no usable build host; run --check")
    return selected


def native_windows_usable():
    """True when MSYS2/Git Bash carries GNU make."""
    bash_exe = host.bash()
    if not bash_exe:
        return False
    return host.find_executable("make", *windows_tool_dirs(bash_exe)) is not None


def native_windows_build():
    """True when the no-make native Windows driver can build."""
    return native.available()


def windows_make_available():
    """True when make exists on the POSIX shell PATH on native Windows."""
    bash_exe = host.bash()
    if not bash_exe:
        return False
    output = decode_output(capture(bash_exe, "-lc", "command -v make 2>/dev/null"))
    return bool(output.strip())


def wsl_available():
    """True when at least one WSL distro is registered."""
    return host.native_windows() and bool(wsl_distros())


def wsl_usable(distro=None):
    """True when a registered distro can drive the Makefile."""
    if not wsl_available():
        return False

    lookup = probe_in_wsl(distro)
    candidates = TOOL_CANDIDATES["linux"]
    # The interpreter is not part of this probe: make runs inside the distro
    # and resolves its own python3.
    return all(
        any(lookup.get(name) for name in candidates[tool])
        for tool in ("make", "cc", "clang", "ld", "objcopy")
    )


def wsl_ovmf(distro=None):
    """OVMF code/vars pair inside the distro, as a two-element list."""
    if not wsl_available():
        return None

    # wsl.exe strips shell variables, so test the candidate paths explicitly.
    script = ("if [ -f /usr/share/OVMF/OVMF_CODE_4M.fd ]; then "
              "echo /usr/share/OVMF/OVMF_CODE_4M.fd; "
              "elif [ -f /usr/share/OVMF/OVMF_CODE.fd ]; then "
              "echo /usr/share/OVMF/OVMF_CODE.fd; "
              "elif [ -f /usr/share/edk2/ovmf/OVMF_CODE.fd ]; then "
              "echo /usr/share/edk2/ovmf/OVMF_CODE.fd; fi; "
              "if [ -f /usr/share/OVMF/OVMF_VARS_4M.fd ]; then "
              "echo /usr/share/OVMF/OVMF_VARS_4M.fd; "
              "elif [ -f /usr/share/OVMF/OVMF_VARS.fd ]; then "
              "echo /usr/share/OVMF/OVMF_VARS.fd; "
              "elif [ -f /usr/share/edk2/ovmf/OVMF_VARS.fd ]; then "
              "echo /usr/share/edk2/ovmf/OVMF_VARS.fd; fi")
    output = decode_output(capture(wsl_executable(), "-d", wsl_distro(distro),
                                   "--", "bash", "-lc", script))
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    return lines if len(lines) == 2 else None


def wsl_distros():
    """Registered WSL distro names; empty when WSL is missing."""
    try:
        output = decode_output(capture(wsl_executable(), "-l", "-q"))
        return [line.strip() for line in re.split(r"\r?\n", output) if line.strip()]
    except OSError:
        return []


def decode_output(data):
    """Decode captured command output, including wsl.exe's UTF-16LE."""
    if not data:
        return ""

    if data[:2] == b"\xff\xfe" or b"\x00" in data:
        return data.decode("utf-16-le", errors="replace")
    return data.decode("utf-8", errors="replace")


def wsl_distro(distro=None):
    """WSL distro to use: explicit, environment, first registered, default."""
    if distro:
        return distro
    return (os.environ.get("WSL_DISTRO_NAME")
            or (wsl_distros() or [None])[0]
            or "Ubuntu-26.04")


def windows_tool_dirs(bash_exe=None):
    """Extra tool directories for a native Windows build."""
    bash_exe = host.bash() if bash_exe is None else bash_exe
    root = host.msys2_root()
    dirs = []
    if root:
        base = root.replace("\\", "/")
        dirs.append(f"{base}/mingw64/bin")
        dirs.append(f"{base}/usr/bin")
    elif bash_exe:
        # Git for Windows keeps its POSIX tools under <root>/usr/bin.
        git_root = str(Path(bash_exe).parent.parent).replace("\\", "/")
        dirs.append(f"{git_root}/usr/bin")
        dirs.append(str(Path(bash_exe).parent))
    dirs.append(posixpath.join(SRC, "tools"))
    return dirs


def make_arguments(options):
    """Make command-line arguments derived from the CLI options."""
    arguments = []
    if options["jobs"] is not None:
        arguments.append(f"-j{options['jobs']}")
    if options["headless"] is not None:
        arguments.append(f"HEADLESS={options['headless']}")
    return arguments + options["args"]


def run_build(host_name, options):
    """Run the build for the resolved host; returns the exit code."""
    arguments = make_arguments(options)
    if host_name in ("linux", "macos"):
        ok = run_native(host_name, arguments)
    elif host_name == "windows":
        ok = run_windows(arguments) if windows_make_available() else run_native_windows(arguments)
    elif host_name == "wsl":
        ok = (run_wsl(arguments, options["distro"]) if host.native_windows()
              else run_native("linux", arguments))
    else:
        raise host.ZosHostError(f"unsupported host: {host_name}")
    return 0 if ok else 1


def run_native_windows(arguments):
    """Build through native.py when no GNU make is available."""
    if any("=" in arg for arg in arguments):
        kept = " ".join(arg for arg in arguments if "=" not in arg)
        print("build: the native no-make driver ignores make variables: "
              f"{kept}", file=sys.stderr)
    target = next((arg for arg in arguments if "=" not in arg), "all")
    return native.main([target]) == 0


def run_native(host_name, arguments):
    """Run make directly on Linux or macOS."""
    env = {**os.environ, **native_env(host_name)}
    try:
        return subprocess.call(["make", *arguments], cwd=SRC, env=env) == 0
    except OSError:
        return False


def tool_env(host_name):
    """Resolved image/QEMU tool paths exported to make."""
    logical = "linux" if host_name == "wsl" else host_name
    candidates = TOOL_CANDIDATES[logical]
    dirs = host.macos_path_dirs() if host_name == "macos" else windows_tool_dirs()
    variables = {
        "mkfs.vfat": "MKFS_VFAT", "mmd": "MMD", "mcopy": "MCOPY",
        "mksquashfs": "MKSQUASHFS", "xorriso": "XORRISO",
        "qemu-system-x86_64": "QEMU",
    }
    env = {}
    for tool, variable in variables.items():
        path = next((found for found in
                     (host.find_executable(name, *dirs)
                      for name in candidates.get(tool, []))
                     if found), None)
        if path:
            env[variable] = path
    return env


def native_env(host_name):
    """Environment for a direct make run on Linux/macOS."""
    env = {"PYTHON": sys.executable}
    if host_name == "linux":
        fasm = posixpath.join(SRC, "tools", "fasm")
        if Path(fasm).is_file():
            env["FASM"] = fasm
    elif host_name == "macos":
        fasm = host.find_executable("fasm", *host.macos_path_dirs())
        if fasm:
            env["FASM"] = fasm
    ovmf = host.ovmf(host=host_name)
    if ovmf:
        env["OVMF_CODE"], env["OVMF_VARS"] = ovmf
    env.update(tool_env(host_name))
    if host_name == "macos":
        dirs = host.macos_path_dirs()
        env["PATH"] = os.pathsep.join(
            [d for d in dirs + [os.environ.get("PATH")] if d])
        env["CC"] = host.find_executable("clang", *dirs) or "clang"
        env["CXX"] = host.find_executable("clang++", *dirs) or "clang++"
        env["LD"] = host.find_executable("ld.lld", *dirs) or "ld.lld"
        env["OBJCOPY"] = host.find_executable("llvm-objcopy", *dirs) or "llvm-objcopy"
        env["BOOT_CC"] = env["CC"]
        env["BOOT_LD"] = host.find_executable("lld-link", *dirs) or "lld-link"
    return env


def windows_exports():
    """Export-ready assignments for a POSIX shell on native Windows."""
    try:
        tool_map = native.tools()
    except native.BuildError:
        tool_map = {}
    exports = [f"PYTHON={host.shell_quote(posix_tool(sys.executable))}"]
    variables = {
        "CC": tool_map.get("clang"), "CXX": tool_map.get("clangxx"),
        "LD": tool_map.get("ld_lld"), "OBJCOPY": tool_map.get("objcopy"),
        "BOOT_CC": tool_map.get("clang"), "BOOT_LD": tool_map.get("lld_link"),
    }
    for variable, path in variables.items():
        if path:
            exports.append(f"{variable}={host.shell_quote(posix_tool(path))}")
    if tool_map.get("fasm"):
        exports.append(f"FASM={host.shell_quote(posix_tool(tool_map['fasm']))}")
    if tool_map.get("clang"):
        llvm = posix_tool(str(Path(short_path(tool_map["clang"])).parent))
        exports.append(f"PATH={host.shell_quote(llvm)}:\"$PATH\"")
    ovmf = host.ovmf(host="windows")
    if ovmf:
        exports.append(f"OVMF_CODE={host.shell_quote(posix_tool(ovmf[0]))}")
        exports.append(f"OVMF_VARS={host.shell_quote(posix_tool(ovmf[1]))}")
    # Git Bash rewrites /subsystem:efi_application and /entry:... into
    # Windows paths; lld-link must see them verbatim.
    exports.append("MSYS_NO_PATHCONV=1")
    exports.append(f"MSYS2_ARG_CONV_EXCL={host.shell_quote('*')}")
    return exports


def short_path(path):
    """8.3 short path for tools under directories with spaces (Windows)."""
    text = str(path)
    if " " not in text or not host.windows_ruby():
        return text

    shortened = host.short_path(text)
    if shortened and " " not in shortened and '"' not in shortened:
        return shortened

    # host.short_path drives cmd through a re-quoted command line, which can
    # mangle paths with spaces; ask the Win32 API for the same 8.3 name.
    try:
        import ctypes

        buffer = ctypes.create_unicode_buffer(32768)
        if ctypes.windll.kernel32.GetShortPathNameW(text, buffer, len(buffer)):
            return buffer.value or text
    except (AttributeError, OSError):
        pass
    return text


def posix_tool(path):
    """Tool path safe for make recipes: POSIX form with no spaces."""
    return host.to_posix_path(short_path(path))


def run_windows(arguments):
    """Run make through MSYS2/Git Bash on native Windows."""
    bash_exe = host.bash()
    if not bash_exe:
        raise host.ZosHostError("MSYS2/Git Bash not found")

    script = "; ".join(f"export {line}" for line in windows_exports())
    script += f"; cd {host.shell_quote(host.to_posix_path(SRC))}"
    script += " && make " + " ".join(host.shell_quote(arg) for arg in arguments)
    try:
        return subprocess.call([bash_exe, "-lc", script]) == 0
    except OSError:
        return False


def run_wsl(arguments, distro):
    """Run make inside the WSL distro, which owns its own python3."""
    wsl = wsl_executable()
    distro = wsl_distro(distro)
    src = decode_output(capture(wsl, "-d", distro, "--", "wslpath", "-a", SRC)).strip()
    if not src:
        raise host.ZosHostError(f"cannot translate paths in WSL distro {distro}")

    normalize_native_dependency_files()

    script = (f"cd {host.shell_quote(src)} && "
              "make " + " ".join(host.shell_quote(arg) for arg in arguments))
    try:
        return subprocess.call([wsl, "-d", distro, "--", "bash", "-lc", script]) == 0
    except OSError:
        return False


def normalize_native_dependency_files():
    """Rewrite backslash paths in native compiler dependency files."""
    for path in Path(SRC, "build").glob("**/*.d"):
        try:
            text = path.read_bytes()
        except OSError:
            continue
        if b"\\" not in text:
            continue
        try:
            path.write_bytes(text.replace(b"\\", b"/"))
        except OSError:
            continue


def wsl_executable():
    """Path to wsl.exe."""
    return host.find_executable("wsl", "C:/Windows/System32") or "wsl.exe"


def capture(*command):
    """Run a command and return its combined stdout/stderr as bytes."""
    try:
        result = subprocess.run([str(part) for part in command],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                check=False)
    except OSError:
        return b""
    return result.stdout or b""


def check(host_name, options):
    """Report tool status for --check; returns 0 when nothing is missing."""
    scope = options["check"] or "build"
    lookup = probe_tools(host_name, options["distro"])
    # On Windows the running interpreter satisfies the Python dependency.
    if host_name == "windows" and not lookup.get("python"):
        lookup["python"] = sys.executable
    # The repository ships a Linux/ELF fasm; report it on POSIX hosts even
    # when it is not on PATH.
    if host_name in ("linux", "macos", "wsl"):
        shipped_fasm = posixpath.join(SRC, "tools", "fasm")
        if Path(shipped_fasm).is_file() and not lookup.get("tools/fasm"):
            lookup["tools/fasm"] = shipped_fasm

    candidates = TOOL_CANDIDATES.get("linux" if host_name == "wsl" else host_name)
    if candidates is None:
        raise host.ZosHostError(f"tool check is not supported on {host_name}")
    resolved = {}
    for tool, names in candidates.items():
        resolved[tool] = next((lookup[name] for name in names if lookup.get(name)), None)

    native_mode = (host_name == "windows" and not windows_make_available()
                   and native_windows_build())
    if native_mode:
        resolved.update(native_tool_map())
    return print_report(host_name, resolved, scope, options["distro"], native_mode)


def native_tool_map():
    """Logical make variables satisfied by the no-make Windows driver."""
    try:
        tool_map = native.tools()
    except native.BuildError:
        return {}
    return {
        "cc": tool_map.get("clang"), "cxx": tool_map.get("clangxx"),
        "ld": tool_map.get("ld_lld"), "objcopy": tool_map.get("objcopy"),
        "clang": tool_map.get("clang"), "lld-link": tool_map.get("lld_link"),
        "fasm": tool_map.get("fasm"), "python": tool_map.get("python"),
        "tar": tool_map.get("tar"),
    }


def probe_tools(host_name, distro):
    """Resolve every candidate tool name for the host."""
    if host_name == "windows":
        return probe_in_bash(host.bash(), windows_exports())
    if host_name == "wsl":
        return probe_in_wsl(distro) if host.native_windows() else probe_native("linux")
    return probe_native(host_name)


def probe_native(host_name):
    """Resolve tool names through the local filesystem/PATH."""
    dirs = windows_tool_dirs()
    if host_name == "macos":
        dirs = host.macos_path_dirs() + dirs
    candidates = TOOL_CANDIDATES[host_name]
    names = list(dict.fromkeys(
        name for group in candidates.values() for name in group))
    lookup = {}
    for name in names:
        if "/" in name:
            path = posixpath.join(SRC, name)
            lookup[name] = path if Path(path).is_file() else None
        else:
            lookup[name] = host.find_executable(name, *dirs)
    return lookup


def probe_in_bash(bash_exe, exports):
    """Resolve tool names through Git Bash/MSYS2 `command -v`."""
    if not bash_exe:
        return {}

    names = list(dict.fromkeys(
        name for group in TOOL_CANDIDATES["windows"].values() for name in group))
    script = "; ".join(f"export {line}" for line in exports)
    script += "; " + "; ".join(
        f"printf '%s\\t%s\\n' {host.shell_quote(name)} "
        f"\"$(command -v {name} 2>/dev/null)\""
        for name in names)
    return parse_probe_output(decode_output(capture(bash_exe, "-lc", script)))


def probe_in_wsl(distro):
    """Resolve tool names inside the WSL distro."""
    if not (host.native_windows() and wsl_available()):
        return {}

    distro = wsl_distro(distro)
    names = list(dict.fromkeys(
        name for group in TOOL_CANDIDATES["linux"].values() for name in group))
    script = "; ".join(
        f"printf '%s\\t%s\\n' {host.shell_quote(name)} "
        f"\"$(command -v {name} 2>/dev/null)\""
        for name in names)
    output = decode_output(capture(wsl_executable(), "-d", distro, "--",
                                   "bash", "-lc", script))
    return parse_probe_output(output)


def parse_probe_output(output):
    """Turn `name<TAB>path` lines into a lookup dict."""
    lookup = {}
    for line in output.splitlines():
        parts = line.strip().split("\t", 1)
        if len(parts) == 2 and parts[0] and parts[1]:
            lookup[parts[0]] = parts[1]
    return lookup


def print_report(host_name, resolved, scope, distro=None, native_mode=False):
    """Print the tool report; returns 1 when a required tool is missing."""
    scopes = {
        "all": ["base", "kernel", "ada", "iso", "run"],
        "build": ["base", "kernel"],
        "iso": ["base", "kernel", "ada", "iso"],
    }.get(scope, [scope])
    optional = ["make"] if native_mode else []
    hints = INSTALL_HINTS.get(host_name, {})
    print(f"host: {host_name}")
    print(f"src:  {SRC}")
    if native_mode:
        print("mode: native no-make driver (LLVM + FASM + Python + tar)")
    if host_name == "windows":
        bash_exe = host.bash()
        shell_line = str(bash_exe).replace("\\", "/") if bash_exe else None
        print(f"shell: {shell_line or 'MSYS2/Git Bash not found (native build unavailable)'}")
    elif host_name == "wsl" and host.native_windows():
        print(f"wsl:   {'available' if wsl_available() else 'not found'}")
    elif host_name == "wsl":
        print(f"wsl:   inside WSL ({os.environ.get('WSL_DISTRO_NAME') or 'unknown distro'})")

    failed = False
    for group in scopes:
        print(f"{group}:")
        for name in SCOPE_GROUPS.get(group, []):
            path = resolved.get(name)
            if path:
                print(f"  {name:<22} {path}")
            elif name in optional:
                print(f"  {name:<22} optional (native no-make build)")
            else:
                failed = True
                hint = hints.get(name)
                print(f"  {name:<22} MISSING" + (f"  -- {hint}" if hint else ""))

    ovmf = wsl_ovmf(distro) if host_name == "wsl" else host.ovmf(host=host_name)
    firmware = (" + ".join(str(path).replace("\\", "/") for path in ovmf)
                if ovmf else "OVMF not found (set OVMF_CODE/OVMF_VARS)")
    print(f"firmware: {firmware}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
