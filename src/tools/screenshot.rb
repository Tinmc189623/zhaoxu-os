#!/usr/bin/env ruby
# screenshot.rb -- boot Zhaoxu OS headless, wait for Aqua Desktop Environment,
# and capture the QEMU framebuffer to a PNG for visual verification.
#
#   usage: ruby tools/screenshot.rb [seconds-before-shot] [out.png]
#   env:   SMOKE_DISTRO SMOKE_ROOT SMOKE_BOOT SMOKE_TIMEOUT
#          SHOT_HMP: newline-separated HMP commands (e.g. mouse_move /
#                    mouse_button) executed before the screendump;
#                    SHOT_STEP (default 0.2) and SHOT_WAIT (default 0.5)
#                    control the pause between commands / before the shot.
#
# Boots with -display none, waits for the desktop through wsl.exe, then asks
# the QEMU HMP monitor (TCP) for a screendump and turns the PPM into a PNG
# with zlib. Pure Ruby.

require "open3"
require "socket"
require "zlib"

DISTRO = ENV["SMOKE_DISTRO"] || "Ubuntu-26.04"
win_root = ENV["SMOKE_ROOT"] || File.expand_path("..", __dir__)
ROOT = win_root =~ /\A([A-Za-z]):[\\\/](.*)\z/ ?
       "/mnt/#{Regexp.last_match(1).downcase}/#{Regexp.last_match(2).gsub('\\', '/')}" :
       win_root
TIMEOUT = (ENV["SMOKE_TIMEOUT"] || 180).to_i
DELAY = (ARGV[0] || 2.0).to_f
SHOT = ARGV[1] || "out/desk.png"

# q35 exposes six single-unit IDE ports; ide.2/ide.3 collide with ide.1 on
# some QEMU builds, so put the installer extras on ide.4/ide.5.
installer_disks = [
  "-drive if=none,id=zsrc,format=raw,file=out/efi.img",
  "-device ide-hd,drive=zsrc,bus=ide.4",
  "-drive if=none,id=zdst,format=raw,file=out/target.img",
  "-device ide-hd,drive=zdst,bus=ide.5",
]
boot = ENV["SMOKE_BOOT"] || "esp"
# Rule 11: ISO images are named zhaoxu-os_x86-64_{version}_{build}_ld.iso.
iso_file = Dir[File.join(win_root, "out", "zhaoxu-os_x86-64_*_ld.iso")]
           .max_by { |f| File.mtime(f) }
if boot.start_with?("iso") && !iso_file
  abort "out/zhaoxu-os_x86-64_*_ld.iso not found; run `make iso` first"
end
iso_drive = "-cdrom out/#{iso_file ? File.basename(iso_file) : "zhaoxu-os_x86-64_0.7.20_2026-11-16_ld.iso"}"
drives = case boot
         when "esp"
           [
             "-drive format=raw,file=fat:rw:esp",
             "-drive if=none,id=zdata,format=raw,file=out/data.img",
             "-device ide-hd,drive=zdata,bus=ide.1",
             *installer_disks,
           ]
         when "iso"
           [
             iso_drive,
             "-drive if=none,id=zdata,format=raw,file=out/data.img",
             "-device ide-hd,drive=zdata,bus=ide.1",
             *installer_disks,
           ]
         when "iso-nodata"
           [iso_drive]
         when "iso-data"
           [
             iso_drive,
             "-drive if=none,id=zdata,format=raw,file=out/data.img",
             "-device ide-hd,drive=zdata,bus=ide.1",
           ]
         when "iso-install"
           # A blank target on ide.1 and no ZHAOXUDATA volume, so the graphical
           # installer opens instead of exiting early.
           [
             iso_drive,
             "-drive if=none,id=zdst,format=raw,file=out/target.img",
             "-device ide-hd,drive=zdst,bus=ide.1",
           ]
         when "installed"
           [
             "-drive if=none,id=zdst,format=raw,file=out/target.img",
             "-device ide-hd,drive=zdst,bus=ide.0",
           ]
         else
           abort "SMOKE_BOOT must be esp, iso, iso-nodata, iso-data, iso-install or installed"
         end

