#!/usr/bin/env ruby
# ext4-test.rb -- host matrix test for the OS-independent ext4 core
# (src/fs/ext4), run before the core moves into Ring3.
#
#   Phase A  our own formatter.
#     A1  32 MiB volume: write, cat, contig; a second process mounts again and
#         reads the contiguous file back byte for byte.
#     A2  rm + sync.
#     A3  a fresh process mounts again: HELLO.TXT must be gone from the
#         directory and from lookup, KEEP.TXT must still be there and still
#         read back intact.  This is what proves the unlink reached the disk.
#     A4  e2fsck -fn must be clean in its output text as well as its exit
#         status: e2fsprogs 1.47 returns 0 while still printing warnings
#         (see .superpowers/sdd/2026-11-11-ext4-primary-m0/task-1-report.md).
#     A5  the same volume mounted at a non-zero base (the installer never
#         puts ext4 at LBA 0).
#     A6  256 MiB volume = two block groups.  Group 0 is filled exactly and
#         the next allocation must land in group 1; group 1's backup
#         superblock/group descriptors must match the primary copies.
#   Phase B  mke2fs matrix (1K/2K/4K blocks x feature sets), read-only.
#            Each image gets one real file written by debugfs, and the file
#            must be listed, stat'ed and read back byte for byte.  Write
#            interop with foreign filesystems is M2, not M0.
#
# Runs on Windows Ruby (WSL has no ruby) and shells out to WSL, which owns
# gcc, mke2fs, e2fsck and debugfs.
#
#   cd <repo>
#   ruby src/tools/ext4-test.rb
#
# Environment: EXT4_TEST_DISTRO (default Ubuntu-26.04).

require "fileutils"
require "open3"

DISTRO = ENV["EXT4_TEST_DISTRO"] || "Ubuntu-26.04"
WIN_SRC = File.expand_path("..", __dir__)
SRC = if WIN_SRC =~ /\A([A-Za-z]):[\\\/](.*)\z/
        "/mnt/#{Regexp.last_match(1).downcase}/#{Regexp.last_match(2).gsub('\\', '/')}"
      else
        abort "ext4-test.rb: expected #{WIN_SRC} under a drive letter"
      end
BIN = "#{SRC}/build/ext4-test/ext4-test"
WORK = "/tmp/zos-ext4-test"
BLOCK = 4096 # ext4_format_volume always formats 4096-byte blocks
GROUP_BLOCKS = 32_768 # EXT4_BPG in ext4_format.c
MAX_FILE_BLOCKS = 16 * 1024 * 1024 / BLOCK # EXT4_MAX_FILE_SIZE in ext4.h

# e2fsck prints these when it is unhappy, whatever its exit status says.
BAD_FSCK = ["WARNING", "Fix?", "corrupt", "orphan", "still has errors"].freeze

def wsl(script)
  # The script goes in on stdin: wsl.exe mangles argv quoting (the repository
  # path contains a space) and `bash -s` sidesteps that completely.  WSL_UTF8
  # makes wsl.exe emit UTF-8 instead of UTF-16LE for its own notices.
  out, status = Open3.capture2e({ "WSL_UTF8" => "1" }, "wsl", "-d", DISTRO,
                                "--", "bash", "-ls", stdin_data: script)
  # wsl.exe prints a localhost-proxy notice on stderr for every call; drop
  # those lines so they cannot be mistaken for harness output.
  text = out.dup.force_encoding(Encoding::UTF_8).scrub
  [text.lines.reject { |line| line.start_with?("wsl: ") }.join, status.success?]
end

$failures = 0

def check(name, ok, out = nil, detail = nil)
  suffix = detail ? " (#{detail})" : ""
  if ok
    puts "PASS #{name}#{suffix}"
  else
    $failures += 1
    puts "FAIL #{name}#{suffix}"
    puts out if out && !out.empty?
  end
  ok
end

def fsck(name, image)
  out, ok = wsl("e2fsck -fn #{image}")
  hits = BAD_FSCK.select { |word| out.include?(word) }
  summary = out.lines.map(&:strip).find { |l| l =~ /\A\S+: \d+\/\d+ files/ }
  check(name, ok && hits.empty?, out,
        "exit_ok=#{ok} warnings=#{hits.inspect} #{summary}")
end

# Fixtures that only Ruby can make: byte ramps (k & 0xFF), which is what the
# harness's `contig` command writes.  They land under build/ so WSL sees them
# through the same 9p mount as the rest of the tree.
FIXTURES_WSL = "#{SRC}/build/ext4-test/fixtures"
FIXTURES_WIN = File.join(WIN_SRC, "build", "ext4-test", "fixtures")

