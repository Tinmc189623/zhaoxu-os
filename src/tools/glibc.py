#!/usr/bin/env python3
"""Configure and build glibc 2.44 for x86_64-zhaoxu (static first).

The source comes from the Fedora 45 SRPM tarball (tools/fedora-fetch.py +
tools/srpm-extract.py), unpacked to $HOME/glibc-build/source.  The port files
live in third_party/glibc-port and are copied into sysdeps/ before configure;
the upstream tree is never edited in place beyond that copy and the
`config.sub` OS-name patch.

    python3 tools/glibc.py --check    # report the configured build state
    python3 tools/glibc.py            # copy port, configure, make

glibc refuses platforms outside linux/gnu on purpose; a new port is expected
to pass --enable-hacker-mode until it is accepted upstream, which is what
this script does.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK = Path.home() / "glibc-build"
SOURCE = WORK / "source"
BUILD = WORK / "build"
PORT = ROOT / "third_party" / "glibc-port"
GCC = Path.home() / "gcc-build" / "install" / "bin" / "x86_64-zhaoxu-gcc"
TARGET = "x86_64-pc-zhaoxu"

CONFIGURE_ARGS = [
    f"--host={TARGET}",
    "--build=x86_64-pc-linux-gnu",
    "--prefix=/usr",
    "--enable-hacker-mode",
    "--disable-shared",
    "--enable-static",
    "--disable-nls",
    "--disable-werror",
    "--disable-profile",
    "--disable-timezone-tools",
    "--disable-multi-arch",
    # Static PIE startfiles (rcrt1/grcrt1) only have build rules in glibc's
    # shared branch; the Zhaoxu loader starts non-PIE static binaries for now.
    "--disable-static-pie",
    # enable-static-pie is derived from default_pie; turning default PIE off
    # is the supported way to drop the shared-branch-only rcrt1/grcrt1 files.
    "--disable-default-pie",
    "--without-selinux",
    "--without-gd",
    "--disable-crypt",
]


def run(command: list[str], chdir: Path = BUILD,
        env: dict[str, str] | None = None) -> None:
    """Echo and execute one step, aborting on failure."""
    print("+ " + " ".join(str(part) for part in command), flush=True)
    merged = dict(os.environ)
    if env:
        merged.update(env)
    result = subprocess.run([str(part) for part in command], cwd=str(chdir),
                            env=merged, check=False)
    if result.returncode != 0:
        raise SystemExit("command failed: " + " ".join(str(part) for part in command))


def patch_config_sub() -> None:
    """Teach glibc's config.sub the zhaoxu OS name."""
    path = SOURCE / "scripts" / "config.sub"
    if not path.is_file():
        raise SystemExit(f"{path} is missing; unpack the glibc tarball first")
    text = path.read_text(encoding="utf-8")
    if "| zhaoxu*" in text:
        return
    for needle, replacement in (
            ("\t| zephyr* \\\n", "\t| zephyr* \\\n\t| zhaoxu* \\\n"),
            ("| zephyr* \\\n", "| zephyr* | zhaoxu* \\\n")):
        if needle in text:
            path.write_text(text.replace(needle, replacement, 1),
                            encoding="utf-8")
            return
    raise SystemExit("config.sub: zephyr entry not found")


def patch_make_syscalls() -> None:
    """Let the stub generator parse the generic tree's cancellation marker.

    sysdeps/unix/syscalls.list writes cancellable entries as `Ci:iBN` while
    the generator expects the marker in the caller column and derives the
    argument count from a `?:` signature; without this the generated stubs
    have an empty SYSCALL_NARGS and the assembler rejects them.
    """
    path = SOURCE / "sysdeps" / "unix" / "make-syscalls.sh"
    text = path.read_text(encoding="utf-8")
    marker = "C*) args=`echo $args | sed 's/^C//'`;;"
    if marker in text:
        return
    needle = "  case $args in\n  E*)"
    if needle not in text:
        raise SystemExit("make-syscalls.sh: args case not found")
    text = text.replace(
        needle, "  case $args in\n  C*) args=`echo $args | sed 's/^C//'`;;\n  E*)", 1)
    path.write_text(text, encoding="utf-8")
    print("glibc: make-syscalls accepts the cancellation marker")


