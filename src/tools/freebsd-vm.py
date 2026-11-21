#!/usr/bin/env python3
"""Drive the pinned FreeBSD 15.1 VM under QEMU/KVM.

    python3 src/tools/freebsd-vm.py --gui               # window (WSLg)
    python3 src/tools/freebsd-vm.py --shot out.png      # screenshot
    python3 src/tools/freebsd-vm.py --keys "root"       # type text + Enter
    python3 src/tools/freebsd-vm.py --keys-file cmds.txt
    python3 src/tools/freebsd-vm.py --powerdown         # ACPI shutdown
    python3 src/tools/freebsd-vm.py --serial-login      # verify the getty
    python3 src/tools/freebsd-vm.py --serial-cmd "ls" 120
    python3 src/tools/freebsd-vm.py --build-live        # full build in the VM
    python3 src/tools/freebsd-vm.py --build-resume      # continue buildkernel
    python3 src/tools/freebsd-vm.py --probe             # headless serial probe

Image (in FREEBSD_VM_DIR, default ~/freebsd-vm):

    https://download.freebsd.org/releases/VM-IMAGES/15.1-RELEASE/amd64/Latest/
      FreeBSD-15.1-RELEASE-amd64-ufs.qcow2.xz

The official VM image has an empty root password.  The image only shows a
login on the VGA console until /etc/ttys enables the ttyu0 getty, so --gui
drives the console through QEMU monitor sendkey/screendump.
"""

import os
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

VM_DIR = Path(os.environ.get("FREEBSD_VM_DIR", "~/freebsd-vm")).expanduser()
IMAGE = VM_DIR / "FreeBSD-15.1-ufs.qcow2"
MONITOR = VM_DIR / "monitor.sock"
WORK = Path(os.environ.get("FREEBSD_WORK", "~/zhaoxu-freebsd")).expanduser()
TARBALL = WORK / "src-15.1-RELEASE.txz"
SERIAL_PORT = int(os.environ.get("FREEBSD_VM_SERIAL_PORT", 4555))
HTTP_PORT = int(os.environ.get("FREEBSD_VM_HTTP_PORT", 8000))
MEMORY = os.environ.get("FREEBSD_VM_MEMORY", "4096")
CPUS = os.environ.get("FREEBSD_VM_CPUS", "4")
BUILD_TIMEOUT = int(os.environ.get("FREEBSD_VM_BUILD_TIMEOUT", 2700))

KEYMAP = {
    " ": "spc", "-": "minus", "_": "shift-minus", "=": "equal",
    "+": "shift-equal", "/": "slash", ".": "dot", ":": "shift-semicolon",
    "|": "shift-backslash", "\"": "shift-apostrophe", "'": "apostrophe",
    "$": "shift-4", "{": "shift-bracket_left", "}": "shift-bracket_right",
    ">": "shift-dot", "<": "shift-comma", "&": "shift-7", "#": "shift-3",
    "%": "shift-5", "*": "shift-8", "!": "shift-1", "?": "shift-slash",
    "@": "shift-2", ";": "semicolon", "[": "bracket_left",
    "]": "bracket_right", ",": "comma", "\\": "backslash", "\n": "ret",
    "\t": "tab",
}


class Console:
    """Serial console reader with regex waits."""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.buffer = ""

    def write(self, text: str) -> None:
        """Send raw text to the guest."""
        self.sock.sendall(text.encode("utf-8"))

    def wait_for(self, pattern: str, timeout: float) -> str | None:
        """Wait for a regex, returning the text through the match."""
        regex = re.compile(pattern)
        deadline = time.time() + timeout
        while True:
            match = regex.search(self.buffer)
            if match:
                text = self.buffer[:match.end()]
                self.buffer = self.buffer[match.end():]
                return text
            left = deadline - time.time()
            if left <= 0:
                return None
            readable, _, _ = select.select([self.sock], [], [], min(left, 0.5))
            if not readable:
                continue
            chunk = self.sock.recv(4096)
            if not chunk:
                return None
            self.buffer += chunk.decode("utf-8", errors="replace")

    def run(self, command: str, marker: str, timeout: float) -> str:
        """Send one command and wait for its marker."""
        self.write(command + "\n")
        output = self.wait_for(marker, timeout)
        if output is None:
            raise SystemExit(f"timeout waiting for {marker}")
        return output