def ramp_fixture(bytes)
  name = "ramp#{bytes}.bin"
  path = File.join(FIXTURES_WIN, name)
  unless File.size?(path) == bytes
    File.binwrite(path, Array.new(bytes) { |k| k & 0xFF }.pack("C*"))
  end
  "#{FIXTURES_WSL}/#{name}"
end

# ---------------------------------------------------------------- build ----
build = <<~SH
  cd "#{SRC}" || exit 1
  mkdir -p build/ext4-test
  gcc -std=gnu11 -O1 -g -fsanitize=address,undefined \
      -fno-sanitize-recover=all -Wall -Wextra \
      -I fs/ext4 -I tests/ext4/include -DEXT4_HOST \
      -o build/ext4-test/ext4-test \
      tests/ext4/host_main.c tests/ext4/host_io.c \
      fs/ext4/ext4_env.c fs/ext4/ext4_mount.c fs/ext4/ext4_extent.c \
      fs/ext4/ext4_dir.c fs/ext4/ext4_read.c fs/ext4/ext4_alloc.c \
      fs/ext4/ext4_write.c fs/ext4/ext4_format.c -lrt
SH
out, ok = wsl(build)
unless ok
  warn "BUILD FAILED:\n#{out}"
  exit 1
end
puts "PASS build host harness (#{BIN})"

# --------------------------------------------------------------- fixtures ---
fixtures = <<~SH
  set -e
  rm -rf #{WORK}
  mkdir -p #{WORK}
  yes abcdefghijklmnopqrstuvwxyz | tr -d '\\n' | head -c 70000 \
      > #{WORK}/alphabet70000.txt
  yes abcdefghijklmnopqrstuvwxyz | tr -d '\\n' | head -c 16777216 \
      > #{WORK}/alphabet16m.bin
  head -c 5000 #{WORK}/alphabet70000.txt > #{WORK}/payload.bin
  head -c 3000 #{WORK}/alphabet70000.txt > #{WORK}/payload3000.bin
SH
out, ok = wsl(fixtures)
unless ok
  warn "FIXTURE SETUP FAILED:\n#{out}"
  exit 1
end

FileUtils.mkdir_p(FIXTURES_WIN)

# ------------------------------------------------- phase A: our formatter ---
own = "#{WORK}/own.img"

# A1: format, two files, full read-back of the big one, one contig file, and a
# second process that reads the contig file back and compares every byte with
# the k & 0xFF ramp the harness wrote.
ramp50000 = ramp_fixture(50_000)
script = <<~SH
  set -e
  dd if=/dev/zero of=#{own} bs=1M count=32 status=none
  cd "#{SRC}"
  "#{BIN}" #{own} format 65536
  "#{BIN}" #{own} mount 0 write /HELLO.TXT 70000 write /KEEP.TXT 3000 \
      stat /HELLO.TXT stat /
  "#{BIN}" #{own} mount 0 cat /HELLO.TXT | cmp - "#{WORK}/alphabet70000.txt"
  "#{BIN}" #{own} mount 0 contig /CONTIG.BIN 50000
  "#{BIN}" #{own} mount 0 cat /CONTIG.BIN | cmp - "#{ramp50000}"
  "#{BIN}" #{own} mount 0 stat /CONTIG.BIN
SH
out, ok = wsl(script)
match = out.match(/contig (\d+) (\d+)/)
contig = match ? [match[1].to_i, match[2].to_i] : nil
contig_ok = contig && contig[0] > 0 && contig[1] * BLOCK >= 50_000
check("own-format format+write+cat+contig",
      ok && out.include?("f 70000") && out.include?("d 4096") &&
        out.include?("f 50000") && contig_ok,
      out, "contig=#{contig.inspect}")

# A2: unlink and sync, nothing else.
script = <<~SH
  set -e
  cd "#{SRC}"
  "#{BIN}" #{own} mount 0 rm /HELLO.TXT sync
SH
out, ok = wsl(script)
check("own-format unlink+sync", ok, out)

# A3: a *fresh process* mounts the image again.  The removal and the surviving
# file have to be what is on the disk, not what was left in memory.  The
# negative tests are `if`/`exit`, because `!` and `&&` lists are exempt from
# `set -e` and would not gate the run.
script = <<~SH
  set -e
  cd "#{SRC}"
  "#{BIN}" #{own} mount 0 ls / > #{WORK}/after-rm.lst
  cat #{WORK}/after-rm.lst
  if grep -q 'HELLO.TXT' #{WORK}/after-rm.lst; then
      echo "HELLO.TXT is still listed after rm+sync and a fresh mount"; exit 91
  fi
  if "#{BIN}" #{own} mount 0 stat /HELLO.TXT; then
      echo "HELLO.TXT still resolves after rm+sync and a fresh mount"; exit 92
  fi
  grep -q 'f 3000 KEEP.TXT' #{WORK}/after-rm.lst
  grep -q 'f 50000 CONTIG.BIN' #{WORK}/after-rm.lst
  grep -q 'd 4096 lost+found' #{WORK}/after-rm.lst
  "#{BIN}" #{own} mount 0 cat /KEEP.TXT | cmp - "#{WORK}/payload3000.bin"
