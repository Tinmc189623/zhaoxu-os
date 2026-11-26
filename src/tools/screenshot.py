#!/usr/bin/env python3
"""Boot Zhaoxu OS headless, wait for Aqua Desktop Environment, and capture
the QEMU framebuffer to a PNG for visual verification.

usage: python tools/screenshot.py [seconds-before-shot] [out.png]
env:   SMOKE_DISTRO SMOKE_ROOT SMOKE_BOOT SMOKE_TIMEOUT
       SHOT_HMP: newline-separated HMP commands (e.g. mouse_move /
                 mouse_button) executed before the screendump;
                 SHOT_STEP (default 0.2) and SHOT_WAIT (default 0.5)
                 control the pause between commands / before the shot.

Boots with -display none, waits for the desktop through wsl.exe, then asks
the QEMU HMP monitor (TCP) for a screendump and turns the PPM into a PNG
with zlib.
"""

import glob
import os
import re
import select
import socket
import struct
import subprocess
import sys
import threading
import time
import zlib
from pathlib import Path

DEFAULT_DISTRO = "Ubuntu-26.04"
DEFAULT_ISO = "zhaoxu-os_x86-64_0.7.20_2026-11-16_ld.iso"

_INT_RE = re.compile(rb"\A[ \t\n\v\f\r]*([+-]?)(\d[\d_]*)")
_FLOAT_RE = re.compile(
    rb"\A[ \t\n\v\f\r]*([+-]?)(\d[\d_]*(?:\.[\d_]*)?|\.[\d_]+)"
    rb"(?:[eE]([+-]?\d[\d_]*))?"
)


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


def ruby_to_f(value):
    """Parse a leading float the way Ruby String#to_f does."""
    data = value if isinstance(value, bytes) else str(value).encode("utf-8")
    match = _FLOAT_RE.match(data)
    if not match:
        return 0.0
    text = (match.group(1) + match.group(2)).replace(b"_", b"")
    if match.group(3):
        text += b"e" + match.group(3).replace(b"_", b"")
    return float(text)


def windows_to_wsl(path):
    """Convert a Windows drive path to its /mnt/<drive>/... form."""
    match = re.fullmatch(r"([A-Za-z]):[\\/](.*)", path)
    if not match:
        return path
    return "/mnt/" + match.group(1).lower() + "/" + match.group(2).replace("\\", "/")


def ruby_file_join(*parts):
    """Join path fragments the way Ruby File.join does."""
    result = ""
    for part in parts:
        if not result:
            result = part
        elif result.endswith(("/", "\\")) or part.startswith(("/", "\\")):
            result += part
        else:
            result += os.sep + part
    return result


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


_BYTE_ESCAPES = {
    7: b"\\a", 8: b"\\b", 9: b"\\t", 10: b"\\n",
    11: b"\\v", 12: b"\\f", 13: b"\\r", 27: b"\\e",
}


def ruby_inspect_bytes(data):
    """Quote a binary string the way Ruby String#inspect does."""
    out = bytearray(b'"')
    for position, byte in enumerate(data):
        if byte == 0x22:
            out += b'\\"'
        elif byte == 0x5C:
            out += b"\\\\"
        elif byte in _BYTE_ESCAPES:
            out += _BYTE_ESCAPES[byte]
        elif byte == 0x23 and position + 1 < len(data) and data[position + 1] in b"{$@":
            out += b"\\#"
        elif 0x20 <= byte <= 0x7E:
            out.append(byte)
        else:
            out += b"\\x%02X" % byte
    out += b'"'
    return bytes(out)


def png_chunk(chunk_type, data):
    """Pack one PNG chunk with its length and CRC."""
    return (
        struct.pack(">I", len(data))
        + chunk_type
        + data
        + struct.pack(">I", zlib.crc32(chunk_type + data) & 0xFFFFFFFF)
    )


