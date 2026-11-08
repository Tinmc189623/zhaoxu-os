#!/usr/bin/env ruby
# smoke.rb -- boot Zhaoxu OS headless in QEMU (inside WSL), drive the serial
# shell interactively and assert a marker in the console output.
#
#   usage: ruby tools/smoke.rb <marker> [commands]
#     commands: newline-separated shell input, literal "\n" allowed;
#               "shutdown" is always appended automatically.
#   env:   SMOKE_DISTRO SMOKE_ROOT SMOKE_TIMEOUT (seconds, default 180)
#          SMOKE_BOOT: esp (default, fat:rw:esp + data.img)
#                      iso (cdrom out/zhaoxu-os.iso + data.img)
#                      iso-nodata (cdrom only, exercises /mnt fallback)
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

boot = ENV["SMOKE_BOOT"] || "esp"
drives = case boot
         when "esp"
           [
             "-drive format=raw,file=fat:rw:esp",
             "-drive if=none,id=zdata,format=raw,file=out/data.img",
             "-device ide-hd,drive=zdata,bus=ide.1",
           ]
         when "iso"
           [
             "-cdrom out/zhaoxu-os.iso",
             "-drive if=none,id=zdata,format=raw,file=out/data.img",
             "-device ide-hd,drive=zdata,bus=ide.1",
           ]
         when "iso-nodata"
           ["-cdrom out/zhaoxu-os.iso"]
         else
           abort "SMOKE_BOOT must be esp, iso or iso-nodata"
         end

inner = +"cd \"#{ROOT}\" || exit 1\n"
inner << %[cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" out/OVMF_VARS.fd\n]
qemu = [
  "timeout #{TIMEOUT} qemu-system-x86_64",
  "-M ${QEMU_MACHINE:-q35} -m ${QEMU_MEM:-512M} -smp 1",
  "-drive if=pflash,format=raw,unit=0,readonly=on,file=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}",
  "-drive if=pflash,format=raw,unit=1,file=out/OVMF_VARS.fd",
  *drives,
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
    prompts = out.scan("zhaoxu>").size
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
