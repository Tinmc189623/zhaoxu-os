# Arbor package manager (rule 55)

Arbor is Zhaoxu OS's package manager, written in C++. Packages use the `.arb`
suffix. Two TOML files describe a package:

- `arbor.toml` — repository-level index metadata (a feed of packages).
- `pkg.toml` — one package's manifest, inside the `.arb` archive.

Status: the format and host tooling below are implemented; the C++ `arbor`
CLI that installs packages into a running system is not written yet.

## `.arb` archive

A `.arb` is a tar archive (no compression for the first version) containing:

```text
<name>-<version>/
├── pkg.toml          # manifest, required
└── payload/          # files installed under the package root
    └── ...
```

Rules:

- `pkg.toml` name and version must match the directory prefix.
- Every payload path is relative and must not contain `..`.
- The installed tree is `/usr/arbor/<name>/<version>/`; the `arbor` CLI
  links the package's binaries into `/usr/bin` when it is installed.

## `pkg.toml`

```toml
[package]
name = "terminal"
version = "0.1.0"
description = "Aqua Desktop Environment terminal client"
license = "GPL-2.0-only"
arch = "x86-64"

[requires]
# name = ">=version"
```

## `arbor.toml`

```toml
[repository]
name = "zhaoxu-os"
url = "https://example.invalid/zhaoxu-os"

[[package]]
name = "terminal"
version = "0.1.0"
file = "terminal-0.1.0.arb"
sha256 = "<hex digest of the .arb>"
```

## Host tooling

- `src/tools/arb-pack.py` builds a `.arb` from a directory that contains
  `pkg.toml` and `payload/`.
- `src/tools/arb-check.py` validates an existing `.arb`: manifest keys, name
  and version match the directory, no path escapes, sha256 printed for the
  index.

Both are Python because they run on the build host (rule 6); the on-device
`arbor` program is the C++ part of rule 55.