def ppm_to_png(ppm):
    """Convert a binary PPM (P6) buffer into PNG bytes and its size."""
    # Scan the four header tokens byte-wise; splitting on whitespace would eat
    # pixel bytes that happen to be whitespace on some hosts.
    index = 0
    size = len(ppm)
    tokens = []
    while len(tokens) < 4:
        while index < size and ppm[index] <= 32:
            index += 1
        start = index
        while index < size and ppm[index] > 32:
            index += 1
        tokens.append(ppm[start:index])
    index += 1
    magic, width_text, height_text, maxval_text = tokens
    pixels = None if index > size else ppm[index:]
    if magic != b"P6":
        abort(b"bad ppm magic " + ruby_inspect_bytes(magic))
    width = ruby_to_i(width_text)
    height = ruby_to_i(height_text)
    if ruby_to_i(maxval_text) != 255 or pixels is None:
        abort("bad ppm header")
    stride = width * 3
    if len(pixels) < stride * height:
        abort("short ppm body")

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * stride:(y + 1) * stride])
    idat = zlib.compress(bytes(raw), 9)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width & 0xFFFFFFFF, height & 0xFFFFFFFF, 8, 2, 0, 0, 0),
    )
    png += png_chunk(b"IDAT", idat)
    png += png_chunk(b"IEND", b"")
    return png, width, height


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


def capture_monitor():
    """Send the optional HMP script and the screendump request."""
    response = bytearray()
    try:
        sock = socket.create_connection(("127.0.0.1", 45454))
        ready, _, _ = select.select([sock], [], [], 1.0)
        if ready:
            chunk = sock.recv(4096)
            if not chunk:
                raise EOFError("end of file reached")
            response.extend(chunk)
        # HMP splits arguments on whitespace; QEMU's cwd is already the src dir.
        for line in env_or("SHOT_HMP", "").split("\n"):
            if not line:
                continue
            sock.sendall((line.strip() + "\n").encode("utf-8"))
            time.sleep(ruby_to_f(env_or("SHOT_STEP", "0.2")))
        time.sleep(ruby_to_f(env_or("SHOT_WAIT", "0.5")))
        sock.sendall(b"screendump out/desk.ppm\n")
        time.sleep(1.5)
        sock.sendall(b"quit\n")
        deadline = time.time() + 1.0
        while time.time() < deadline:
            ready, _, _ = select.select([sock], [], [], 0.2)
            if not ready:
                break
            chunk = sock.recv(4096)
            if not chunk:
                break
            response.extend(chunk)
        sock.close()
        if b"PPM" not in response:
            write_line(sys.stderr.buffer, b"monitor response:\n" + bytes(response))
    except Exception as error:
        write_line(
            sys.stderr.buffer,
            ("monitor connection failed: " + str(error)).encode("utf-8"),
        )


