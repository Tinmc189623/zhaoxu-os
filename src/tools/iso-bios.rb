#!/usr/bin/env ruby
# iso-bios.rb -- patch the BIOS stage 2 parameter block inside a finished ISO.
#
# The legacy BIOS boot image (bios.img) holds only stage 1 and stage 2; the
# kernel travels as the ordinary ISO9660 file install/KERNEL.BIN.  Its extents
# are only known after xorriso has laid the ISO out, so this script walks the
# ISO9660 primary volume descriptor, finds the file and writes the 48-byte
# ZBOOTPRM block of the stage 2 copy embedded in bios.img.  The El Torito boot
# catalog is parsed first, so the patched extent is proven to be the image the
# firmware is told to load:
#
#   kernel_lba (2 KiB units), kernel_bytes, kernel_sectors,
#   cd flag (1), font_lba, font_bytes
#
#   ruby tools/iso-bios.rb <iso>

iso = ARGV[0]
abort "usage: iso-bios.rb <iso>" unless iso

# Success marker.  Under WSL this script usually runs as the Windows ruby.exe,
# and the interop layer can report exit 0 even when abort fires (it depends on
# the WSL build), so the Makefile cannot trust the exit status.  The marker is
# dropped up front and only re-created once the patched image is on disk; the
# ISO rule asserts it.
marker = "#{iso}.patched"
File.delete(marker) if File.exist?(marker)

SECTOR = 2048
STAGE2_OFFSET = 1024          # stage 2 sits 1 KiB into the El Torito image
FINAL_EXTENT = 0x80           # file flags bit 7: not the last extent
BOOT_RECORD = 17              # boot record volume descriptor sector
CATALOG_ENTRY = 32            # initial/default entry after the 32-byte validation entry
CATALOG_ENTRY_LBA = 8         # within the entry: load RBA in 2048-byte units

data = File.binread(iso)

def le32(str, off)
  str[off, 4].unpack1("V")
end

# ISO9660 identifiers carry a ";version" suffix on files, and the primary
# descriptor uses upper case while the tree on disk may not.
def normalize(name)
  name.sub(/;[0-9]*\z/, "").upcase
end

def entries(data, lba, size)
  raw = data[lba * SECTOR, size]
  abort "iso-bios: directory extent #{lba} outside the ISO" unless raw && raw.bytesize == size
  out = []
  pos = 0
  while pos < raw.bytesize
    len = raw.getbyte(pos)
    break if len.nil? || len.zero?
    abort "iso-bios: short directory record at #{pos}" if pos + len > raw.bytesize
    rec = raw[pos, len]
    name_len = rec.getbyte(32)
    name = rec[33, name_len].to_s
    # 0x00 / 0x01 mark the "." / ".." entries.
    name = "." if name_len == 1 && rec.getbyte(33) == 0
    name = ".." if name_len == 1 && rec.getbyte(33) == 1
    out << { name: name, lba: le32(rec, 2), size: le32(rec, 10),
             final: (rec.getbyte(25) & FINAL_EXTENT).zero? }
    pos += len
  end
  out
end

def find(list, want)
  item = list.find { |e| normalize(e[:name]) == want }
  abort "iso-bios: #{want} missing from the ISO" unless item
  abort "iso-bios: #{want} is multi-extent" unless item[:final]
  abort "iso-bios: #{want} is empty" if item[:size].zero?
  item
end

pvd = data[16 * SECTOR, SECTOR]
abort "iso-bios: no ISO9660 primary volume descriptor" unless pvd && pvd[1, 5] == "CD001"

root = entries(data, le32(pvd, 158), le32(pvd, 166))
install = find(root, "INSTALL")
abort "iso-bios: INSTALL is not a directory" if install[:size].zero?
kernel = find(entries(data, install[:lba], install[:size]), "KERNEL.BIN")
font = find(entries(data, install[:lba], install[:size]), "WQY.TTC")
boot = find(root, "BIOS.IMG")

[kernel, font].each do |item|
  next if item[:lba] * SECTOR + item[:size] <= data.bytesize
  abort "iso-bios: #{item[:name]} extent past the end of the ISO"
end

stage2 = boot[:lba] * SECTOR + STAGE2_OFFSET
image_end = boot[:lba] * SECTOR + boot[:size]
abort "iso-bios: BIOS.IMG extent outside the ISO" if image_end > data.bytesize

# The El Torito catalog is what the firmware actually loads, so the patch must
# land in the image the catalog points at -- cross-check it against the
# BIOS.IMG directory entry instead of trusting the tree alone.
boot_record = data[BOOT_RECORD * SECTOR, SECTOR]
unless boot_record && boot_record.getbyte(0).zero? && boot_record[1, 5] == "CD001"
  abort "iso-bios: no El Torito boot record volume descriptor"
end
unless boot_record[7, 32].to_s.start_with?("EL TORITO")
  abort "iso-bios: boot record is not an El Torito descriptor"
end

catalog_lba = le32(boot_record, 71)
catalog = data[catalog_lba * SECTOR, SECTOR]
abort "iso-bios: boot catalog outside the ISO" unless catalog && catalog.bytesize == SECTOR
unless catalog.getbyte(0) == 1 && catalog.getbyte(30) == 0x55 && catalog.getbyte(31) == 0xAA
  abort "iso-bios: boot catalog validation entry missing"
end

image_lba = le32(catalog, CATALOG_ENTRY + CATALOG_ENTRY_LBA)
unless image_lba == boot[:lba]
  abort "iso-bios: catalog boots LBA #{image_lba}, BIOS.IMG is at LBA #{boot[:lba]}"
end

magic = "ZBOOTPRM".b
index = data.index(magic, stage2)
abort "iso-bios: ZBOOTPRM missing from BIOS.IMG" unless index
abort "iso-bios: ZBOOTPRM outside BIOS.IMG stage 2" if index + 8 + 48 > image_end

sectors = (kernel[:size] + SECTOR - 1) / SECTOR
patch = [kernel[:lba], kernel[:size], sectors, 1, font[:lba], font[:size]]
        .pack("Q<Q<Q<Q<Q<Q<")
data[index + 8, patch.bytesize] = patch

File.binwrite(iso, data)
File.write(marker, "kernel_lba=#{kernel[:lba]} font_lba=#{font[:lba]}\n")
puts "iso-bios: kernel LBA #{kernel[:lba]} (#{kernel[:size]} bytes), " \
     "font LBA #{font[:lba]} (#{font[:size]} bytes)"
