#!/usr/bin/env ruby
# bios-img.rb -- build the legacy BIOS El Torito no-emulation boot image.
#
# The firmware loads the whole image at 0x0000:0x7C00 and jumps to it, so the
# layout below puts stage 2 exactly where stage 1 expects it:
#   [0, 512)      stage 1 (512-byte boot sector)
#   [512, 1024)   zero padding
#   [1024, ...)   stage 2 (runs at 0x8000)
#
# The image carries no kernel.  The CD keeps it as install/KERNEL.BIN and
# tools/iso-bios.rb patches the ZBOOTPRM block inside the finished ISO, which
# is why stage 1 is told stage 2 is already resident (stage2_lba = 0xFFFF).
#
#   ruby tools/bios-img.rb <stage1> <stage2> <out.img>

S1, S2, OUT = ARGV
abort "usage: bios-img.rb <stage1> <stage2> <out.img>" unless OUT

SECTOR = 512
STAGE2_OFFSET = 1024            # 0x7C00 + 1024 = 0x8000
STAGE2_RESIDENT = 0xFFFF        # stage1 marker value: do not read stage 2
MBR_CODE_BYTES = 446            # a partition table follows in an installed MBR

s1 = File.binread(S1)
s2 = File.binread(S2)

abort "stage1 must be #{SECTOR} bytes (got #{s1.bytesize})" unless s1.bytesize == SECTOR
abort "stage2 too large (#{s2.bytesize})" if s2.bytesize > 0x8000

marker = s1.index("ZSLBA")
abort "stage1 ZSLBA marker not found" unless marker
abort "stage1 ZSLBA marker outside the MBR code area" if marker + 7 > MBR_CODE_BYTES

stage1 = s1.dup
stage1[marker + 5, 2] = [STAGE2_RESIDENT].pack("v")

image = "\x00".b * STAGE2_OFFSET
image[0, SECTOR] = stage1
image[STAGE2_OFFSET, s2.bytesize] = s2
image << ("\x00".b * ((SECTOR - image.bytesize % SECTOR) % SECTOR))

File.binwrite(OUT, image)
puts "bios.img: stage1 + stage2 (#{s2.bytesize} bytes) in " \
     "#{image.bytesize / SECTOR} sectors, no kernel"