def main():
    """Boot, grab the QEMU framebuffer and write out.png."""
    distro = env_or("SMOKE_DISTRO", DEFAULT_DISTRO)
    win_root = env_or("SMOKE_ROOT", str(Path(__file__).resolve().parent.parent))
    root = windows_to_wsl(win_root)
    timeout = ruby_to_i(env_or("SMOKE_TIMEOUT", "180"))
    delay = ruby_to_f(sys.argv[1]) if len(sys.argv) > 1 else 2.0
    shot = sys.argv[2] if len(sys.argv) > 2 else "out/desk.png"

    # q35 exposes six single-unit IDE ports; ide.2/ide.3 collide with ide.1 on
    # some QEMU builds, so put the installer extras on ide.4/ide.5.
    installer_disks = [
        "-drive if=none,id=zsrc,format=raw,file=out/efi.img",
        "-device ide-hd,drive=zsrc,bus=ide.4",
        "-drive if=none,id=zdst,format=raw,file=out/target.img",
        "-device ide-hd,drive=zdst,bus=ide.5",
    ]
    boot = env_or("SMOKE_BOOT", "esp")
    # ISO images are named zhaoxu-os_x64_{version}_{build}_ld.iso.
    iso_pattern = os.path.join(win_root, "out", "zhaoxu-os_x64_*_ld.iso")
    iso_candidates = sorted(glob.glob(iso_pattern))
    iso_file = max(iso_candidates, key=os.path.getmtime) if iso_candidates else None
    if boot.startswith("iso") and not iso_file:
        abort("out/zhaoxu-os_x64_*_ld.iso not found; run `make iso` first")
    iso_name = os.path.basename(iso_file) if iso_file else DEFAULT_ISO
    iso_drive = "-cdrom out/" + iso_name

    if boot == "esp":
        drives = [
            "-drive format=raw,file=fat:rw:esp",
            "-drive if=none,id=zdata,format=raw,file=out/data.img",
            "-device ide-hd,drive=zdata,bus=ide.1",
            *installer_disks,
        ]
    elif boot == "iso":
        drives = [
            iso_drive,
            "-drive if=none,id=zdata,format=raw,file=out/data.img",
            "-device ide-hd,drive=zdata,bus=ide.1",
            *installer_disks,
        ]
    elif boot == "iso-nodata":
        drives = [iso_drive]
    elif boot == "iso-data":
        drives = [
            iso_drive,
            "-drive if=none,id=zdata,format=raw,file=out/data.img",
            "-device ide-hd,drive=zdata,bus=ide.1",
        ]
    elif boot == "iso-install":
        # A blank target on ide.1 and no ZHAOXUDATA volume, so the graphical
        # installer opens instead of exiting early.
        drives = [
            iso_drive,
            "-drive if=none,id=zdst,format=raw,file=out/target.img",
            "-device ide-hd,drive=zdst,bus=ide.1",
        ]
    elif boot == "installed":
        drives = [
            "-drive if=none,id=zdst,format=raw,file=out/target.img",
            "-device ide-hd,drive=zdst,bus=ide.0",
        ]
    else:
        abort("SMOKE_BOOT must be esp, iso, iso-nodata, iso-data, iso-install or installed")

    inner = 'cd "' + root + '" || exit 1\n'
    inner += "rm -f out/desk.ppm\n"
    inner += 'cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" out/OVMF_VARS.fd\n'
    qemu = [
        "timeout %d qemu-system-x86_64" % timeout,
        "-M ${QEMU_MACHINE:-q35} -m ${QEMU_MEM:-512M} -smp 1",
        "-drive if=pflash,format=raw,unit=0,readonly=on,file=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}",
        "-drive if=pflash,format=raw,unit=1,file=out/OVMF_VARS.fd",
        *drives,
        "-net none -serial stdio -display none -no-reboot",
        "-monitor tcp:127.0.0.1:45454,server,nowait",
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

    deadline = time.time() + timeout
    started = time.time()
    while True:
        left = deadline - time.time()
        if left <= 0:
            break
        time.sleep(0.25)
        if time.time() - started >= delay:
            break
    with lock:
        serial = bytes(out)
    time.sleep(0.5)

    capture_monitor()
    time.sleep(0.5)
    serial_path = os.environ.get("SHOT_SERIAL")
    if serial_path is not None:
        Path(serial_path).write_bytes(serial)

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

    result = subprocess.run(
        ["wsl", "-d", distro, "--", "bash", "-lc", 'cat "' + root + '/out/desk.ppm"'],
        stdout=subprocess.PIPE,
    )
    ppm = result.stdout
    if not ppm:
        abort(b"no screendump produced\n" + serial[-2000:])

    png, width, height = ppm_to_png(ppm)
    Path(ruby_file_join(win_root, shot)).write_bytes(png)
    write_line(
        sys.stdout.buffer,
        ("saved " + shot + " (%dx%d, %d bytes)" % (width, height, len(png))).encode("utf-8"),
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
