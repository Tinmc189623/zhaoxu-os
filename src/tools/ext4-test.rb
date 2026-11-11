#!/usr/bin/env ruby
# ext4-test.rb -- host matrix test for the OS-independent ext4 core
# (src/fs/ext4), run before the core moves into Ring3.
#
#   Phase A  our own formatter -> write/read/contig/unlink -> e2fsck -fn.
#            e2fsck must be clean in its output text as well as its exit
#            status: e2fsprogs 1.47 returns 0 while still printing warnings
#            (see .superpowers/sdd/2026-11-11-ext4-primary-m0/task-1-report.md).
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
  head -c 5000 #{WORK}/alphabet70000.txt > #{WORK}/payload.bin
  head -c 3000 #{WORK}/alphabet70000.txt > #{WORK}/payload3000.bin
SH
out, ok = wsl(fixtures)
unless ok
  warn "FIXTURE SETUP FAILED:\n#{out}"
  exit 1
end

# ------------------------------------------------- phase A: our formatter ---
own = "#{WORK}/own.img"

# A1: format, two files, full read-back of the big one, one contig file.
script = <<~SH
  set -e
  dd if=/dev/zero of=#{own} bs=1M count=32 status=none
  cd "#{SRC}"
  "#{BIN}" #{own} format 65536
  "#{BIN}" #{own} mount 0 write /HELLO.TXT 70000 write /KEEP.TXT 3000 \
      stat /HELLO.TXT stat /
  "#{BIN}" #{own} mount 0 cat /HELLO.TXT > #{WORK}/hello.txt
  cmp #{WORK}/alphabet70000.txt #{WORK}/hello.txt
  "#{BIN}" #{own} mount 0 contig /CONTIG.BIN 50000
SH
out, ok = wsl(script)
match = out.match(/contig (\d+) (\d+)/)
contig = match ? [match[1].to_i, match[2].to_i] : nil
contig_ok = contig && contig[0] > 0 && contig[1] * BLOCK >= 50000
check("own-format format+write+cat+contig",
      ok && out.include?("f 70000") && out.include?("d 4096") && contig_ok,
      out, "contig=#{contig.inspect}")

# A2: unlink survives a remount, sync, and the listing is what we expect.
script = <<~SH
  set -e
  cd "#{SRC}"
  "#{BIN}" #{own} mount 0 rm /HELLO.TXT sync ls /
SH
out, ok = wsl(script)
listing_ok = ok &&
             out.include?("d 4096 lost+found") &&
             out.include?("f 3000 KEEP.TXT") &&
             out.include?("f 50000 CONTIG.BIN") &&
             !out.include?("HELLO.TXT")
check("own-format unlink+sync+ls", listing_ok, out)

# A3: e2fsck over the image the whole phase just rewrote.
fsck("own-format e2fsck -fn", own)

# A4: the same volume mounted at a non-zero base (the installer's layout:
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
