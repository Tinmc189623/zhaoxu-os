#!/usr/bin/env python3
"""Validate an Arbor package (.arb) and print its digest for arbor.toml.

usage: arb-check.py <package.arb> [...]

Checks the archive against docs/ARBOR.md: one <name>-<version>/ root, a
pkg.toml with the required keys, matching name/version, a payload/ tree, and
no path escapes.  Exits 1 when any package is invalid.
"""

import hashlib
import sys
import tarfile
import tomllib
from pathlib import PurePosixPath

REQUIRED = ("name", "version", "description", "license", "arch")


def check(path) -> list[str]:
    """Return the problems in one package; an empty list means valid."""
    problems = []
    try:
        archive = tarfile.open(path, "r")
    except (OSError, tarfile.TarError) as error:
        return [f"{path}: {error}"]

    with archive:
        names = archive.getnames()
        roots = {PurePosixPath(name).parts[0] for name in names if name}
        if len(roots) != 1:
            problems.append(f"{path}: expected one root directory, got {roots}")
            root = ""
        else:
            root = roots.pop()
        for name in names:
            if ".." in PurePosixPath(name).parts:
                problems.append(f"{path}: unsafe path {name}")

        manifest = f"{root}/pkg.toml" if root else ""
        if manifest not in names:
            problems.append(f"{path}: missing {manifest or 'pkg.toml'}")
            return problems
        with archive.extractfile(manifest) as stream:
            try:
                data = tomllib.load(stream)
            except tomllib.TOMLDecodeError as error:
                return problems + [f"{path}: pkg.toml: {error}"]

        package = data.get("package", {})
        for key in REQUIRED:
            if not package.get(key):
                problems.append(f"{path}: pkg.toml has no package.{key}")
        expected = f"{package.get('name')}-{package.get('version')}"
        if root and root != expected:
            problems.append(f"{path}: root {root} != {expected}")
        if not any(name.startswith(f"{root}/payload/") for name in names):
            problems.append(f"{path}: payload/ is empty")

    digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
    print(f"arb    : {path} sha256 {digest}")
    return problems


def main(argv: list[str]) -> int:
    """Validate every archive named on the command line."""
    if len(argv) < 2:
        raise SystemExit("usage: arb-check.py <package.arb> [...]")
    failed = 0
    for argument in argv[1:]:
        problems = check(argument)
        if problems:
            failed += 1
            for problem in problems:
                print(problem)
        else:
            print(f"arb    : {argument} OK")
    print(f"arb    : {len(argv) - 1 - failed}/{len(argv) - 1} package(s) valid")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