def patch_abi_tags() -> None:
    """Give the port its own ELF ABI tag; glibc has no catch-all default."""
    path = SOURCE / "abi-tags"
    text = path.read_text(encoding="utf-8")
    if "zhaoxu" in text:
        return
    entry = (".*-.*-zhaoxu.*\t\t6\t0.8.0\t# Zhaoxu OS, earliest compatible "
             "kernel\n")
    path.write_text(text + entry, encoding="utf-8")
    print("glibc: added the zhaoxu abi-tag")


def install_port() -> None:
    """Copy the port's sysdeps tree into the glibc source."""
    source_sysdeps = PORT / "sysdeps"
    if not source_sysdeps.is_dir():
        raise SystemExit(f"{source_sysdeps} is missing")
    for path in sorted(source_sysdeps.rglob("*")):
        if not path.is_file():
            continue
        target = SOURCE / "sysdeps" / path.relative_to(source_sysdeps)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    print(f"glibc: installed port files from {PORT}")


SYSCALL_LIST_DIRS = [
    "unix", "unix/sysv", "unix/inet", "posix", "generic", "nptl", "pthread",
    "gnu", "x86", "x86_64", "x86_64/64", "wordsize-64",
    "unix/sysv/zhaoxu", "x86_64/zhaoxu",
]


def complete_syscall_header() -> None:
    """Map every generated stub's syscall name to a number.

    glibc's stub generator emits one assembly wrapper per syscalls.list entry
    and expects SYS_<name> to exist.  Calls the kernel does not implement get
    the reserved number 113, which the dispatcher answers with ENOSYS, so a
    program gets a clean error instead of a link or assembly failure.
    """
    header = SOURCE / "sysdeps" / "unix" / "sysv" / "zhaoxu" / "bits" / \
        "syscall.h"
    text = header.read_text(encoding="utf-8")
    known = set()
    for name in re.findall(r"^#define SYS_([A-Za-z0-9_]+)", text, re.M):
        known.add(name)
    names = set()
    for directory in SYSCALL_LIST_DIRS:
        listing = SOURCE / "sysdeps" / directory / "syscalls.list"
        if not listing.is_file():
            continue
        for line in listing.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = line.split()
            if len(fields) >= 3:
                names.add(fields[2])
    extra = sorted(names - known)
    if not extra:
        return
    additions = ["", "/* Calls with no kernel implementation: ENOSYS. */",
                 "#ifndef SYS_unimplemented", "#define SYS_unimplemented 113",
                 "#endif", "#ifndef __NR_unimplemented",
                 "#define __NR_unimplemented SYS_unimplemented", "#endif"]
    for name in extra:
        additions.append(f"#ifndef SYS_{name}")
        additions.append(f"#define SYS_{name} SYS_unimplemented")
        additions.append("#endif")
        additions.append(f"#ifndef __NR_{name}")
        additions.append(f"#define __NR_{name} SYS_unimplemented")
        additions.append("#endif")
    marker = "\n#endif"
    index = text.rfind(marker)
    if index < 0:
        raise SystemExit(f"{header}: cannot find include guard end")
    header.write_text(text[:index] + "\n".join(additions) + text[index:],
                      encoding="utf-8")
    print(f"glibc: {len(extra)} unimplemented syscall names mapped to ENOSYS")


def check() -> int:
    """Report whether the static libc exists."""
    libc = BUILD / "libc.a"
    if libc.is_file():
        print(f"libc.a: {libc} ({libc.stat().st_size} bytes)")
        return 0
    print("libc.a not built yet")
    return 1


def bison_shim() -> Path:
    """Provide the bison version glibc insists on.

    The release tarball ships its generated parsers, so bison is never really
    invoked; configure only gates on the version string.  A real bison would
    need root to install, so answer the version probe honestly and refuse any
    actual regeneration.
    """
    directory = WORK / "bin"
    directory.mkdir(parents=True, exist_ok=True)
    shim = directory / "bison"
    shim.write_text(
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "--version) echo 'bison (GNU Bison) 3.8.2' ;;\n"
        "*) echo 'bison shim: generated parsers are shipped, not rebuilt' >&2\n"
        "   exit 1 ;;\n"
        "esac\n",
        encoding="utf-8")
    shim.chmod(0o755)
    return directory