SH
out, ok = wsl(script)
listing_ok = ok &&
             out.include?("f 3000 KEEP.TXT") &&
             out.include?("f 50000 CONTIG.BIN") &&
             out.include?("d 4096 lost+found") &&
             !out.include?("HELLO.TXT")
check("own-format unlink survives a fresh mount", listing_ok, out)

# A4: e2fsck over the image the whole phase just rewrote.
fsck("own-format e2fsck -fn", own)

# A5: the same volume mounted at a non-zero base (the installer's layout:
# ext4 does not start at LBA 0, so volume_base must be honoured).
offset = "#{WORK}/offset.img"
base = 4 * 1024 * 1024
script = <<~SH
  set -e
  dd if=/dev/zero of=#{offset} bs=1M count=40 status=none
  dd if=#{own} of=#{offset} bs=1M seek=4 conv=notrunc status=none
  cd "#{SRC}"
  "#{BIN}" #{offset} mount #{base} cat /KEEP.TXT > #{WORK}/base.txt
  cmp #{WORK}/payload3000.bin #{WORK}/base.txt
  "#{BIN}" #{offset} mount #{base} ls /
SH
out, ok = wsl(script)
check("own-format mount at base #{base}", ok && out.include?("f 3000 KEEP.TXT"),
      out)

# ------------------------------------------------- A6: two block groups ---
# 256 MiB is two 128 MiB block groups (EXT4_BPG = 32768 4 KiB blocks).  Group 0
# holds the superblock, the GDT, both bitmaps, the 512-block inode table and
# the root/lost+found blocks: 518 blocks used, 32250 free.  The core caps one
# file at 16 MiB (4096 blocks), so seven full files plus a 3578-block tail fill
# the group exactly and the next run has to be allocated in group 1.
MULTI_SECTORS = 524_288
MULTI_BYTES = MULTI_SECTORS * 512
GROUP_META = 1 + 1 + 2 + 512 # super + GDT + both bitmaps + inode table
GROUP0_FREE = GROUP_BLOCKS - GROUP_META - 2 # minus root and lost+found
multi = "#{WORK}/multi.img"
fill = []
remaining = GROUP0_FREE
while remaining > 0
  blocks = [remaining, MAX_FILE_BLOCKS].min
  fill << { path: "/FILL#{fill.size + 1}.BIN", bytes: blocks * BLOCK }
  remaining -= blocks
end
tail = fill.last
group1_bytes = 8 * BLOCK
group1_blocks = group1_bytes / BLOCK
ramp_group1 = ramp_fixture(group1_bytes)

write_cmds = fill.map { |f| "write #{f[:path]} #{f[:bytes]}" }.join(" ")
script = <<~SH
  set -e
  truncate -s #{MULTI_BYTES} #{multi}
  cd "#{SRC}"
  "#{BIN}" #{multi} format #{MULTI_SECTORS}
  "#{BIN}" #{multi} mount 0 #{write_cmds} contig /GROUP1.BIN #{group1_bytes} ls /
SH
out, ok = wsl(script)
match = out.match(/contig (\d+) (\d+)/)
group1 = match ? [match[1].to_i, match[2].to_i] : nil
# The group-1 file must start inside group 1 (block >= EXT4_BPG): that is only
# possible if group 0 really was full and the backup superblock/GDT of group 1
# were skipped as allocated.
group1_ok = group1 && group1[0] >= GROUP_BLOCKS &&
            group1[1] * BLOCK >= group1_bytes
check("multi-group format+fill+group-1 alloc",
      ok && group1_ok &&
        out.include?("f #{fill.first[:bytes]} FILL1.BIN") &&
        out.include?("f #{tail[:bytes]} #{File.basename(tail[:path])}") &&
        out.include?("f #{group1_bytes} GROUP1.BIN") &&
        out.include?("d 4096 lost+found"),
      out,
      "contig=#{group1.inspect} fill=#{fill.map { |f| f[:bytes] }.inspect}")

