# glibc 2.44 port tree (x86_64-zhaoxu)

This directory holds only the Zhaoxu-specific port files; the glibc source
itself stays pristine. `tools/glibc.py` copies `sysdeps/` from here into the
build tree, patches `scripts/config.sub` for the `zhaoxu` OS name, and runs
configure with the stage-1 cross compiler from `tools/gcc.py`.

The port follows `docs/superpowers/plans/2026-11-26-glibc-port.md`:
static first, then the dynamic loader.  Build gate 1 is a configure run plus
`libc.a`; gate 2 is `user/glibc_hello.c` booting on `out/target.img`.
