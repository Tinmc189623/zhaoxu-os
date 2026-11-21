#!/usr/bin/env python3
"""Cross-platform QEMU runner for Zhaoxu OS.

Builds (through build.py) and boots the ISO with the host's QEMU/OVMF.
Same four hosts as build.py: native Linux, macOS, Windows (MSYS2) and WSL2.

    python3 tools/run.py                       # UEFI, ISO only
    python3 tools/run.py --display gtk         # WSLg / desktop window
    python3 tools/run.py --smp 4 --mem 1024
    python3 tools/run.py run-bios              # any make run target
    python3 tools/run.py run-install           # ISO + blank target disk

Anything after the options is passed to make as extra target/variables.
"""

import os
import re
import subprocess
import sys

import build
import host

SRC = build.SRC

RUN_TARGETS = ["run-iso", "run", "run-bios", "run-install", "run-install-bios"]


def take(argv):
    """Pop the next argument, or None when the list is empty."""
    return argv.pop(0) if argv else None


def main(argv):
    """CLI entry point; returns the process exit code."""
    try:
        options = parse_options(list(argv))
        if options is None:
            return 0

        host_name = build.resolve_host(options)
        if host_name in ("linux", "macos"):
            ok = run_native(host_name, options)
        elif host_name == "windows":
            ok = run_windows(options)
        elif host_name == "wsl":
            ok = run_wsl(options) if host.native_windows() else run_native("linux", options)
        else:
            raise host.ZosHostError(f"unsupported host: {host_name}")
        return 0 if ok else 1
    except host.ZosHostError as error:
        print(f"run: {error}", file=sys.stderr)
        return 1


def parse_options(argv):
    """Parse the runner CLI; returns the options dict or None for --help."""
    options = {
        "host": "auto", "distro": None, "display": "none", "machine": "q35",
        "mem": 512, "smp": 4, "jobs": 4, "headless": None, "args": [],
    }
    while argv:
        argument = argv.pop(0)
        if argument == "--host":
            options["host"] = take(argv)
        elif re.fullmatch(r"--host=(.+)", argument):
            options["host"] = re.fullmatch(r"--host=(.+)", argument).group(1)
        elif argument == "--distro":
            options["distro"] = take(argv)
        elif re.fullmatch(r"--distro=(.+)", argument):
            options["distro"] = re.fullmatch(r"--distro=(.+)", argument).group(1)
        elif argument == "--display":
            options["display"] = take(argv)
        elif re.fullmatch(r"--display=(.+)", argument):
            options["display"] = re.fullmatch(r"--display=(.+)", argument).group(1)
        elif argument == "--machine":
            options["machine"] = take(argv)
        elif re.fullmatch(r"--machine=(.+)", argument):
            options["machine"] = re.fullmatch(r"--machine=(.+)", argument).group(1)
        elif argument == "--mem":
            options["mem"] = int(take(argv))
        elif re.fullmatch(r"--mem=(\d+)", argument):
            options["mem"] = int(re.fullmatch(r"--mem=(\d+)", argument).group(1))
        elif argument == "--smp":
            options["smp"] = int(take(argv))
        elif re.fullmatch(r"--smp=(\d+)", argument):
            options["smp"] = int(re.fullmatch(r"--smp=(\d+)", argument).group(1))
        elif argument in ("--jobs", "-j"):
            options["jobs"] = int(take(argv))
        elif re.fullmatch(r"-j(\d+)", argument):
            options["jobs"] = int(re.fullmatch(r"-j(\d+)", argument).group(1))
        elif argument == "--headless":
            options["headless"] = 1
        elif argument == "--gui":
            options["headless"] = 0
        elif argument in ("--help", "-h"):
            print(help_text())
            return None
        else:
            options["args"] = [argument] + argv
            break
    return options


def help_text():
    """Usage text for --help."""
    return f"""usage: python3 tools/run.py [options] [make target/variables...]

  --display none|gtk|sdl|curses  QEMU display backend (default none)
  --machine q35|pc               QEMU machine (default q35)
  --mem MiB                      guest RAM (default 512)
  --smp N                        guest CPUs (default 4)
  --jobs N, -jN                  build jobs
  --headless / --gui             build mode
  --host auto|native|wsl|...
  --distro NAME                  WSL distro
  --help

targets: {', '.join(RUN_TARGETS)}
"""


def make_arguments(options):
    """Make arguments that boot the ISO with the requested QEMU settings."""
    first = options["args"][0] if options["args"] else None
    target = first if first in RUN_TARGETS else "run-iso"
    extra = options["args"][1:] if first in RUN_TARGETS else options["args"]
    arguments = [target]
    arguments.append(f"QEMU_MACHINE={options['machine']}")
    arguments.append(f"QEMU_MEM={options['mem']}M")
    arguments.append(f"QEMU_SMP={options['smp']}")
    arguments.append(f"QEMU_DISPLAY=-display {options['display']}")
    if options["headless"] is not None:
        arguments.append(f"HEADLESS={options['headless']}")
    return arguments + (extra or [])


def run_native(host_name, options):
    """Run make on Linux or macOS with the resolved environment."""
    env = {**os.environ, **build.native_env(host_name)}
    qemu = host.find_executable("qemu-system-x86_64", *tool_dirs(host_name))
    if qemu:
        env["QEMU"] = qemu
    try:
        return subprocess.call(["make", *make_arguments(options)],
                               cwd=SRC, env=env) == 0
    except OSError:
        return False


def run_windows(options):
    """Run make through MSYS2/Git Bash."""
    bash_exe = host.bash()
    if not bash_exe:
        raise host.ZosHostError("MSYS2/Git Bash not found")

    script = "; ".join(f"export {line}" for line in build.windows_exports())
    script += f"; cd {host.shell_quote(host.to_posix_path(SRC))}"
    script += " && make " + " ".join(
        host.shell_quote(arg) for arg in make_arguments(options))
    try:
        return subprocess.call([bash_exe, "-lc", script]) == 0
    except OSError:
        return False


def run_wsl(options):
    """Run make inside the WSL distro, which owns its own python3."""
    if not build.wsl_available():
        raise host.ZosHostError("no registered WSL distro")

    wsl = build.wsl_executable()
    distro = build.wsl_distro(options["distro"])
    src = build.decode_output(
        build.capture(wsl, "-d", distro, "--", "wslpath", "-a", SRC)).strip()
    if not src:
        raise host.ZosHostError(f"cannot translate paths in WSL distro {distro}")

    script = (f"cd {host.shell_quote(src)} && "
              "make " + " ".join(
                  host.shell_quote(arg) for arg in make_arguments(options)))
    try:
        return subprocess.call([wsl, "-d", distro, "--", "bash", "-lc", script]) == 0
    except OSError:
        return False


def tool_dirs(host_name):
    """Directories to search for qemu-system-x86_64."""
    dirs = [f"{SRC}/tools"]
    if host_name == "macos":
        dirs += host.macos_path_dirs()
    return dirs


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