# Group 1's backup metadata: the superblock copy starts at the first byte of
# block 32768 (only group 0 keeps its superblock at +1024, behind the boot
# sector) and the group descriptors follow at block 32769.  Both must match the
# primary copies apart from s_block_group_nr.
script = <<~SH
  set -e
  u16() { od -An -tu2 -j "$2" -N 2 "$1" | tr -d ' \\n'; }
  u32() { od -An -tu4 -j "$2" -N 4 "$1" | tr -d ' \\n'; }
  dd if=#{multi} bs=#{BLOCK} skip=0 count=1 status=none | tail -c +1025 | head -c 1024 > #{WORK}/primary.sb
  dd if=#{multi} bs=#{BLOCK} skip=#{GROUP_BLOCKS} count=1 status=none | head -c 1024 > #{WORK}/group1.sb
  [ "$(u16 #{WORK}/group1.sb 56)" = "61267" ]
  [ "$(u16 #{WORK}/group1.sb 90)" = "1" ]
  [ "$(u32 #{WORK}/group1.sb 4)" = "#{MULTI_BYTES / BLOCK}" ]
  [ "$(u32 #{WORK}/group1.sb 32)" = "#{GROUP_BLOCKS}" ]
  [ "$(u32 #{WORK}/group1.sb 12)" = "$(u32 #{WORK}/primary.sb 12)" ]
  [ "$(u32 #{WORK}/group1.sb 16)" = "$(u32 #{WORK}/primary.sb 16)" ]
  dd if=#{multi} bs=#{BLOCK} skip=1 count=1 status=none | head -c 64 > #{WORK}/primary.gdt
  dd if=#{multi} bs=#{BLOCK} skip=#{GROUP_BLOCKS + 1} count=1 status=none | head -c 64 > #{WORK}/group1.gdt
  cmp #{WORK}/primary.gdt #{WORK}/group1.gdt
  # bg_free_blocks_count (0x0C of each 32-byte descriptor): group 0 must be
  # full to the last block and group 1 must have lost exactly the run we asked
  # for, which is what "the allocation moved to group 1" means on disk.
  [ "$(u16 #{WORK}/primary.gdt 12)" = "0" ]
  [ "$(u16 #{WORK}/group1.gdt 44)" = "#{GROUP_BLOCKS - GROUP_META - group1_blocks}" ]
SH
out, ok = wsl(script)
check("multi-group backup superblock+GDT+free counts", ok, out)

# Read the group-0 boundaries and the group-1 file back from separate mounts.
script = <<~SH
  set -e
  cd "#{SRC}"
  head -c #{tail[:bytes]} #{WORK}/alphabet16m.bin > #{WORK}/alphabet-tail.bin
  "#{BIN}" #{multi} mount 0 cat #{fill.first[:path]} | cmp - "#{WORK}/alphabet16m.bin"
  "#{BIN}" #{multi} mount 0 cat #{tail[:path]} | cmp - "#{WORK}/alphabet-tail.bin"
  "#{BIN}" #{multi} mount 0 cat /GROUP1.BIN | cmp - "#{ramp_group1}"
  "#{BIN}" #{multi} mount 0 stat /GROUP1.BIN
SH
out, ok = wsl(script)
check("multi-group read-back", ok && out.include?("f #{group1_bytes}"), out)

fsck("multi-group e2fsck -fn", multi)

# --------------------------------------------- phase B: mke2fs read-only ---
cases = []
[1024, 2048, 4096].each do |bs|
  [[], ["-O 64bit"], ["-O metadata_csum"], ["-O ^has_journal"]].each do |opt|
    cases << { block: bs, opts: opt }
  end
end

cases.each do |c|
  tag = c[:opts].empty? ? "default" : c[:opts].first.tr(" ^-", "")
  image = "#{WORK}/mkfs-#{c[:block]}-#{tag}.img"
  script = <<~SH
    set -e
    rm -f #{image}
    dd if=/dev/zero of=#{image} bs=1M count=24 status=none
    mke2fs -q -F -t ext4 -b #{c[:block]} #{c[:opts].join(' ')} #{image}
    debugfs -w -R "write #{WORK}/payload.bin /HELLO.TXT" #{image} >/dev/null
    debugfs -R "stat /HELLO.TXT" #{image} 2>/dev/null | grep -q '^Inode:' || {
      echo "debugfs did not create /HELLO.TXT in #{image}"; exit 90; }
    cd "#{SRC}"
    "#{BIN}" #{image} mount 0 ls / stat /HELLO.TXT
    "#{BIN}" #{image} mount 0 cat /HELLO.TXT > #{WORK}/read-back.bin
    cmp #{WORK}/payload.bin #{WORK}/read-back.bin
  SH
  out, ok = wsl(script)
  name = "read-only block=#{c[:block]} opts=#{c[:opts].inspect}"
  check(name,
        ok && out.include?("f 5000 HELLO.TXT") &&
          out =~ /^f 5000$/ && out =~ /^d \d+ lost\+found$/,
        out)
end

# ------------------------------------------------------------------ done ---
if $failures.zero?
  puts "RESULT PASS"
  exit 0
end
warn "RESULT FAIL (#{$failures} failing case(s))"
exit 1
