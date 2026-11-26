# csh as the default shell (rule 1)

Rule 1 requires `csh` as the default shell, with `bash` also present, and both
must be real ports, never self-written.  Today the image ships only
`/bin/bash`, `/etc/passwd` names bash as root's shell, the kernel console
spawns `/bin/bash` (`kernel/main.c`), and `/etc/shells` records that csh is
pending.  That is the remaining rule-1 violation.

## Source

Fedora 45 publishes `tcsh-6.24.16-2.fc45`; the source RPM is fetched and
verified by `tools/fedora-fetch.py` like the btrfs/toolchain packages.  The
pristine upstream `tcsh-6.24.16` tarball from that SRPM is vendored under
`third_party/tcsh` (rule 39: upstream files unmodified; Fedora's patches stay
out unless a portability fix is required and then lives in the port layer).

## Build

`tools/csh.py` follows the proven `tools/bash.py` pattern:

- configure out of tree in `$HOME/csh-build` against the newlib install and
  `user/lib/posix` objects (autoconf rejects the repository's spaces);
- `clang --target=x86_64-unknown-elf`, PIE at RVA 0, `user/elf.ld`, no PIE
  runtime: the same `-nostdlib` link line as bash;
- tcsh needs a termcap implementation for line editing.  Fedora 45's
  `ncurses` SRPM supplies `libtermcap`/`libtinfo`; it is cross-built for
  Zhaoxu the same way (no trimming, rule 39) and linked into the csh image.
  If tcsh's configure rejects the cross build without termcap, the port
  supplies the small `tgetent/tgetstr/tgetflag/tgetnum` surface from the same
  ncurses source rather than inventing one.

## Integration

1. `make out/csh.elf` packs `out/csh.zxe`; the RAMFS stages it as `/bin/csh`.
2. `/etc/passwd` root shell becomes `/bin/csh`; `/etc/shells` lists both
   `/bin/csh` and `/bin/bash`.
3. `kernel/main.c` spawns `/bin/csh` on the console by default and keeps
   `/bin/bash` available; a missing csh falls back to bash rather than
   halting.
4. Existing bash users are unaffected: `exec bash` still works.

## Verification

- `make out/csh.elf`, `tools/zxe-check.py out/csh.zxe`.
- Boot test: the console prompt comes from csh, `echo $version` prints the
  tcsh version, `set path = (/bin /usr/bin)` and a pipeline work.
- `cat /etc/passwd` shows `/bin/csh`; `/etc/shells` lists both shells.
- The image still boots the installer entry (`cmdline: install`) and the
  desktop path.

Copyright © 2026 Nexsteaduser. All Rights Reserved.
