#!/usr/bin/env ruby
# smoke.rb -- boot Zhaoxu OS headless in QEMU (inside WSL), drive the serial
# shell interactively and assert a marker in the console output.
#
#   usage: ruby tools/smoke.rb <marker> [commands]
#     commands: newline-separated shell input, literal "\n" allowed;
#               "shutdown" is always appended automatically.
#   env:   SMOKE_DISTRO SMOKE_ROOT SMOKE_TIMEOUT (seconds, default 180)
#          SMOKE_FW: uefi (default, OVMF pflash) or bios (SeaBIOS, no pflash)
#          SMOKE_BOOT: iso (default, CD only, no extra disks)
#                      iso-install (CD + one blank out/target.img)
#                      installed (boot out/target.img alone)
#          bios firmware supports iso, iso-install and installed.
#
# Tests boot the ISO only; installation is exercised by `install` in csh or
# by booting the installed target afterwards.
#
# Run from Windows Ruby (WSL has no ruby): it shells out through wsl.exe and
# feeds one command line at a time once the guest shows a prompt, so the
# 16-byte UART FIFO never overflows.

require "open3"

DISTRO = ENV["SMOKE_DISTRO"] || "Ubuntu-26.04"
win_root = ENV["SMOKE_ROOT"] || File.expand_path("..", __dir__)
ROOT = win_root =~ /\A([A-Za-z]):[\\\/](.*)\z/ ?
       "/mnt/#{Regexp.last_match(1).downcase}/#{Regexp.last_match(2).gsub('\\', '/')}" :
       win_root
TIMEOUT = (ENV["SMOKE_TIMEOUT"] || 180).to_i

marker = ARGV[0] or abort "usage: smoke.rb <marker> [commands]"
commands = (ARGV[1] || "").gsub('\n', "\n").split("\n").reject(&:empty?)

target_disk = [
  "-drive if=none,id=zdst,format=raw,file=out/target.img",
  "-device ide-hd,drive=zdst,bus=ide.1",
]
# Rule 11: ISO images are named zhaoxu_{version}_installer.iso.
iso_file = Dir[File.join(win_root, "out", "zhaoxu_*_installer.iso")]
           .max_by { |f| File.mtime(f) }
iso_drives = [
  "-drive if=none,id=zcd,format=raw,media=cdrom,readonly=on," \
  "file=out/#{iso_file ? File.basename(iso_file) : "zhaoxu_0.5.0_installer.iso"}",
  "-device ide-cd,drive=zcd,bus=ide.0",
]
boot = ENV["SMOKE_BOOT"] || "iso"
fw = ENV["SMOKE_FW"] || "uefi"
if boot.start_with?("iso") && !iso_file
  abort "out/zhaoxu_*_installer.iso not found; run `make iso` first"
end
drives = case boot
         when "iso"
           iso_drives
         when "iso-install"
           [*iso_drives, *target_disk]
         when "installed"
           [
             "-drive if=none,id=zdst,format=raw,file=out/target.img",
             "-device ide-hd,drive=zdst,bus=ide.0",
           ]
         else
           abort "SMOKE_BOOT must be iso, iso-install or installed"
         end

case fw
when "uefi"
  firmware = [
    "-drive if=pflash,format=raw,unit=0,readonly=on," \
    "file=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}",
    "-drive if=pflash,format=raw,unit=1,file=out/OVMF_VARS.fd",
  ]
  boot_order = boot.start_with?("iso") ? ["-boot", "order=d"] : []
when "bios"
  unless %w[iso iso-install installed].include?(boot)
    abort "SMOKE_FW=bios supports only iso, iso-install and installed"
  end
  firmware = []
  boot_order = ["-boot", boot == "installed" ? "order=c" : "order=d"]
else
  abort "SMOKE_FW must be uefi or bios"
end

inner = +"cd \"#{ROOT}\" || exit 1\n"
if fw == "uefi"
  inner << %[cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" out/OVMF_VARS.fd\n]
end
qemu = [
  "timeout #{TIMEOUT} qemu-system-x86_64",
  "-M ${QEMU_MACHINE:-q35} -m ${QEMU_MEM:-512M} -smp 1",
  *firmware,
  *drives,
  *boot_order,
  "-net none -serial stdio -monitor none -display none -no-reboot",
]
inner << qemu.map { |l| "  #{l} \\\\" }.join("\n").sub(/ \\\\\z/, "") << "\n"

out = String.new(encoding: Encoding::BINARY)
$stdout.binmode
$stderr.binmode
ok = false
Open3.popen3("wsl", "-d", DISTRO, "--", "bash", "-lc", inner) do |stdin, stdout, stderr, waiter|
  stdin.binmode
  stdout.binmode
  stderr.binmode
  deadline = Time.now + TIMEOUT
  queue = commands + ["shutdown"]
  sent = 0
  last_send = Time.now
  eof_count = 0
  loop do
    left = deadline - Time.now
    break if left <= 0
    begin
      readable = IO.select([stdout, stderr], nil, nil, [left, 0.25].min)
    rescue IOError
      break
    end
    if readable
      readable[0].each do |io|
        begin
          chunk = io.read_nonblock(4096)
          out << chunk if chunk
        rescue Errno::EAGAIN, IO::EAGAINWaitReadable
        rescue EOFError
          eof_count += 1
        end
      end
    end
    if out.include?(marker)
      ok = true
      break
    end
    break if eof_count >= 2 && sent >= queue.size
    prompts = out.scan(/zhaoxu>|edit>/).size
    if sent < queue.size && prompts > sent && Time.now - last_send > 0.8
      stdin.write(queue[sent].dup.force_encoding(Encoding::BINARY) + "\n")
      stdin.flush
      sent += 1
      last_send = Time.now
    end
  end
  begin
    waiter.kill("KILL")
  rescue StandardError
  end
end

system("wsl", "-d", DISTRO, "--", "pkill", "-f", "qemu-system-x86_64",
       out: File::NULL, err: File::NULL)

if ok
  puts "SMOKE PASS: #{marker.inspect}"
  puts "--- last 6000 bytes ---"
  puts out[-6000..] || out if ENV["SMOKE_DUMP"]
  exit 0
else
  warn "SMOKE FAIL: #{marker.inspect}"
  warn "--- last 3000 bytes ---"
  warn out[-3000..] || out
  exit 1
end
