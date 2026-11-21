#!/usr/bin/env python3
"""Unit tests for tools/host.py (the cross-platform build host detector).

Run with: python3 tests/tools/host_test.py

No compiler or QEMU needed: the tests pin platform strings and environment
instead of relying on the machine that runs them.
"""

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))
import host  # noqa: E402

FAILURES = 0


def check(name):
    """Run one test function and report its result."""
    def decorator(function):
        global FAILURES
        try:
            function()
            print(f"ok   {name}")
        except Exception as error:  # noqa: BLE001 - report every failure
            FAILURES += 1
            print(f"FAIL {name}: {error}")
        return function
    return decorator


def assert_true(condition, message="assertion failed"):
    """Minimal assertion helper."""
    if not condition:
        raise AssertionError(message)


@check("detect native Linux")
def _():
    assert_true(host.detect(platform="x86_64-linux", env={},
                            proc_version="Linux version 6.8.0") == "linux")


@check("detect WSL by environment")
def _():
    assert_true(host.detect(platform="x86_64-linux",
                            env={"WSL_DISTRO_NAME": "Ubuntu-26.04"},
                            proc_version="Linux version 5.15") == "wsl")


@check("detect WSL by /proc/version")
def _():
    assert_true(host.detect(
        platform="x86_64-linux", env={},
        proc_version="Linux 5.15.90.1-microsoft-standard-WSL2") == "wsl")


@check("detect macOS")
def _():
    assert_true(host.detect(platform="arm64-darwin23", env={}) == "macos")


@check("detect native Windows")
def _():
    assert_true(host.detect(platform="x64-mingw-ucrt", env={}) == "windows")
    assert_true(host.detect(platform="i386-mingw32", env={}) == "windows")


@check("reject unsupported platform")
def _():
    try:
        host.detect(platform="wasm32-unknown-emscripten", env={})
    except host.ZosHostError:
        return
    raise AssertionError("expected ZosHostError")


@check("windows_ruby")
def _():
    assert_true(host.windows_ruby("x64-mingw-ucrt"))
    assert_true(not host.windows_ruby("x86_64-linux"))
    assert_true(not host.windows_ruby("arm64-darwin23"))


@check("to_posix_path converts drive letters")
def _():
    converted = host.to_posix_path("C:\\Users\\dev\\src")
    assert_true(converted.endswith("/c/Users/dev/src"), converted)
    assert_true(host.to_posix_path("C:/Ruby33-x64/bin/ruby.exe")
                == "/c/Ruby33-x64/bin/ruby.exe")


@check("shell_quote escapes single quotes")
def _():
    assert_true(host.shell_quote("a'b") == "'a'\"'\"'b'")
    assert_true(host.shell_quote("plain") == "'plain'")
    assert_true(host.shell_quote("") == "''")


@check("find_executable searches extra directories")
def _():
    with tempfile.TemporaryDirectory() as directory:
        name = "zhaoxu-make-test.exe" if host.windows_ruby() else "zhaoxu-make-test"
        target = Path(directory) / name
        target.write_text("")
        found = host.find_executable("zhaoxu-make-test", directory,
                                     env={"PATH": ""})
        assert_true(found, f"not found in {directory}")
        assert_true(Path(found).resolve() == target.resolve())


@check("find_executable returns None for a missing tool")
def _():
    assert_true(host.find_executable("zhaoxu-definitely-missing",
                                     env={"PATH": ""}) is None)


@check("ovmf finds a code/vars pair")
def _():
    with tempfile.TemporaryDirectory() as directory:
        (Path(directory) / "OVMF_CODE.fd").write_text("")
        (Path(directory) / "OVMF_VARS.fd").write_text("")
        code = host.first_file([directory], host.OVMF_CODE_NAMES)
        vars_ = host.first_file([directory], host.OVMF_VARS_NAMES)
        assert_true(code and vars_, "OVMF pair not found")
        assert_true(Path(code).name == "OVMF_CODE.fd")
        assert_true(Path(vars_).name == "OVMF_VARS.fd")


@check("make_assignment skips empty values")
def _():
    assert_true(host.make_assignment("CC", "clang") == "CC='clang'")
    assert_true(host.make_assignment("CC", None) is None)
    assert_true(host.make_assignment("CC", "") is None)


print("PASS" if FAILURES == 0 else f"{FAILURES} failure(s)")
sys.exit(0 if FAILURES == 0 else 1)