inner = +"cd \"#{ROOT}\" || exit 1\n"
inner << "rm -f out/desk.ppm\n"
inner << %[cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" out/OVMF_VARS.fd\n]
qemu = [
  "timeout #{TIMEOUT} qemu-system-x86_64",
  "-M ${QEMU_MACHINE:-q35} -m ${QEMU_MEM:-512M} -smp 1",
  "-drive if=pflash,format=raw,unit=0,readonly=on,file=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}",
  "-drive if=pflash,format=raw,unit=1,file=out/OVMF_VARS.fd",
  *drives,
  "-net none -serial stdio -display none -no-reboot",
  "-monitor tcp:127.0.0.1:45454,server,nowait",
]
inner << qemu.map { |l| "  #{l} \\\\" }.join("\n").sub(/ \\\\\z/, "") << "\n"

def ppm_to_png(ppm)
  # Scan the four header tokens byte-wise.  String#split(/\s+/) can consume
  # pixel bytes that happen to be whitespace (0x0A/0x0D/0x20) on some hosts.
  index = 0
  tokens = []
  while tokens.length < 4
    index += 1 while ppm.getbyte(index) && ppm.getbyte(index) <= 32
    start = index
    index += 1 while ppm.getbyte(index) && ppm.getbyte(index) > 32
    tokens << ppm.byteslice(start, index - start)
  end
  index += 1
  magic, width_s, height_s, maxval_s = tokens
  pixels = ppm.byteslice(index, ppm.bytesize - index)
  abort "bad ppm magic #{magic.inspect}" unless magic == "P6"
  width = width_s.to_i
  height = height_s.to_i
  abort "bad ppm header" unless maxval_s.to_i == 255 && !pixels.nil?
  pixels = pixels.b
  stride = width * 3
  abort "short ppm body" if pixels.bytesize < stride * height

  raw = "".b
  height.times { |y| raw << "\x00".b << pixels.byteslice(y * stride, stride) }
  idat = Zlib::Deflate.deflate(raw, 9)
  chunk = lambda do |type, data|
    [data.bytesize].pack("N") + type.b + data +
      [Zlib.crc32(type.b + data)].pack("N")
  end
  png = "\x89PNG\r\n\x1a\n".b
  png << chunk.call("IHDR", [width, height, 8, 2, 0, 0, 0].pack("NNCCCCC"))
  png << chunk.call("IDAT", idat)
  png << chunk.call("IEND", "")
  [png, width, height]
end

out = String.new(encoding: Encoding::BINARY)
shot = nil
Open3.popen3("wsl", "-d", DISTRO, "--", "bash", "-lc", inner) do |stdin, stdout, stderr, waiter|
  stdin.binmode
  stdout.binmode
  stderr.binmode
  deadline = Time.now + TIMEOUT

  started = Time.now
  loop do
    left = deadline - Time.now
    break if left <= 0
    readable = IO.select([stdout, stderr], nil, nil, 0.25)
    if readable
      readable[0].each do |io|
        begin
          chunk = io.read_nonblock(4096)
          out << chunk if chunk
        rescue Errno::EAGAIN, EOFError
        end
      end
    end
    break if Time.now - started >= DELAY
  end
  sleep 0.5

  begin
    sock = TCPSocket.new("127.0.0.1", 45_454)
    response = +""
    if IO.select([sock], nil, nil, 1.0)
      response << sock.read_nonblock(4096)
    end
    # HMP splits arguments on whitespace; QEMU's cwd is already the src dir.
    (ENV["SHOT_HMP"] || "").split("\n").reject(&:empty?).each do |line|
      sock.write(line.strip + "\n")
      sleep (ENV["SHOT_STEP"] || 0.2).to_f
    end
    sleep (ENV["SHOT_WAIT"] || 0.5).to_f
    sock.write("screendump out/desk.ppm\n")
    sleep 1.5
    sock.write("quit\n")
    deadline = Time.now + 1.0
    while Time.now < deadline
      break unless IO.select([sock], nil, nil, 0.2)
      begin
        response << sock.read_nonblock(4096)
      rescue EOFError
        break
      end
    end
    sock.close
    warn "monitor response:\n#{response}" unless response.include?("PPM")
  rescue StandardError => e
    warn "monitor connection failed: #{e.message}"
  end
  sleep 0.5
  File.binwrite(ENV["SHOT_SERIAL"], out) if ENV["SHOT_SERIAL"]
  begin
    waiter.kill("KILL")
  rescue StandardError
  end
end

system("wsl", "-d", DISTRO, "--", "pkill", "-f", "qemu-system-x86_64",
       out: File::NULL, err: File::NULL)

ppm, = Open3.capture2("wsl", "-d", DISTRO, "--", "bash", "-lc",
                      "cat \"#{ROOT}/out/desk.ppm\"", binmode: true)
ppm = ppm.b
abort "no screendump produced\n#{out[-2000..]}" if ppm.empty?

png, width, height = ppm_to_png(ppm)
File.binwrite(File.join(win_root, SHOT), png)
puts "saved #{SHOT} (#{width}x#{height}, #{png.bytesize} bytes)"
