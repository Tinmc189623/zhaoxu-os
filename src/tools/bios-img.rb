#!/usr/bin/env ruby
# bios-img.rb -- build the legacy BIOS El Torito boot image (1.44 MB
# floppy emulation).  Layout:
#   LBA 0            stage 1 (512-byte boot sector)
#   LBA 1 .. 1+S-1   stage 2
#   LBA 1+S ..       kernel (zero padded)
# The stage 2 parameter block is patched with the kernel LBA, byte size and
# sector count so the same stage 2 binary works from the ISO and from an
# installed disk.
#
#   ruby tools/bios-img.rb <stage1> <stage2> <kernel> <out.img>

S1, S2, KERNEL, OUT = ARGV
abort "usage: bios-img.rb <stage1> <stage2> <kernel> <out.img>" unless OUT

s1 = File.binread(S1)
s2 = File.binread(S2)
kernel = File.binread(KERNEL)

abort "stage1 must be 512 bytes" unless s1.bytesize == 512
abort "stage2 too large (#{s2.bytesize})" if s2.bytesize > 0x8000

sector = 512
s2_sectors = (s2.bytesize + sector - 1) / sector
kernel_lba = 1 + s2_sectors
kernel_sectors = (kernel.bytesize + sector - 1) / sector
image_bytes = 80 * 2 * 18 * sector      # 1.44 MB floppy

MAX_KERNEL_SECTORS = 0x380              # 448 KiB low buffer below the EBDA
abort "kernel too large for the stage 2 low buffer " \
      "(#{kernel.bytesize} bytes)" if kernel_sectors > MAX_KERNEL_SECTORS

last = (kernel_lba + kernel_sectors) * sector
abort "boot image overflow: #{last} > #{image_bytes}" if last > image_bytes

MAGIC = "ZBOOTPRM".b
index = s2.index(MAGIC)
abort "stage2 parameter magic not found" unless index
abort "truncated stage2 parameter block" if index + 8 + 24 > s2.bytesize

patched = s2.dup
patched[index + 8, 24] = [kernel_lba, kernel.bytesize, kernel_sectors].pack("Q<Q<Q<")

image = "\x00".b * image_bytes
image[0, 512] = s1
image[sector, s2.bytesize] = patched
image[kernel_lba * sector, kernel.bytesize] = kernel

File.binwrite(OUT, image)
puts "bios.img: stage2 #{s2_sectors} sectors, kernel #{kernel_sectors} sectors at LBA #{kernel_lba}"
