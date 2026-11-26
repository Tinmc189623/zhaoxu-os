#!/usr/bin/env python3
"""Boot Zhaoxu OS headless in QEMU (inside WSL) and assert that a marker
appears in the serial console output.

usage: python tools/smoke.py <marker>
env:   SMOKE_DISTRO SMOKE_ROOT SMOKE_TIMEOUT (seconds, default 180)
       SMOKE_FW: uefi (default, OVMF pflash) or bios (SeaBIOS, no pflash)
       SMOKE_BOOT: iso (default, CD only, no extra disks)
                   iso-install (CD + one blank out/target.img)
                   installed (boot out/target.img alone)
       bios firmware supports iso, iso-install and installed.

The smoke test watches serial output only and does not drive the shell;
installation is exercised by the graphical installer or by booting the
installed target afterwards.

Run from Windows Python: it shells out through wsl.exe and watches the
serial console until the marker shows up or the timeout expires.
"""

import glob
import os
import re
import subprocess
import sys
import threading
import time
import unicodedata
from pathlib import Path

DEFAULT_DISTRO = "Ubuntu-26.04"
DEFAULT_ISO = "zhaoxu-os_x64_0.7.20_2026-11-16_ld.iso"

_INT_RE = re.compile(rb"\A[ \t\n\v\f\r]*([+-]?)(\d[\d_]*)")
_INSPECT_ESCAPES = {
    "\a": "\\a", "\b": "\\b", "\t": "\\t", "\n": "\\n",
    "\v": "\\v", "\f": "\\f", "\r": "\\r", "\x1b": "\\e",
}


def env_or(name, default):
    """Return an environment value, even when it is an empty string."""
    return os.environ[name] if name in os.environ else default


def ruby_to_i(value):
    """Parse a leading integer the way Ruby String#to_i does."""
    data = value if isinstance(value, bytes) else str(value).encode("utf-8")
    match = _INT_RE.match(data)
    if not match:
        return 0
    number = int(match.group(2).replace(b"_", b""))
    return -number if match.group(1) == b"-" else number


def windows_to_wsl(path):
    """Convert a Windows drive path to its /mnt/<drive>/... form."""
    match = re.fullmatch(r"([A-Za-z]):[\\/](.*)", path)
    if not match:
        return path
    return "/mnt/" + match.group(1).lower() + "/" + match.group(2).replace("\\", "/")


def ruby_char_printable(char):
    """Match the characters Ruby String#inspect leaves unescaped."""
    if char < "\x80":
        return "\x20" <= char <= "\x7e"
    return unicodedata.category(char) not in ("Cc", "Cs", "Zl", "Zp", "Cn")


def ruby_inspect(text):
    """Quote a string the way Ruby String#inspect does."""
    out = ['"']
    for position, char in enumerate(text):
        if char == '"':
            out.append('\\"')
        elif char == "\\":
            out.append("\\\\")
        elif char in _INSPECT_ESCAPES:
            out.append(_INSPECT_ESCAPES[char])
        elif char == "#" and position + 1 < len(text) and text[position + 1] in "{$@":
            out.append("\\#")
        elif ruby_char_printable(char):
            out.append(char)
        elif ord(char) <= 0xFFFF:
            out.append("\\u%04X" % ord(char))
        else:
            out.append("\\u{%X}" % ord(char))
    out.append('"')
    return "".join(out)


def write_line(stream, data):
    """Write bytes to a binary stream, adding a newline unless present."""
    if not data.endswith(b"\n"):
        data += b"\n"
    stream.write(data)
    stream.flush()


def abort(message):
    """Print message to stderr and exit with status 1 like Ruby abort."""
    if isinstance(message, str):
        message = message.encode("utf-8")
    write_line(sys.stderr.buffer, message)
    raise SystemExit(1)


def read_stream(stream, buffer, lock, finished, index):
    """Drain one child pipe into the shared byte buffer."""
    try:
        while True:
            chunk = os.read(stream.fileno(), 4096)
            if not chunk:
                break
            with lock:
                buffer.extend(chunk)
    finally:
        finished[index] = True