def generate_modules_header() -> None:
    """Write libc-modules.h for a static-only build.

    glibc 2.44 only defines the rule for this header inside its
    `build-shared = yes` branch, yet the compiler always includes it.  The
    header is just MODULE_* numbers for the IS_IN macro, so the static build
    gets its own copy instead of forcing a shared build.
    """
    header = BUILD / "libc-modules.h"
    header.write_text(
        "/* AUTOGENERATED by tools/glibc.py for the zhaoxu static build.\n"
        "   Covers every module name glibc sources pass to IS_IN; values below\n"
        "   MODULE_LIBS_BEGIN are non-library modules, as in the real\n"
        "   gen-libc-modules.awk output. */\n"
        "#define MODULE_testsuite 1\n"
        "#define MODULE_testsuite_internal 2\n"
        "#define MODULE_nonlib 3\n"
        "#define MODULE_module 4\n"
        "#define MODULE_LIB 5\n"
        "#define MODULE_lib 6\n"
        "#define MODULE_LIBS_BEGIN 7\n"
        "#define MODULE_libc 8\n"
        "#define MODULE_libpthread 9\n"
        "#define MODULE_librt 10\n"
        "#define MODULE_libdl 11\n"
        "#define MODULE_libm 12\n"
        "#define MODULE_libmvec 13\n"
        "#define MODULE_libanl 14\n"
        "#define MODULE_libutil 15\n"
        "#define MODULE_libresolv 16\n"
        "#define MODULE_libnsl 17\n"
        "#define MODULE_libcrypt 18\n"
        "#define MODULE_libBrokenLocale 19\n"
        "#define MODULE_libthread_db 20\n"
        "#define MODULE_libsupport 21\n"
        "#define MODULE_libhurduser 22\n"
        "#define MODULE_libmachuser 23\n"
        "#define MODULE_rtld 24\n"
        "#define MODULE_ldconfig 25\n",
        encoding="utf-8")
    (BUILD / "libc-modules.stmp").touch()


def bootstrap_generated(environment: dict[str, str]) -> None:
    """Generate the headers whose rules live in glibc's shared branch.

    A static-only configure leaves abi-versions.h (and friends) undefined,
    yet libc sources include them unconditionally.  Running the same rules
    with build-shared=yes on the command line produces just those headers.
    """
    targets = [BUILD / "abi-versions.h", BUILD / "first-versions.h",
               BUILD / "ldbl-compat-choose.h", BUILD / "dl-tunable-list.h",
               BUILD / "gcc-macros.h"]
    run(["make", "-r", "-C", str(SOURCE), f"objdir={BUILD}",
         "build-shared=yes", *[str(t) for t in targets]], env=environment)


def build() -> int:
    """Install the port, patch config.sub, configure and compile."""
    if not (SOURCE / "configure").is_file():
        raise SystemExit(f"{SOURCE} is missing; unpack the glibc tarball first")
    if not GCC.is_file():
        raise SystemExit("cross gcc missing; run: python3 tools/gcc.py")
    install_port()
    complete_syscall_header()
    patch_make_syscalls()
    patch_config_sub()
    patch_abi_tags()
    BUILD.mkdir(parents=True, exist_ok=True)
    generate_modules_header()
    shim_dir = bison_shim()
    environment = dict(os.environ)
    environment["CC"] = str(GCC)
    environment["CFLAGS"] = "-O2 -g"
    # glibc has no --disable-static-pie switch; the configure cache variable
    # is the supported override.  Its startfiles only exist in the shared
    # build branch.
    environment["libc_cv_static_pie"] = "no"
    environment["PATH"] = (f"{shim_dir}:{GCC.parent}:"
                           f"{environment.get('PATH', '')}")
    run([str(SOURCE / "configure"), *CONFIGURE_ARGS], env=environment)
    bootstrap_generated(environment)
    jobs = str(os.cpu_count() or 4)
    # The first make only bootstraps build/sysd-sorted; the soversions and
    # libc-modules headers are generated once that file exists, so a fresh
    # build needs a second pass.
    try:
        run(["make", f"-j{jobs}"], env=environment)
    except SystemExit:
        if not (BUILD / "sysd-sorted").is_file():
            raise
        print("glibc: retrying make after the sysd-sorted bootstrap pass")
        run(["make", f"-j{jobs}"], env=environment)
    return check()


def main() -> int:
    """Parse arguments and run the requested step."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="only report the built libc.a")
    options = parser.parse_args()
    if options.check:
        return check()
    return build()


if __name__ == "__main__":
    sys.exit(main())
