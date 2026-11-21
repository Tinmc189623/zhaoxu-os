#!/usr/bin/env python3
"""Host matrix test for the OS-independent ext4 core (src/fs/ext4), before it
moves into Ring3.

Phase A runs our own formatter (32 MiB volume, unlink, fresh mounts, e2fsck,
a non-zero base, then a 256 MiB two-block-group volume).  Phase B mounts
mke2fs images read-only across block sizes and feature sets.

Runs on Windows Python and shells out to WSL, which owns gcc, mke2fs, e2fsck
and debugfs.

  cd <repo>
  python3 src/tools/ext4-test.py

Environment: EXT4_TEST_DISTRO (default Ubuntu-26.04).
"""

import os
import re
import subprocess
import sys
from pathlib import Path

DISTRO = os.environ.get("EXT4_TEST_DISTRO", "Ubuntu-26.04")
WIN_SRC = Path(__file__).resolve().parent.parent
_match = re.fullmatch(r"([A-Za-z]):[\\/](.*)", str(WIN_SRC))
if _match is None:
    sys.exit(f"ext4-test.rb: expected {WIN_SRC} under a drive letter")
SRC = "/mnt/{}/{}".format(_match.group(1).lower(),
                          _match.group(2).replace("\\", "/"))
BIN = f"{SRC}/build/ext4-test/ext4-test"
WORK = "/tmp/zos-ext4-test"
BLOCK = 4096  # ext4_format_volume always formats 4096-byte blocks
GROUP_BLOCKS = 32768  # EXT4_BPG in ext4_format.c
MAX_FILE_BLOCKS = 16 * 1024 * 1024 // BLOCK  # EXT4_MAX_FILE_SIZE in ext4.h

# e2fsck prints these when it is unhappy, whatever its exit status says.
BAD_FSCK = ["WARNING", "Fix?", "corrupt", "orphan", "still has errors"]

FIXTURES_WSL = f"{SRC}/build/ext4-test/fixtures"
FIXTURES_WIN = WIN_SRC / "build" / "ext4-test" / "fixtures"

failures = 0


def ruby_puts(text):
    """Print like Ruby's puts: add a newline only when missing."""
    sys.stdout.write(text if text.endswith("\n") else text + "\n")


def ruby_warn(text):
    """Print to stderr like Ruby's warn: add a newline only when missing."""
    sys.stderr.write(text if text.endswith("\n") else text + "\n")


def ruby_inspect(value):
    """Ruby-style inspect for None, integers, strings and lists of those."""
    if value is None:
        return "nil"
    if isinstance(value, list):
        return "[" + ", ".join(ruby_inspect(item) for item in value) + "]"
    if isinstance(value, str):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return str(value)


def sh(*lines):
    """Join shell lines into one script."""
    return "\n".join(lines) + "\n"


def wsl(script):
    """Run a bash script in WSL; return (output, success).

    The script goes in on stdin because wsl.exe mangles argv quoting (the
    repository path contains a space).  WSL_UTF8 makes wsl.exe emit UTF-8
    for its own notices, and its localhost-proxy lines are dropped.
    """
    env = dict(os.environ)
    env["WSL_UTF8"] = "1"
    proc = subprocess.run(
        ["wsl", "-d", DISTRO, "--", "bash", "-ls"],
        input=script.encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
        check=False,
    )
    text = proc.stdout.decode("utf-8", errors="replace")
    lines = [line for line in text.splitlines(keepends=True)
             if not line.startswith("wsl: ")]
    return "".join(lines), proc.returncode == 0


def check(name, ok, out=None, detail=None):
    """Record one PASS/FAIL case and print its output on failure."""
    global failures
    suffix = f" ({detail})" if detail is not None else ""
    if ok:
        ruby_puts(f"PASS {name}{suffix}")
    else:
        failures += 1
        ruby_puts(f"FAIL {name}{suffix}")
        if out:
            ruby_puts(out)
    return ok


def fsck(name, image):
    """Run e2fsck -fn and fail on bad words in the text as well as the status."""
    out, ok = wsl(f"e2fsck -fn {image}")
    hits = [word for word in BAD_FSCK if word in out]
    summary = next(
        (line.strip() for line in out.splitlines()
         if re.match(r"^\S+: \d+/\d+ files", line)),
        None,
    )
    check(name, ok and not hits, out,
          f"exit_ok={str(ok).lower()} warnings={ruby_inspect(hits)} "
          f"{summary or ''}")


def ramp_fixture(nbytes):
    """Return the WSL path of a k & 0xFF ramp fixture of nbytes bytes."""
    name = f"ramp{nbytes}.bin"
    path = FIXTURES_WIN / name
    if not path.is_file() or path.stat().st_size != nbytes:
        path.write_bytes(bytes(k & 0xFF for k in range(nbytes)))
    return f"{FIXTURES_WSL}/{name}"


