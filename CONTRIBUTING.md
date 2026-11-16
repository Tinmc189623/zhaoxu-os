# Contributing to Zhaoxu OS

Thanks for helping. This repository has stricter rules than a typical project;
read `RULES.md` v1.96 first. `AGENTS.md` describes the invariants and
`CLAUDE.md` gives the build and architecture tour. If a contribution conflicts
with a rule, the rule wins.

## Ground rules

- **Code must be AI-generated.** Human-written code is not accepted (rule 6).
  Reviewers will still read it line by line; generating code does not transfer
  responsibility to the tool that produced it.
- **All repository scripts are Ruby** (rule 6). Do not add shell, Python, or
  JavaScript build scripts. The Makefile is the build system; Ruby only
  handles auxiliary tools under `src/tools/`.
- **One file does one job** (rules 12, 23, 29). Do not add catch-all files or
  extend an existing catch-all with an unrelated responsibility.
- **No placeholders and no simulated results.** A change is not done because
  it compiles; attach a serial log, QEMU screendump, or test output from a real
  run.
- **Third-party code is used as shipped** (rule 39). Vendor pinned sources,
  do not patch them in-tree, and do not present a self-developed component as
  an upstream port (rule 42).
- **Names are contractual.** Write Yuanshuo, Zhaoxu OS, Limine, Aqua Desktop
  Environment, Desktop Window Compositor, Desktop Window Manager, ZAF, and
  Z64 exactly. Always write the brand `Nexsteaduser` in one word and use
  `admin@nexsteaduser.com` for project mail.
- The current default shell is 8sh, which is transitional. `RULES.md` rule 1
  requires bash and forbids further self-developed shell work; do not add new
  shell features to 8sh. Port bash instead.

## Building

Build on Linux, macOS, or WSL2. WSL2 is the preferred Windows path:

```bash
cd src
make -j4                 # kernel, user programs and initramfs
make iso                 # hybrid Limine ISO
make run-iso             # UEFI QEMU
make host-test           # Ruby host-detection tests
```

A clean ISO build needs `xorriso`, `mtools`, `dosfstools`, `qemu`,
OVMF/SeaBIOS firmware, `ffmpeg`, and the cross toolchain described in
`CLAUDE.md`. A newlib/ncurses/nano build additionally needs:

```bash
ruby tools/newlib.rb
ruby tools/posix-ports.rb
```

Do not commit `build/`, `out/`, `esp/`, generated ISOs, or newlib/ncurses
installations.

## Testing before you send a change

There is no general test suite. At minimum:

1. `ruby tests/tools/host_test.rb` prints `PASS`.
2. A clean `make -j4` links, and `nm -u out/kernel.elf` is empty.
3. `nm out/kernel.elf` contains none of `ahci_*`, `acpi_init`, `serial_init`,
   `keyboard_*`, `mouse_*`, `pit_init`, or `pci_*`; drivers belong in Ring3.
4. Boot the ISO in QEMU and reach Aqua Desktop Environment, or 8sh on a
   headless build. Keep the serial log.
5. Run the boot test that covers your area (`M0_BOOT_TEST`, `M5_BOOT_TEST`,
   `FT_BOOT_TEST`, `GL_BOOT_TEST`, `VK_BOOT_TEST`, `WLSTOCK_BOOT_TEST`,
   `POSIX_BOOT_TEST`, `NLIBC_BOOT_TEST`, or `FE_BOOT_TEST`) and put the marker
   lines in the pull request.
6. After boot or installer changes, verify both UEFI and Legacy BIOS, and
   install to a blank disk, then boot the installed disk in both modes.

## Code conventions

- Kernel code is freestanding C11: no libc, no SSE/MMX/AVX, no red zone, no
  exceptions or RTTI. Font code is the only SSE exception.
- User programs run in Ring 3 and use Z64 through `user/userlib.h`. Do not
  link drivers or desktop code into `kernel.elf`.
- Add a comment above each function explaining what it does and any invariant
  callers must preserve. Explain why, not just what the next line says.
- Syscall numbers live in both `kernel/proc/syscall.c` and
  `user/userlib.h` and must stay identical. A new syscall needs a handler, a
  user wrapper, a README/AGENTS note, and a regression.
- Keep paths and permissions strict: ZXE pages are W^X; `/bin/ext4fs` is the
  only block-capable process; `/bin/display` is the only holder of scanout and
  BAR capabilities.
- Fonts are limited to MiSans, MiSans Global, and Inter (OpenType). Icons are
  SVG and must rasterize through `kernel/gfx/svg.c`.
- Version numbers change only in `version.cfg`, followed by `make version`.
  Follow rule 22 for when major, minor, or routine builds change.

## Commit and pull request process

- Branch from `main`; do not force-push or rewrite `main`, and do not rewrite
  published history without explicit approval from the project owner.
- One logical change per commit. Use a conventional prefix such as `feat`,
  `fix`, `docs`, `test`, or `chore`, and explain the reason in the body.
- In the pull request, state what changed, which rule or design it serves,
  what you tested, and the exact serial/screenshot evidence. List any
  third-party additions and update `THIRD_PARTY_NOTICES.md`.
- Do not mark a design decision as final on your own. Brand assets, release
  versions, and public pushes need explicit approval.
- Security reports go to **admin@nexsteaduser.com**, not a public issue. See
  `SECURITY.md`.

## License

Contributions are accepted under the Apache License 2.0 in `LICENSE`. By
submitting a pull request you confirm that you have the right to contribute
the code and that it is generated and reviewed under the rules above.

---

Copyright © 2026 Nexsteaduser. All Rights Reserved. Email: admin@nexsteaduser.com