class Monitor:
    """QEMU HMP console: screendump and sendkey."""

    def __init__(self, path: Path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(str(path))

    def cmd(self, text: str) -> str:
        """Run one HMP command and read to the prompt."""
        self.sock.sendall((text + "\n").encode())
        buffer = ""
        deadline = time.time() + 10
        while True:
            left = deadline - time.time()
            if left <= 0:
                raise SystemExit(f"monitor timeout: {text}")
            readable, _, _ = select.select([self.sock], [], [], min(left, 0.5))
            if not readable:
                continue
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            buffer += chunk.decode("utf-8", errors="replace")
            if "(qemu)" in buffer:
                break
        return buffer

    def screendump(self) -> Path:
        """Capture the current screen as PPM."""
        path = VM_DIR / "screen.ppm"
        path.unlink(missing_ok=True)
        self.cmd(f"screendump {path}")
        return path

    def keys(self, sequence: list[str], delay: float = 0.03) -> None:
        """Inject keys one at a time; sendkey drops bursts."""
        for key in sequence:
            self.cmd(f"sendkey {key}")
            time.sleep(delay)


def key_sequence(text: str) -> list[str]:
    """Map text to QEMU key names."""
    keys = []
    for char in text:
        if "a" <= char <= "z" or "0" <= char <= "9":
            keys.append(char)
        elif "A" <= char <= "Z":
            keys.append("shift-" + char.lower())
        elif char in KEYMAP:
            keys.append(KEYMAP[char])
        else:
            raise SystemExit(f"unmapped character {char!r}")
    return keys


def start_qemu(display: str = "none", monitor: Path | None = None,
               serial: str | None = None) -> subprocess.Popen:
    """Start QEMU against the pinned image."""
    if not IMAGE.is_file():
        raise SystemExit(f"missing VM image {IMAGE}")
    if monitor:
        Path(monitor).unlink(missing_ok=True)

    args = [
        "qemu-system-x86_64", "-enable-kvm", "-M", "q35", "-cpu", "host",
        "-smp", CPUS, "-m", MEMORY,
        "-drive", f"file={IMAGE},if=virtio,format=qcow2",
        "-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0",
        "-display", display,
        "-monitor", f"unix:{monitor},server=on,wait=off" if monitor else "none",
        "-no-reboot",
    ]
    if serial:
        args += ["-serial", serial]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def connect_monitor() -> Monitor:
    """Connect to the running VM's monitor socket."""
    deadline = time.time() + 15
    while True:
        try:
            return Monitor(MONITOR)
        except OSError:
            if time.time() > deadline:
                raise SystemExit("monitor socket not ready")
            time.sleep(0.3)


def connect_serial() -> Console:
    """Connect to the running VM's serial port."""
    return Console(socket.create_connection(("127.0.0.1", SERIAL_PORT), timeout=10))


def start_http_server() -> subprocess.Popen:
    """Serve the source tarball to the guest over QEMU user networking."""
    print(f"http   : serving {WORK} on port {HTTP_PORT}", file=sys.stderr)
    return subprocess.Popen(
        [sys.executable, "-m", "http.server", str(HTTP_PORT), "--directory", str(WORK)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


def login(console: Console) -> None:
    """Authenticate as root over the serial console; reuse a live shell."""
    if console.wait_for(r"# \Z", 3):
        console.write("stty -echo\n")
        time.sleep(0.5)
        console.run("echo SYNC_OK", "SYNC_OK", 20)
        return

    if console.wait_for(r"login:", 180) is None:
        raise SystemExit("no login prompt on the serial console")

    for _ in range(3):
        console.write("root\n")
        if console.wait_for(r"(# |\$ )", 45):
            console.write("stty -echo\n")
            time.sleep(0.5)
            console.run("echo SYNC_OK", "SYNC_OK", 20)
            return
        console.write("\n")
    raise SystemExit("root login failed")


def gui() -> None:
    """Window plus serial; runs until the window closes."""
    serial = f"tcp:127.0.0.1:{SERIAL_PORT},server=on,wait=off"
    start_qemu(display="gtk", monitor=MONITOR, serial=serial).wait()


def shot(path: str) -> None:
    """Screenshot and convert to PNG."""
    ppm = connect_monitor().screendump()
    time.sleep(0.5)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(ppm), path],
                   check=False)
    print(path)


def keys(text: str) -> None:
    """Type text and press Enter twice."""
    monitor = connect_monitor()
    monitor.keys(key_sequence(text))
    time.sleep(0.3)
    monitor.keys(["ret"])
    time.sleep(0.3)
    monitor.keys(["ret"])
    print(f"sent {len(text)} characters")


def keys_file(path: str) -> None:
    """Type a file line by line, one command per line."""
    monitor = connect_monitor()
    lines = Path(path).read_text(encoding="utf-8").splitlines()
    for line in lines:
        if not line:
            continue
        monitor.keys(key_sequence(line))
        time.sleep(0.4)
        monitor.keys(["ret"])
        time.sleep(0.4)
        monitor.keys(["ret"])
        time.sleep(0.6)
    print(f"sent {len(lines)} line(s)")


def powerdown() -> None:
    """Ask the guest to shut down via ACPI."""
    connect_monitor().cmd("system_powerdown")
    print("sent ACPI powerdown")


def serial_login() -> None:
    """Verify that the serial getty accepts root."""
    console = connect_serial()
    login(console)
    print("serial : root login OK")


def serial_cmd(command: str, limit: float = 180) -> None:
    """Run one command over the serial console and print its output."""
    console = connect_serial()
    login(console)
    print(console.run(f"{command}; echo CMD_DONE_$?", r"CMD_DONE=\d+", limit))


def serial_debug() -> None:
    """Dump the raw serial login exchange."""
    console = connect_serial()
    sock = console.sock

    def read(seconds: float) -> str:
        output = ""
        deadline = time.time() + seconds
        while time.time() < deadline:
            readable, _, _ = select.select([sock], [], [], 0.3)
            if readable:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                output += chunk.decode("utf-8", errors="replace")
        return output

    print(f"--- initial: {read(3)!r}")
    sock.sendall(b"root\n")
    print(f"--- after root: {read(5)!r}")
    sock.sendall(b"\n")
    print(f"--- after enter: {read(5)!r}")
    sock.close()


def run_build(console: Console, fetch_source: bool) -> None:
    """Fetch (optionally), unpack, configure and build inside the VM."""
    console.run("ifconfig vtnet0 up; echo UP_RC=$?", r"UP_RC=", 60)
    console.run("dhclient vtnet0; echo DHCP_RC=$?", r"DHCP_RC=", 120)

    if fetch_source:
        print("guest  : fetching source")
        console.run(
            f"fetch -q -o /tmp/src.txz http://10.0.2.2:{HTTP_PORT}/src-15.1-RELEASE.txz; "
            "echo FETCH_RC=$?", r"FETCH_RC=", 900)
        print("guest  : extracting to /usr/src")
        console.run("rm -rf /usr/src && tar -xJf /tmp/src.txz -C /; echo TAR_RC=$?",
                    r"TAR_RC=", 900)
        print("guest  : writing ZHAOXU config")
        console.write("cat > /usr/src/sys/amd64/conf/ZHAOXU <<'ZEOF'\n")
        console.write('include "MINIMAL"\n')
        console.write("ident ZHAOXU\n")
        console.write("ZEOF\n")
        console.run("echo CONFIG_RC=$?", r"CONFIG_RC=", 60)

    print("guest  : buildkernel")
    output = console.run(
        f"cd /usr/src && make -j{CPUS} buildkernel KERNCONF=ZHAOXU; echo BUILD_RC=$?",
        r"BUILD_RC=\d+", BUILD_TIMEOUT)

    errors = [line for line in output.splitlines()
              if re.search(r"Kernel build|error:|Stop\.", line)]
    print("\n".join(errors[-20:]))
    rc = re.search(r"BUILD_RC=(\d+)", output)
    listing = console.run(
        "ls -l /usr/obj/usr/src/amd64.amd64/sys/ZHAOXU/kernel; echo LS_DONE",
        "LS_DONE", 30)
    print("\n".join(line for line in listing.splitlines() if line.endswith("kernel")))

    if not rc or rc.group(1) != "0":
        raise SystemExit(f"buildkernel failed (rc={rc.group(1) if rc else '?'})")
    print("build  : ZHAOXU kernel built inside the VM")


def build_live() -> None:
    """Full build against the already-running VM (keeps it running)."""
    if not TARBALL.is_file():
        raise SystemExit(f"missing {TARBALL}; run tools/freebsd-personality.py --fetch")
    http = start_http_server()
    try:
        console = connect_serial()
        login(console)
        print("guest  : serial root shell")
        run_build(console, fetch_source=True)
    finally:
        console.sock.close()
        http.terminate()


def build_resume() -> None:
    """Continue buildkernel on an existing /usr/src and /usr/obj."""
    console = connect_serial()
    try:
        login(console)
        console.run("test -f /usr/src/sys/amd64/conf/ZHAOXU && echo CONF_OK",
                    "CONF_OK", 30)
        print("guest  : resuming buildkernel")
        run_build(console, fetch_source=False)
    finally:
        console.sock.close()


def build_headless() -> None:
    """Boot a fresh headless VM and build from scratch."""
    if not TARBALL.is_file():
        raise SystemExit(f"missing {TARBALL}; run tools/freebsd-personality.py --fetch")
    http = start_http_server()
    qemu = start_qemu(serial=f"tcp:127.0.0.1:{SERIAL_PORT},server=on,wait=off")
    console = None
    try:
        console = connect_serial()
        login(console)
        run_build(console, fetch_source=True)
        console.write("poweroff\n")
        time.sleep(3)
    finally:
        if console:
            console.sock.close()
        qemu.terminate()
        http.terminate()


def main(argv: list[str]) -> int:
    mode = argv[1] if len(argv) > 1 else "--gui"
    if mode == "--gui":
        gui()
    elif mode == "--shot":
        shot(argv[2] if len(argv) > 2 else str(VM_DIR / "screen.png"))
    elif mode == "--keys":
        keys(argv[2])
    elif mode == "--keys-file":
        keys_file(argv[2])
    elif mode == "--key":
        connect_monitor().keys([argv[2]])
        print(f"sent {argv[2]}")
    elif mode == "--login":
        monitor = connect_monitor()
        monitor.keys(key_sequence("root"))
        monitor.keys(["ret"])
        time.sleep(2)
        monitor.keys(["ret"])
        print("sent root login")
    elif mode == "--powerdown":
        powerdown()
    elif mode == "--serial-login":
        serial_login()
    elif mode == "--serial-cmd":
        serial_cmd(argv[2], float(argv[3]) if len(argv) > 3 else 180)
    elif mode == "--serial-debug":
        serial_debug()
    elif mode == "--build-live":
        build_live()
    elif mode == "--build-resume":
        build_resume()
    elif mode == "--build":
        build_headless()
    elif mode == "--probe":
        serial_login()
    else:
        raise SystemExit("usage: freebsd-vm.py --gui|--shot|--keys|--keys-file|"
                         "--key|--login|--powerdown|--serial-login|--serial-cmd|"
                         "--serial-debug|--build-live|--build-resume|--build|--probe")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