# ---------------------------------------------------------------- build ----
build = sh(
    f'cd "{SRC}" || exit 1',
    "mkdir -p build/ext4-test",
    "gcc -std=gnu11 -O1 -g -fsanitize=address,undefined \\",
    "    -fno-sanitize-recover=all -Wall -Wextra \\",
    "    -I fs/ext4 -I tests/ext4/include -DEXT4_HOST \\",
    "    -o build/ext4-test/ext4-test \\",
    "    tests/ext4/host_main.c tests/ext4/host_io.c \\",
    "    fs/ext4/ext4_env.c fs/ext4/ext4_mount.c fs/ext4/ext4_extent.c \\",
    "    fs/ext4/ext4_dir.c fs/ext4/ext4_read.c fs/ext4/ext4_alloc.c \\",
    "    fs/ext4/ext4_write.c fs/ext4/ext4_format.c -lrt",
)
out, ok = wsl(build)
if not ok:
    ruby_warn(f"BUILD FAILED:\n{out}")
    sys.exit(1)
ruby_puts(f"PASS build host harness ({BIN})")

# --------------------------------------------------------------- fixtures ---
fixtures = sh(
    "set -e",
    f"rm -rf {WORK}",
    f"mkdir -p {WORK}",
    "yes abcdefghijklmnopqrstuvwxyz | tr -d '\\n' | head -c 70000 \\",
    f"    > {WORK}/alphabet70000.txt",
    "yes abcdefghijklmnopqrstuvwxyz | tr -d '\\n' | head -c 16777216 \\",
    f"    > {WORK}/alphabet16m.bin",
    f"head -c 5000 {WORK}/alphabet70000.txt > {WORK}/payload.bin",
    f"head -c 3000 {WORK}/alphabet70000.txt > {WORK}/payload3000.bin",
)
out, ok = wsl(fixtures)
if not ok:
    ruby_warn(f"FIXTURE SETUP FAILED:\n{out}")
    sys.exit(1)

os.makedirs(FIXTURES_WIN, exist_ok=True)

# ------------------------------------------------- phase A: our formatter ---
own = f"{WORK}/own.img"

# A1: format, two files, full read-back of the big one, one contig file, and a
# second process that reads the contig file back against the k & 0xFF ramp.
ramp50000 = ramp_fixture(50000)
script = sh(
    "set -e",
    f"dd if=/dev/zero of={own} bs=1M count=32 status=none",
    f'cd "{SRC}"',
    f'"{BIN}" {own} format 65536',
    f'"{BIN}" {own} mount 0 write /HELLO.TXT 70000 write /KEEP.TXT 3000 \\',
    "    stat /HELLO.TXT stat /",
    f'"{BIN}" {own} mount 0 cat /HELLO.TXT | cmp - '
    f'"{WORK}/alphabet70000.txt"',
    f'"{BIN}" {own} mount 0 contig /CONTIG.BIN 50000',
    f'"{BIN}" {own} mount 0 cat /CONTIG.BIN | cmp - "{ramp50000}"',
    f'"{BIN}" {own} mount 0 stat /CONTIG.BIN',
)
out, ok = wsl(script)
match = re.search(r"contig (\d+) (\d+)", out)
contig = [int(match.group(1)), int(match.group(2))] if match else None
contig_ok = contig and contig[0] > 0 and contig[1] * BLOCK >= 50000
check("own-format format+write+cat+contig",
      ok and "f 70000" in out and "d 4096" in out and
      "f 50000" in out and contig_ok,
      out, f"contig={ruby_inspect(contig)}")

# A2: unlink and sync, nothing else.
script = sh(
    "set -e",
    f'cd "{SRC}"',
    f'"{BIN}" {own} mount 0 rm /HELLO.TXT sync',
)
out, ok = wsl(script)
check("own-format unlink+sync", ok, out)

