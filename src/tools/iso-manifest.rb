#!/usr/bin/env ruby
# Populate the ISO metadata files required by AGENTS.md.
# Usage: ruby iso-manifest.rb <iso-root> <os-version> <codename> <kernel> <kernel-version> <build>
#
# Writes casper/{manifest,uuid,README.diskdefines}, .disk/{info,README,
# casper-uuid-generic}, preseed/os.seed, README.html and autorun.inf, then a
# md5sum.txt over every file in the ISO root.  The OS and kernel versions are
# separate fields of version.cfg (rule: version.cfg is the only source).

require "digest"
require "fileutils"
require "securerandom"

unless ARGV.length == 6
  abort "usage: ruby iso-manifest.rb iso-root os-version codename kernel kernel-version build"
end
root, version, codename, kernel, kernel_version, build = ARGV
# Normalise Windows separators so Dir.glob and the md5 relative names work
# on every host (the ISO build passes a POSIX path, but direct use may not).
root = root.tr("\\", "/").sub(%r{/+\z}, "")
abort "missing ISO root #{root}" unless Dir.exist?(root)

def write(path, text)
  FileUtils.mkdir_p(File.dirname(path))
  File.write(path, text)
  puts "iso: wrote #{path.sub(Dir.pwd + '/', '')}"
end

uuid = SecureRandom.uuid
casper = File.join(root, "casper")
disk = File.join(root, ".disk")
FileUtils.mkdir_p([casper, disk, File.join(root, "preseed")])

write(File.join(casper, "uuid"), "#{uuid}\n")
write(File.join(casper, "manifest"), <<~EOF)
  Zhaoxu OS #{version} "#{codename}" build #{build}
  kernel #{kernel} #{kernel_version}
  file          .        <version>
  file          bin      <directory>
  file          etc      <directory>
  file          lib      <directory>
  file          usr      <directory>
  EOF

write(File.join(casper, "README.diskdefines"), <<~EOF)
  #define DISKNAME  Zhaoxu OS #{version} "#{codename}"
  #define KERNEL  #{kernel} #{kernel_version}
  #define TYPE  binary
  #define ARCH  amd64
  #define ARCHSUFFIX  amd64
  EOF

write(File.join(disk, "info"), "Zhaoxu OS #{version} \"#{codename}\" build #{build} - Release amd64 (#{kernel} #{kernel_version})\n")
write(File.join(disk, "README"), "This disc is a Zhaoxu OS live/install image.\n")
write(File.join(disk, "casper-uuid-generic"), "#{uuid}\n")

write(File.join(root, "preseed", "os.seed"), <<~EOF)
  # Zhaoxu OS unattended install defaults (ZhaoxuBootMgr / installer).
  zhaoxu zhaoxu/language string zh_CN
  zhaoxu zhaoxu/keyboard string us
  zhaoxu zhaoxu/filesystem string ext4
  zhaoxu zhaoxu/boot string gpt
  zhaoxu zhaoxu/confirm boolean true
  EOF

write(File.join(root, "README.html"), <<~EOF)
  <!doctype html>
  <html lang="zh-CN"><head><meta charset="utf-8">
  <title>Zhaoxu OS #{version}</title></head>
  <body><h1>Zhaoxu OS #{version} &laquo;#{codename}&raquo;</h1>
  <p>#{kernel} #{kernel_version} &middot; Aqua Desktop Environment &middot; Desktop Window Manager &middot; ZAF</p>
  <ul><li>UEFI: EFI/BOOT/BOOTX64.EFI &rarr; OSLDRX64.efi &rarr; kernel.elf</li>
  <li>BIOS: boot/zhaoxu/BOOT.BIN &rarr; osloader.elf &rarr; kernel.elf</li></ul>
  </body></html>
  EOF

write(File.join(root, "autorun.inf"), "[autorun]\nicon=README.html\nlabel=Zhaoxu OS\n")

sums = []
Dir.glob(File.join(root, "**", "*"), File::FNM_DOTMATCH).sort.each do |path|
  next unless File.file?(path)
  relative = path.sub(root + "/", "")
  next if relative == "md5sum.txt"
  sums << "#{Digest::MD5.file(path).hexdigest}  #{relative}"
end
write(File.join(root, "md5sum.txt"), sums.join("\n") + "\n")
