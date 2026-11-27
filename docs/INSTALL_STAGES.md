# Installation stages

The installation follows the Windows XP i386 three-stage model. Stage 1 is
implemented; stages 2 and 3 are specified here and not built yet. Nothing in
this file claims more than the code does.

## Stage 1: text-mode setup (implemented)

The ISO's only Limine entry passes `cmdline: install`. The kernel starts
`/bin/drivers.udrv` for keyboard input and then `/bin/apps/install.zxe`, which
owns the console. There is no display service and no compositor.

- Colours: light grey on blue, set through `SYS_CONSOLE_COLOR` (79), the
  XP-style setup screen.
- Keyboard: raw tty mode delivers one key per read. The PS/2 driver maps
  F1–F12 to `0xF1`–`0xFC`, arrows/Home/End/Page keys to `0x81`–`0x88`, and
  Delete to `0x7F`.
- Flow: welcome (ENTER / F3), licence (F8 / ESC), disk list (Up/Down or digit,
  ENTER, R rescan, F3 quit), layout (T GPT/MBR, D data partition, ENTER),
  format warning (F), progress from `SYS_INSTALL_PROGRESS`, done (ENTER
  reboots through `SYS_REBOOT`).
- Stage 1 writes the partition table, the FAT32 ESP (`/boot/limine` holds
  `KERNEL.ELF`, `MiSans.ttf`, `WALL.RGB` and the Limine files) and formats the
  ext4 root through the `/bin/ext4fs.udrv` service.

## Stage 2: GUI phase from the installed disk (not implemented)

The XP model: after stage 1 copies files, the machine reboots from the hard
disk and a graphical phase finishes the installation, reading its payload
from the CD.

- Boot the installed system, not the CD.
- Enter the GUI phase instead of the desktop: the kernel reads the pending
  state marker and starts the setup GUI (ADE client) rather than `/bin/aqua`'s
  desktop session.
- Read from the CD only: the stage 2 app pulls remaining payload through the
  ISO9660 path (`kernel/fs/iso9660.c`), i.e. the medium supplies files but
  does not boot.
- Clear the marker when the phase finishes so the next boot is a normal
  desktop boot.

Not implemented. Missing pieces: a writable state marker the installed kernel
can read at boot (ESP file written by stage 1, or a file in the ext4 root),
runtime CD mounting for the stage 2 app, and the GUI phase application.

## Stage 3: OOBE before the first desktop (not implemented)

The first boot after stage 2 runs the out-of-box experience before any desktop:

- user name and password, hostname, timezone and locale, optional network;
- writes the answers into `/etc` on the installed root;
- clears the OOBE marker, then starts Aqua Desktop Environment normally.

Not implemented. It depends on stage 2's marker mechanism and on a writable
`/etc` on the installed root.

## State markers (design)

Both stages need one durable flag that survives reboots and is readable by the
installed kernel:

- `setup.stage` on the ESP root (FAT32, writable by the kernel through
  `kernel/fs/fat32_write.c`): value `1` while stage 2 is pending, `2` while
  OOBE is pending, absent for a finished system.
- Stage 1 writes `1`; the stage 2 app writes `2`; the OOBE writes nothing
  (file removed).

The ESP layout example in `CLAUDE.md` does not list this file, so it is not
written until stages 2 and 3 exist; the design is recorded here so the two
stages and the media layout do not drift apart.