# A3: a fresh process mounts the image again.  The removal and the surviving
# file have to be what is on the disk.  The negative tests are if/exit,
# because ! and && lists are exempt from set -e.
script = sh(
    "set -e",
    f'cd "{SRC}"',
    f'"{BIN}" {own} mount 0 ls / > {WORK}/after-rm.lst',
    f"cat {WORK}/after-rm.lst",
    f"if grep -q 'HELLO.TXT' {WORK}/after-rm.lst; then",
    '    echo "HELLO.TXT is still listed after rm+sync and a fresh mount";'
    " exit 91",
    "fi",
    f'if "{BIN}" {own} mount 0 stat /HELLO.TXT; then',
    '    echo "HELLO.TXT still resolves after rm+sync and a fresh mount";'
    " exit 92",
    "fi",
    f"grep -q 'f 3000 KEEP.TXT' {WORK}/after-rm.lst",
    f"grep -q 'f 50000 CONTIG.BIN' {WORK}/after-rm.lst",
    f"grep -q 'd 4096 lost+found' {WORK}/after-rm.lst",
    f'"{BIN}" {own} mount 0 cat /KEEP.TXT | cmp - '
    f'"{WORK}/payload3000.bin"',
)
out, ok = wsl(script)
listing_ok = (
    ok and "f 3000 KEEP.TXT" in out and "f 50000 CONTIG.BIN" in out and
    "d 4096 lost+found" in out and "HELLO.TXT" not in out
)
check("own-format unlink survives a fresh mount", listing_ok, out)

# A4: e2fsck over the image the whole phase just rewrote.
fsck("own-format e2fsck -fn", own)

# A5: the same volume mounted at a non-zero base (the installer's layout).
offset = f"{WORK}/offset.img"
base = 4 * 1024 * 1024
script = sh(
    "set -e",
    f"dd if=/dev/zero of={offset} bs=1M count=40 status=none",
    f"dd if={own} of={offset} bs=1M seek=4 conv=notrunc status=none",
    f'cd "{SRC}"',
    f'"{BIN}" {offset} mount {base} cat /KEEP.TXT > {WORK}/base.txt',
    f"cmp {WORK}/payload3000.bin {WORK}/base.txt",
    f'"{BIN}" {offset} mount {base} ls /',
)
out, ok = wsl(script)
check(f"own-format mount at base {base}",
      ok and "f 3000 KEEP.TXT" in out, out)

# ------------------------------------------------- A6: two block groups ---
# 256 MiB is two 128 MiB block groups.  Group 0 holds the superblock, the GDT,
# both bitmaps, the 512-block inode table and the root/lost+found blocks: 518
# blocks used, 32250 free.  Seven full 16 MiB files plus a 3578-block tail fill
# the group exactly, so the next run has to be allocated in group 1.
MULTI_SECTORS = 524288
MULTI_BYTES = MULTI_SECTORS * 512
GROUP_META = 1 + 1 + 2 + 512  # super + GDT + both bitmaps + inode table
GROUP0_FREE = GROUP_BLOCKS - GROUP_META - 2  # minus root and lost+found
multi = f"{WORK}/multi.img"
fill = []
remaining = GROUP0_FREE
while remaining > 0:
    blocks = min(remaining, MAX_FILE_BLOCKS)
    fill.append({"path": f"/FILL{len(fill) + 1}.BIN", "bytes": blocks * BLOCK})
    remaining -= blocks
tail = fill[-1]
group1_bytes = 8 * BLOCK
group1_blocks = group1_bytes // BLOCK
ramp_group1 = ramp_fixture(group1_bytes)

write_cmds = " ".join(f"write {f['path']} {f['bytes']}" for f in fill)
script = sh(
    "set -e",
    f"truncate -s {MULTI_BYTES} {multi}",
    f'cd "{SRC}"',
    f'"{BIN}" {multi} format {MULTI_SECTORS}',
    f'"{BIN}" {multi} mount 0 {write_cmds} contig /GROUP1.BIN '
    f'{group1_bytes} ls /',
)
out, ok = wsl(script)
match = re.search(r"contig (\d+) (\d+)", out)
group1 = [int(match.group(1)), int(match.group(2))] if match else None
# The group-1 file must start inside group 1: only possible if group 0 really
# was full and the backup superblock/GDT of group 1 were skipped as allocated.
group1_ok = (group1 and group1[0] >= GROUP_BLOCKS and
             group1[1] * BLOCK >= group1_bytes)
tail_name = tail["path"].rsplit("/", 1)[-1]
check("multi-group format+fill+group-1 alloc",
      ok and group1_ok and
      f"f {fill[0]['bytes']} FILL1.BIN" in out and
      f"f {tail['bytes']} {tail_name}" in out and
      f"f {group1_bytes} GROUP1.BIN" in out and
      "d 4096 lost+found" in out,
      out,
      f"contig={ruby_inspect(group1)} "
      f"fill={ruby_inspect([f['bytes'] for f in fill])}")