def main():
    """Run the headless QEMU smoke test and return the exit status."""
    distro = env_or("SMOKE_DISTRO", DEFAULT_DISTRO)
    win_root = env_or("SMOKE_ROOT", str(Path(__file__).resolve().parent.parent))
    root = windows_to_wsl(win_root)
    timeout = ruby_to_i(env_or("SMOKE_TIMEOUT", "180"))

    if len(sys.argv) < 2:
        abort("usage: smoke.rb <marker>")
    marker = sys.argv[1]
    marker_bytes = marker.encode("utf-8")

    target_disk = [
        "-drive if=none,id=zdst,format=raw,file=out/target.img",
        "-device ide-hd,drive=zdst,bus=ide.1",
    ]
    # ISO images are named zhaoxu-os_x64_{version}_{build-date}_ld.iso.
    iso_pattern = os.path.join(win_root, "out", "zhaoxu-os_x64_*_ld.iso")
    iso_candidates = sorted(glob.glob(iso_pattern))
    iso_file = max(iso_candidates, key=os.path.getmtime) if iso_candidates else None
    iso_name = os.path.basename(iso_file) if iso_file else DEFAULT_ISO
    iso_drives = [
        "-drive if=none,id=zcd,format=raw,media=cdrom,readonly=on,file=out/" + iso_name,
        "-device ide-cd,drive=zcd,bus=ide.0",
    ]
    boot = env_or("SMOKE_BOOT", "iso")
    fw = env_or("SMOKE_FW", "uefi")
    if boot.startswith("iso") and not iso_file:
        abort("out/zhaoxu-os_x64_*_ld.iso not found; run `make iso` first")

    if boot == "iso":
        drives = iso_drives
    elif boot == "iso-install":
        drives = iso_drives + target_disk
    elif boot == "installed":
        drives = [
            "-drive if=none,id=zdst,format=raw,file=out/target.img",
            "-device ide-hd,drive=zdst,bus=ide.0",
        ]
    else:
        abort("SMOKE_BOOT must be iso, iso-install or installed")

    if fw == "uefi":
        firmware = [
            "-drive if=pflash,format=raw,unit=0,readonly=on,"
            "file=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}",
            "-drive if=pflash,format=raw,unit=1,file=out/OVMF_VARS.fd",
        ]
        boot_order = ["-boot", "order=d"] if boot.startswith("iso") else []
    elif fw == "bios":
        if boot not in ("iso", "iso-install", "installed"):
            abort("SMOKE_FW=bios supports only iso, iso-install and installed")
        firmware = []
        boot_order = ["-boot", "order=c" if boot == "installed" else "order=d"]
    else:
        abort("SMOKE_FW must be uefi or bios")

    inner = 'cd "' + root + '" || exit 1\n'
    if fw == "uefi":
        inner += 'cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" out/OVMF_VARS.fd\n'
    qemu = [
        "timeout %d qemu-system-x86_64" % timeout,
        "-M ${QEMU_MACHINE:-q35} -m ${QEMU_MEM:-512M} -smp 1",
        *firmware,
        *drives,
        *boot_order,
        "-net none -serial stdio -monitor none -display none -no-reboot",
    ]
    joined = "\n".join("  " + line + " \\\\" for line in qemu)
    if joined.endswith(" \\\\"):
        joined = joined[:-3]
    inner += joined + "\n"

    out = bytearray()
    lock = threading.Lock()
    finished = [False, False]
    process = subprocess.Popen(
        ["wsl", "-d", distro, "--", "bash", "-lc", inner],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    readers = [
        threading.Thread(target=read_stream, args=(process.stdout, out, lock, finished, 0)),
        threading.Thread(target=read_stream, args=(process.stderr, out, lock, finished, 1)),
    ]
    for reader in readers:
        reader.daemon = True
        reader.start()

    ok = False
    deadline = time.time() + timeout
    eof_count = 0
    while True:
        left = deadline - time.time()
        if left <= 0:
            break
        time.sleep(min(left, 0.25))
        with lock:
            if marker_bytes in out:
                ok = True
                break
        eof_count += sum(1 for value in finished if value)
        if eof_count >= 2:
            break

    try:
        process.kill()
    except OSError:
        pass
    for reader in readers:
        reader.join(timeout=1.0)

    subprocess.run(
        ["wsl", "-d", distro, "--", "pkill", "-f", "qemu-system-x86_64"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    if ok:
        write_line(sys.stdout.buffer, ("SMOKE PASS: " + ruby_inspect(marker)).encode("utf-8"))
        write_line(sys.stdout.buffer, b"--- last 6000 bytes ---")
        if "SMOKE_DUMP" in os.environ:
            write_line(sys.stdout.buffer, bytes(out[-6000:]))
        return 0

    write_line(sys.stderr.buffer, ("SMOKE FAIL: " + ruby_inspect(marker)).encode("utf-8"))
    write_line(sys.stderr.buffer, b"--- last 3000 bytes ---")
    write_line(sys.stderr.buffer, bytes(out[-3000:]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
