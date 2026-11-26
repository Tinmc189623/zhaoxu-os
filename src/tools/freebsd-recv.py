#!/usr/bin/env python3
"""Receive a file pushed out of the FreeBSD 15.1 VM.

QEMU user networking lets the guest reach the host at 10.0.2.2 but not the
other way round, so artifacts built inside the guest (the combined OS_5P
image, for example) travel by a guest-side push:

    python3 src/tools/freebsd-recv.py --port 9000 --output out/os5p.elf
    # inside the guest:
    nc 10.0.2.2 9000 < /tmp/os5p_port.elf

The receiver writes to a .part file, prints the byte count, then renames.
"""

import argparse
import socket
import sys
from pathlib import Path


def main() -> int:
    """Bind, accept one connection, stream it to the output file."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    target = Path(args.output)
    part = target.with_suffix(target.suffix + ".part")
    part.parent.mkdir(parents=True, exist_ok=True)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", args.port))
    server.listen(1)
    print(f"recv   : listening on {args.port}", file=sys.stderr)

    conn, peer = server.accept()
    written = 0
    with conn, part.open("wb") as stream:
        while True:
            chunk = conn.recv(1 << 20)
            if not chunk:
                break
            stream.write(chunk)
            written += len(chunk)
    server.close()

    part.replace(target)
    print(f"recv   : {written} bytes -> {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
