#!/usr/bin/env python3
"""Build the Yuanshuo kernel on a native Windows host without MSYS2/make.

The regular build path is the GNU Makefile.  On Windows the toolchain may
exist without GNU make (LLVM + Python + Git's tar), so this driver runs the
same compile/link/package steps directly.

Supported targets:
  all     kernel.elf and initramfs.tar (Limine supplies the boot layer)
  clean   remove build/out artifacts
  host-test  run tests/tools/host_test.py

ISO targets need xorriso, mkfs.vfat, mtools and mksquashfs and are rejected
here; they remain the Makefile's job on hosts that have those tools.

Ada drivers are not compiled by this driver (no GNAT on a plain Windows
host).  The release step reuses the drivers.zxe produced by the last Ada
build; the driver fails loudly if that artifact is missing.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent.as_posix()

# Filled by build_all(); every compile/link step reads it.
TOOLS = {}

_makefile_text = None


class BuildError(RuntimeError):
    """Raised when a build step cannot run."""


# ---- tool discovery ---------------------------------------------------

def llvm_bin(env=None):
    """Directory holding clang.exe."""
    env = os.environ if env is None else env
    for directory in (env.get("LLVM_BIN"), "C:/Program Files/LLVM/bin"):
        if directory and Path(directory, "clang.exe").is_file():
            return directory
    return None


def tools(env=None):
    """Resolve every LLVM/tar/python executable used by the driver."""
    env = os.environ if env is None else env
    bin_dir = llvm_bin(env)
    if not bin_dir:
        raise BuildError(
            "LLVM not found (set LLVM_BIN to the directory holding clang.exe)")

    return {
        "clang": f"{bin_dir}/clang.exe",
        "clangxx": f"{bin_dir}/clang++.exe",
        "ld_lld": f"{bin_dir}/ld.lld.exe",
        "lld_link": f"{bin_dir}/lld-link.exe",
        "objcopy": f"{bin_dir}/llvm-objcopy.exe",
        "python": sys.executable,
        "tar": tar_path(env),
    }


def tar_path(env=None):
    """Git for Windows tar.exe."""
    env = os.environ if env is None else env
    for path in (env.get("ZOS_TAR"),
                 "C:/Program Files/Git/usr/bin/tar.exe",
                 "C:/Program Files (x86)/Git/usr/bin/tar.exe"):
        if path and Path(path).is_file():
            return path
    return None


def available(env=None):
    """True when every tool the driver needs is present."""
    try:
        resolved = tools(env)
    except BuildError:
        return False
    return all(resolved[key] for key in
               ("clang", "clangxx", "ld_lld", "lld_link", "objcopy", "python", "tar"))


def missing(env=None):
    """Names of the tools that could not be resolved."""
    try:
        resolved = tools(env)
    except BuildError as error:
        return [str(error)]
    return [key for key, value in resolved.items() if not value]


# ---- CLI --------------------------------------------------------------

def main(argv):
    """CLI entry point; returns the process exit code."""
    target = argv[0] if argv else "all"
    try:
        if target == "all":
            build_all()
        elif target == "clean":
            clean()
        elif target == "host-test":
            run([sys.executable, "tests/tools/host_test.py"])
        else:
            print(f"native build: target '{target}' needs the Makefile toolchain "
                  "(make + Ada GNAT + xorriso/mtools/squashfs for ISO); "
                  "this no-make driver supports: all, clean, host-test",
                  file=sys.stderr)
            return 1
    except BuildError as error:
        print(f"native build: {error}", file=sys.stderr)
        return 1
    return 0


# ---- build ------------------------------------------------------------

def build_all():
    """Full kernel + initramfs build."""
    TOOLS.update(tools())
    prepare_directories()
    write_headless_header()
    build_userland()
    build_initramfs()
    link_kernel()
    report()


def prepare_directories():
    """Create the build output directories."""
    for directory_name in ("build", "out", "build/iso"):
        Path(SRC, directory_name).mkdir(parents=True, exist_ok=True)


def write_headless_header():
    """Write the fixed headless flag header."""
    Path(SRC, "build", "headless.h").write_text(
        "#define BUILD_HEADLESS 0\n", encoding="utf-8")


# ---- userland ---------------------------------------------------------

def build_userland():
    """Build ADE, ext4fs and the standalone ZXE programs."""
    build_ade()
    build_ext4fs()
    build_user_program("init", ["user/init.c"], [], "build/init.uelf", "out/init.zxe")
    build_user_program("hello", ["user/hello.c"], [], "build/hello.uelf", "out/hello.zxe")
    build_user_program("ping", ["user/ping.c"], [], "build/ping.uelf", "out/ping.zxe")
    build_user_program("pong", ["user/pong.c"], [], "build/pong.uelf", "out/pong.zxe")
    build_user_program("welcome_client", make_var("WELCOME_SRCS"),
                       ["-Iuser", "-Iuser/lib/include", "-Ikernel/include"],
                       "build/welcome_client.uelf", "out/welcome_client.zxe")
    build_user_program("calc_client", make_var("CALC_SRCS"),
                       ["-Iuser", "-Iuser/lib/include", "-Ikernel/include"],
                       "build/calc_client.uelf", "out/calc_client.zxe")
    build_user_program("install_client", make_var("INSTALL_SRCS"),
                       ["-Iuser", "-Iuser/lib/include", "-Ikernel/include"],
                       "build/install_client.uelf", "out/install_client.zxe")


def build_user_program(name, sources, includes, uelf, zxe):
    """Compile, link and pack one user program."""
    objects = []
    for source in with_userlib(sources):
        obj = f"build/{name}/" + re.sub(r"\.c$", ".o", source)
        compile_object(source, obj, user_cflags() + includes)
        objects.append(obj)
    link_shared(objects, uelf)
    run([TOOLS["python"], "tools/zxe-pack.py", uelf, zxe])


def with_userlib(sources):
    """Append userlib.c when the source list omits it."""
    if "user/userlib.c" in sources:
        return sources
    return sources + ["user/userlib.c"]


def link_shared(objects, uelf):
    """Link a PIE user image with ld.lld."""
    run([TOOLS["ld_lld"], "-m", "elf_x86_64", "-shared", "-Bsymbolic",
         "--build-id=none", "-T", user_link_script(),
         "-z", "max-page-size=0x1000", "-z", "norelro", "--no-rosegment",
         "-o", uelf, *objects])


def user_link_script():
    """Generate the lld-safe user linker script once, then reuse it."""
    script = Path(SRC, "build", "user.ld")
    if script.is_file():
        return str(script)

    text = Path(SRC, "user", "user.ld").read_text(encoding="utf-8")
    text = text.replace("        *(.dynamic)\n", "", 1)
    text = text.replace(
        "    .data : ALIGN(4096)",
        "    .dynamic : ALIGN(4096)\n"
        "    {\n        *(.dynamic)\n    } :data\n\n"
        "    .data : ALIGN(4096)", 1)
    script.parent.mkdir(parents=True, exist_ok=True)
    script.write_text(text, encoding="utf-8")
    return str(script)


def build_ade():
    """Build the Aqua Desktop Environment server."""
    ade_build = "build/ade"
    objects = []
    for source in make_var("ADE_C_SRCS"):
        obj = f"{ade_build}/" + re.sub(r"\.c$", ".o", source)
        compile_object(source, obj, ade_cflags())
        objects.append(obj)
    for source in make_var("ADE_FONT_SRCS"):
        obj = f"{ade_build}/" + re.sub(r"\.c$", ".o", source)
        compile_object(source, obj, ade_font_cflags())
        objects.append(obj)
    for source in make_var("ADE_CXX_SRCS"):
        obj = f"{ade_build}/" + re.sub(r"\.cpp$", ".o", source)
        compile_object(source, obj, ade_cxxflags(), cc=TOOLS["clangxx"])
        objects.append(obj)
    link_shared(objects, "build/aqua.uelf")
    run([TOOLS["python"], "tools/zxe-pack.py", "build/aqua.uelf", "out/aqua.zxe"])


def build_ext4fs():
    """Build the Ring3 ext4 service."""
    objects = []
    for source in make_var("EXT4FS_SRCS"):
        obj = "build/ext4fs/" + re.sub(r"\.c$", ".o", source)
        compile_object(source, obj,
                       user_cflags()
                       + ["-Ifs/ext4", "-Iapps/ext4fs", "-Iuser", "-Ikernel/include"])
        objects.append(obj)
    link_shared(objects, "build/ext4fs.uelf")
    run([TOOLS["python"], "tools/zxe-pack.py", "build/ext4fs.uelf",
         "out/ext4fs.zxe"])


def blob_object(input_path, output, prefix, section):
    """Wrap a flat binary in an ELF object via llvm-objcopy."""
    # objcopy's binary-input symbol turns every non-alphanumeric to '_'.
    symbol = re.sub(r"[^A-Za-z0-9]", "_", input_path)
    run([TOOLS["objcopy"], "-I", "binary", "-O", "elf64-x86-64",
         "-B", "i386:x86-64",
         f"--rename-section=.data={section},alloc,load,readonly,data,contents",
         f"--redefine-sym=_binary_{symbol}_start={prefix}_start",
         f"--redefine-sym=_binary_{symbol}_end={prefix}_end",
         f"--redefine-sym=_binary_{symbol}_size={prefix}_size",
         input_path, output])


# ---- initramfs in the kernel -----------------------------------------

def build_initramfs():
    """Stage the initramfs tree, tar it and embed it as an object."""
    tree = Path(SRC, "build", "initramfs").as_posix()
    shutil.rmtree(tree, ignore_errors=True)
    directory(tree, "bin")
    directory(tree, "bin/apps")
    directory(tree, "boot")
    directory(tree, "boot/limine")
    directory(tree, "etc")
    directory(tree, "mnt")
    directory(tree, "data")
    directory(tree, "data/DOCS")
    copy("out/init.zxe", f"{tree}/bin/init")
    copy("out/aqua.zxe", f"{tree}/bin/aqua")
    copy("out/ext4fs.zxe", f"{tree}/bin/ext4fs")
    copy("out/welcome_client.zxe", f"{tree}/bin/apps/welcome.zxe")
    copy("out/calc_client.zxe", f"{tree}/bin/apps/calc.zxe")
    copy("out/install_client.zxe", f"{tree}/bin/apps/install.zxe")
    copy("third_party/limine/bin/BOOTX64.EFI",
         f"{tree}/boot/limine/BOOTX64.EFI")
    copy("third_party/limine/bin/limine-bios.sys",
         f"{tree}/boot/limine/limine-bios.sys")
    run([TOOLS["python"], "tools/limine-bios-hdd.py",
         "third_party/limine/limine-bios-hdd.h",
         "build/limine-bios-hdd.bin"])
    copy("build/limine-bios-hdd.bin",
         f"{tree}/boot/limine/limine-bios-hdd.bin")
    copy("initramfs/etc/motd", f"{tree}/etc/motd")
    copy("initramfs/etc/version", f"{tree}/etc/version")
    copy("initramfs/data/hello.txt", f"{tree}/data/HELLO.TXT")
    copy("initramfs/data/info.txt", f"{tree}/data/DOCS/INFO.TXT")
    copy("out/hello.zxe", f"{tree}/data/HELLO.ZXE")
    copy("build/hello.uelf", f"{tree}/data/HELLO.ELF")
    copy("out/ping.zxe", f"{tree}/data/PING.ZXE")
    copy("out/pong.zxe", f"{tree}/data/PONG.ZXE")
    copy_drivers(f"{tree}/bin/drivers")
    run([TOOLS["tar"], "--format=ustar", "--owner=0", "--group=0",
         "--numeric-owner", "-cf", "out/initramfs.tar", "-C",
         "build/initramfs", "."])
    blob_object("out/initramfs.tar", "build/initramfs.o", "initramfs",
                ".initramfs")


def copy_drivers(destination):
    """Reuse the Ada drivers artifact from the last full build."""
    source = Path(SRC, "out", "drivers.zxe")
    if not source.is_file():
        raise BuildError(
            "out/drivers.zxe is missing and this host has no GNAT Ada "
            "compiler; build the drivers on a host with gnat-14 first")
    shutil.copy(str(source), destination)


# ---- kernel -----------------------------------------------------------

def link_kernel():
    """Compile every kernel source and link kernel.elf."""
    objects = []
    for source in make_var("C_SRCS"):
        obj = "build/" + re.sub(r"\.c$", ".o", source)
        compile_object(source, obj, kernel_cflags())
        objects.append(obj)
    for source in make_var("ASM_SRCS"):
        obj = "build/" + re.sub(r"\.S$", ".o", source)
        run([TOOLS["clang"], "--target=x86_64-elf", "-m64", "-c", source,
             "-o", obj])
        objects.append(obj)
    objects.append("build/initramfs.o")
    run([TOOLS["ld_lld"], "-m", "elf_x86_64", "-T", "kernel/linker.ld",
         "-nostdlib", "-static", "--build-id=none",
         "-z", "max-page-size=0x1000", "-o", "out/kernel.elf", *objects])


def report():
    """Print the produced image sizes."""
    print("native build: all")
    for relative in ("out/kernel.elf", "out/initramfs.tar"):
        size = Path(SRC, relative).stat().st_size
        print(f"  {relative:<28} {size:>8d} bytes")


# ---- flags ------------------------------------------------------------

def cross():
    """Clang cross-compilation target flags."""
    return ["--target=x86_64-elf"]


def kernel_cflags():
    """Kernel compile flags."""
    return cross() + [
        "-m64", "-std=gnu11", "-O2", "-g", "-ffreestanding", "-fno-pie",
        "-fno-pic", "-fno-stack-protector", "-fno-asynchronous-unwind-tables",
        "-mno-red-zone", "-mcmodel=kernel", "-mgeneral-regs-only",
        "-mno-sse", "-mno-sse2", "-mno-mmx", "-mno-avx", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-Ikernel/include", "-Ikernel", "-Ikernel/font",
        "-DPROC_SELFTEST=0", "-DDEBUG_SERIAL=0", "-DSCHED_STATS=0",
    ]


def user_cflags():
    """User program compile flags."""
    return cross() + [
        "-m64", "-std=gnu11", "-O2", "-ffreestanding", "-fpie",
        "-fno-stack-protector", "-fno-asynchronous-unwind-tables",
        "-mno-red-zone", "-mno-sse", "-mno-sse2", "-mno-mmx", "-mno-avx",
        "-Wall", "-Wextra", "-Wno-unused-parameter",
    ]


def user_ldflags():
    """User program link flags (kept for parity with the Makefile)."""
    return cross() + [
        "-fuse-ld=lld", "-nostdlib", "-shared", "-Wl,-Bsymbolic,--build-id=none",
        "-Wl,-T,user/user.ld", "-Wl,-z,max-page-size=0x1000",
    ]


def ade_cflags():
    """ADE C compile flags."""
    return user_cflags() + [
        "-I.", "-Iuser", "-Iuser/lib/include", "-Ikernel/include",
        "-Ikernel", "-Ikernel/font", "-DZOS_USER",
    ]


def ade_font_cflags():
    """ADE font sources compile with SSE2 but without the kernel SSE ban."""
    base = [flag for flag in ade_cflags() if flag not in ("-mno-sse", "-mno-sse2")]
    return base + ["-msse2", "-mno-avx", "-fno-math-errno"]


def ade_cxxflags():
    """ADE C++ compile flags."""
    base = [flag for flag in ade_cflags() if flag != "-std=gnu11"]
    return base + [
        "-std=gnu++20", "-fno-exceptions", "-fno-rtti",
        "-fno-threadsafe-statics", "-fno-use-cxa-atexit",
    ]


# ---- Makefile source lists -------------------------------------------

def makefile_text():
    """Cached Makefile contents."""
    global _makefile_text
    if _makefile_text is None:
        _makefile_text = Path(SRC, "Makefile").read_text(encoding="utf-8")
    return _makefile_text


def make_var(name):
    """Read a `NAME := a b \\` continuation list from the Makefile."""
    lines = makefile_text().splitlines(keepends=True)
    pattern = re.compile(rf"^{re.escape(name)}\s*:?=\s*")
    start = next((i for i, line in enumerate(lines) if pattern.match(line)), None)
    if start is None:
        raise BuildError(f"Makefile variable {name} not found")

    parts = []
    index = start
    while True:
        line = lines[index]
        body = pattern.sub("", line, count=1) if index == start else line
        parts.append(re.sub(r"\\\s*$", "", body))
        if not body.rstrip().endswith("\\"):
            break
        index += 1
    words = re.split(r"\s+", "".join(parts))
    return [word for word in words if word and not word.startswith("$(")]


# ---- process helpers ------------------------------------------------

def compile_object(source, obj, flags, cc=None):
    """Compile one source file into an object under build/."""
    cc = cc or TOOLS["clang"]
    flags = flags + ["-DHEADLESS=0"] if source == "kernel/fs/install.c" else flags
    Path(SRC, obj).parent.mkdir(parents=True, exist_ok=True)
    run([cc, *flags, "-c", source, "-o", obj])


def directory(base, relative):
    """Create a directory below base."""
    Path(base, relative).mkdir(parents=True, exist_ok=True)


def copy(relative, destination):
    """Copy a repository file into the staging tree."""
    shutil.copy(str(Path(SRC, relative)), destination)


def run(command):
    """Run one build command from SRC, raising BuildError on failure."""
    # clang's -fuse-ld=lld looks up ld.lld on PATH, and LLVM is often not on
    # PATH on a plain Windows host.
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join(
        [part for part in (llvm_bin(), env.get("PATH")) if part])
    result = subprocess.run([str(part) for part in command], cwd=SRC, env=env,
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace", check=False)
    if result.returncode == 0:
        return

    raise BuildError(
        f"command failed ({result.returncode}): {' '.join(str(c) for c in command)}\n"
        f"{result.stdout}\n{result.stderr}")


def clean():
    """Remove the build and out trees."""
    for directory_name in ("build", "out"):
        shutil.rmtree(Path(SRC, directory_name), ignore_errors=True)
    print("native build: clean")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