# Group 1's backup metadata: the superblock copy starts at the first byte of
# block 32768 and the group descriptors follow at block 32769.  Both must match
# the primary copies apart from s_block_group_nr.
script = sh(
    "set -e",
    f"u16() {{ od -An -tu2 -j \"$2\" -N 2 \"$1\" | tr -d ' \\n'; }}",
    f"u32() {{ od -An -tu4 -j \"$2\" -N 4 \"$1\" | tr -d ' \\n'; }}",
    f"dd if={multi} bs={BLOCK} skip=0 count=1 status=none | "
    f"tail -c +1025 | head -c 1024 > {WORK}/primary.sb",
    f"dd if={multi} bs={BLOCK} skip={GROUP_BLOCKS} count=1 status=none | "
    f"head -c 1024 > {WORK}/group1.sb",
    f'[ "$(u16 {WORK}/group1.sb 56)" = "61267" ]',
    f'[ "$(u16 {WORK}/group1.sb 90)" = "1" ]',
    f'[ "$(u32 {WORK}/group1.sb 4)" = "{MULTI_BYTES // BLOCK}" ]',
    f'[ "$(u32 {WORK}/group1.sb 32)" = "{GROUP_BLOCKS}" ]',
    f'[ "$(u32 {WORK}/group1.sb 12)" = "$(u32 {WORK}/primary.sb 12)" ]',
    f'[ "$(u32 {WORK}/group1.sb 16)" = "$(u32 {WORK}/primary.sb 16)" ]',
    f"dd if={multi} bs={BLOCK} skip=1 count=1 status=none | "
    f"head -c 64 > {WORK}/primary.gdt",
    f"dd if={multi} bs={BLOCK} skip={GROUP_BLOCKS + 1} count=1 status=none | "
    f"head -c 64 > {WORK}/group1.gdt",
    f"cmp {WORK}/primary.gdt {WORK}/group1.gdt",
    f'[ "$(u16 {WORK}/primary.gdt 12)" = "0" ]',
    f'[ "$(u16 {WORK}/group1.gdt 44)" = '
    f'"{GROUP_BLOCKS - GROUP_META - group1_blocks}" ]',
)
out, ok = wsl(script)
check("multi-group backup superblock+GDT+free counts", ok, out)

# Read the group-0 boundaries and the group-1 file back from separate mounts.
script = sh(
    "set -e",
    f'cd "{SRC}"',
    f"head -c {tail['bytes']} {WORK}/alphabet16m.bin > "
    f"{WORK}/alphabet-tail.bin",
    f'"{BIN}" {multi} mount 0 cat {fill[0]["path"]} | cmp - '
    f'"{WORK}/alphabet16m.bin"',
    f'"{BIN}" {multi} mount 0 cat {tail["path"]} | cmp - '
    f'"{WORK}/alphabet-tail.bin"',
    f'"{BIN}" {multi} mount 0 cat /GROUP1.BIN | cmp - "{ramp_group1}"',
    f'"{BIN}" {multi} mount 0 stat /GROUP1.BIN',
)
out, ok = wsl(script)
check("multi-group read-back", ok and f"f {group1_bytes}" in out, out)

fsck("multi-group e2fsck -fn", multi)

# --------------------------------------------- phase B: mke2fs read-only ---
cases = []
for bs in (1024, 2048, 4096):
    for opt in ([], ["-O 64bit"], ["-O metadata_csum"], ["-O ^has_journal"]):
        cases.append({"block": bs, "opts": opt})

for case in cases:
    block = case["block"]
    opts = case["opts"]
    tag = "default" if not opts else opts[0].translate(
        str.maketrans("", "", " ^-"))
    image = f"{WORK}/mkfs-{block}-{tag}.img"
    script = sh(
        "set -e",
        f"rm -f {image}",
        f"dd if=/dev/zero of={image} bs=1M count=24 status=none",
        f"mke2fs -q -F -t ext4 -b {block} {' '.join(opts)} {image}",
        f'debugfs -w -R "write {WORK}/payload.bin /HELLO.TXT" '
        f'{image} >/dev/null',
        f'debugfs -R "stat /HELLO.TXT" {image} 2>/dev/null | '
        f"grep -q '^Inode:' || {{",
        f'  echo "debugfs did not create /HELLO.TXT in {image}"; exit 90; }}',
        f'cd "{SRC}"',
        f'"{BIN}" {image} mount 0 ls / stat /HELLO.TXT',
        f'"{BIN}" {image} mount 0 cat /HELLO.TXT > {WORK}/read-back.bin',
        f"cmp {WORK}/payload.bin {WORK}/read-back.bin",
    )
    out, ok = wsl(script)
    name = f"read-only block={block} opts={ruby_inspect(opts)}"
    check(name,
          ok and "f 5000 HELLO.TXT" in out and
          re.search(r"^f 5000$", out, re.MULTILINE) and
          re.search(r"^d \d+ lost\+found$", out, re.MULTILINE),
          out)

# ------------------------------------------------------------------ done ---
if failures == 0:
    ruby_puts("RESULT PASS")
    sys.exit(0)
ruby_warn(f"RESULT FAIL ({failures} failing case(s))")
sys.exit(1)
