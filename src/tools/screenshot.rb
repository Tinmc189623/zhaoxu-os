#!/usr/bin/env ruby
# screenshot.rb -- boot Zhaoxu OS headless, run Desktop-Shell, and capture the
# QEMU framebuffer to a PNG for visual verification.
#
#   usage: ruby tools/screenshot.rb [seconds-before-shot] [out.png]
#   env:   SMOKE_DISTRO SMOKE_ROOT SMOKE_BOOT SMOKE_TIMEOUT
#
# Boots with -display none, drives the serial shell through wsl.exe like
# smoke.rb, then asks the QEMU HMP monitor (TCP) for a screendump and turns
# the PPM into a PNG with zlib. Pure Ruby.

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

installer_disks = [
  "-drive if=none,id=zsrc,format=raw,file=out/efi.img",
  "-device ide-hd,drive=zsrc,bus=ide.2",
  "-drive if=none,id=zdst,format=raw,file=out/target.img",
  "-device ide-hd,drive=zdst,bus=ide.3",
]
boot = ENV["SMOKE_BOOT"] || "esp"
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
             "-cdrom out/zhaoxu-os.iso",
             "-drive if=none,id=zdata,format=raw,file=out/data.img",
             "-device ide-hd,drive=zdata,bus=ide.1",
             *installer_disks,
           ]
         when "iso-nodata"
           ["-cdrom out/zhaoxu-os.iso"]
         when "installed"
           [
             "-drive if=none,id=zdst,format=raw,file=out/target.img",
             "-device ide-hd,drive=zdst,bus=ide.0",
           ]
         else
           abort "SMOKE_BOOT must be esp, iso, iso-nodata or installed"
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
  magic, rest = ppm.split(/\s+/, 2)
  abort "bad ppm magic #{magic.inspect}" unless magic == "P6"
  width_s, rest = rest.split(/\s+/, 2)
  height_s, rest = rest.split(/\s+/, 2)
  maxval_s, pixels = rest.split(/\s+/, 2)
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

  prompt_seen = false
  desktop_sent = false
  desktop_at = nil
  loop do
    left = deadline - Time.now
    break if left <= 0
    readable = IO.select([stdout, stderr], nil, nil, 0.25)
    if readable
      readable[0].each do |io|
        begin
          chunk = io.read_nonblock(4096)
          out << chunk if chunk
        rescue Errno::EAGAIN, Errno::EAGAINWaitReadable, EOFError
        end
      end
    end
    unless prompt_seen
      prompt_seen = out.include?("zhaoxu>")
      next
    end
    unless desktop_sent
      stdin.write((ENV["SHOT_CMD"] || "desktop") + "\n")
      stdin.flush
      desktop_sent = true
      desktop_at = Time.now
    end
    break if desktop_sent && Time.now - desktop_at >= DELAY
  end
  sleep 0.5

  begin
    sock = TCPSocket.new("127.0.0.1", 45_454)
    response = +""
    if IO.select([sock], nil, nil, 1.0)
      response << sock.read_nonblock(4096)
    end
    # HMP splits arguments on whitespace; QEMU's cwd is already the src dir.
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
  begin
    waiter.kill("KILL")
  rescue StandardError
  end
end

system("wsl", "-d", DISTRO, "--", "pkill", "-f", "qemu-system-x86_64",
       out: File::NULL, err: File::NULL)

ppm, = Open3.capture2("wsl", "-d", DISTRO, "--", "bash", "-lc",
                      "cat \"#{ROOT}/out/desk.ppm\"")
ppm = ppm.b
abort "no screendump produced\n#{out[-2000..]}" if ppm.empty?

png, width, height = ppm_to_png(ppm)
File.binwrite(File.join(win_root, SHOT), png)
puts "saved #{SHOT} (#{width}x#{height}, #{png.bytesize} bytes)"
