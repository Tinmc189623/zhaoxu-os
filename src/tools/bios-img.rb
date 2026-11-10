#!/usr/bin/env ruby
# bios-img.rb -- build the legacy BIOS El Torito boot image (1.44 MB
# floppy emulation).  Layout:
#   LBA 0            stage 1 (512-byte boot sector)
#   LBA 1 .. 1+S-1   stage 2
#   LBA 1+S ..       kernel (zero padded)
# The kernel payload is extracted from the canonical KERNEL.ELF (PT_LOAD
# filesz, placed at vaddr - KERNEL_VMA), so the BIOS and UEFI paths always
# use the same kernel artifact and no kernel.bin intermediate is needed.
#
#   ruby tools/bios-img.rb <stage1> <stage2> <kernel.elf> <out.img>

S1, S2, KERNEL, OUT = ARGV
abort "usage: bios-img.rb <stage1> <stage2> <kernel.elf> <out.img>" unless OUT

KERNEL_VMA = 0xffffffff80000000

# Flatten an ELF64 kernel into the memory image that starts at KERNEL_VMA.
# A raw binary is accepted as-is for callers that already pass one.
def flatten_kernel(path)
  data = File.binread(path)
  return data unless data.start_with?("\x7FELF".b)

  abort "not an ELF64 x86-64 image" unless data.getbyte(4) == 2 &&
                                            data.getbyte(5) == 1
  entry = data[24, 8].unpack1("Q<")
  abort "kernel entry is not KERNEL_VMA" unless entry == KERNEL_VMA

  phoff = data[32, 8].unpack1("Q<")
  phentsize = data[54, 2].unpack1("v")
  phnum = data[56, 2].unpack1("v")
  abort "no program headers" if phoff.zero? || phnum.zero?

  segments = []
  span = 0
  phnum.times do |i|
    base = phoff + i * phentsize
    type, _flags, offset, vaddr, _paddr, filesz, _memsz, _align =
      data[base, 56].unpack("VVQ<Q<Q<Q<Q<Q<")
    next unless type == 1 && filesz.positive?
    abort "segment below KERNEL_VMA" if vaddr < KERNEL_VMA
    virtual = vaddr - KERNEL_VMA
    abort "segment offset overflow" if virtual + filesz < virtual
    abort "segment outside file" if offset + filesz > data.bytesize
    segments << [virtual, offset, filesz]
    span = [span, virtual + filesz].max
  end
  abort "no loadable segments" if segments.empty?

  flat = "\x00".b * span
  segments.each { |virtual, offset, filesz| flat[virtual, filesz] = data[offset, filesz] }
  flat
end

s1 = File.binread(S1)
s2 = File.binread(S2)
kernel = flatten_kernel(KERNEL)

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
