#!/usr/bin/env ruby
# kernel-flat.rb -- flatten out/kernel.elf into the raw boot image that the
# legacy BIOS stage 2 reads from the CD (install/KERNEL.BIN).
#
# The kernel is linked at KERNEL_VMA, so each PT_LOAD segment is placed at
# (vaddr - KERNEL_VMA) and the gaps between segments become zeroes (stage 2
# zeroes the whole 16 MiB window before copying, so .bss needs no image).
#
# The output is padded to a whole 2048-byte CD sector: stage 2 always reads
# whole sectors, and the read must not run past the file extent.
#
#   ruby tools/kernel-flat.rb <kernel.elf> <out.bin>

KERNEL_VMA = 0xffffffff80000000
CD_SECTOR = 2048
MAX_KERNEL_BYTES = 0x70000      # stage 2 low buffer, must stay below the EBDA

in_path, out_path = ARGV
abort "usage: kernel-flat.rb <kernel.elf> <out.bin>" unless out_path

data = File.binread(in_path)
abort "kernel-flat: not an ELF64 little-endian image" unless
  data.start_with?("\x7FELF".b) && data.getbyte(4) == 2 && data.getbyte(5) == 1

entry = data[24, 8].unpack1("Q<")
abort "kernel-flat: entry is not KERNEL_VMA" unless entry == KERNEL_VMA

phoff = data[32, 8].unpack1("Q<")
phentsize = data[54, 2].unpack1("v")
phnum = data[56, 2].unpack1("v")
abort "kernel-flat: no program headers" if phoff.zero? || phnum.zero?

segments = []
span = 0
phnum.times do |i|
  base = phoff + i * phentsize
  type, _flags, offset, vaddr, _paddr, filesz, _memsz, _align =
    data[base, 56].unpack("VVQ<Q<Q<Q<Q<Q<")
  next unless type == 1 && filesz.positive?
  abort "kernel-flat: segment below KERNEL_VMA" if vaddr < KERNEL_VMA
  virtual = vaddr - KERNEL_VMA
  abort "kernel-flat: segment offset overflow" if virtual + filesz < virtual
  abort "kernel-flat: segment outside file" if offset + filesz > data.bytesize
  segments << [virtual, offset, filesz]
  span = [span, virtual + filesz].max
end
abort "kernel-flat: no loadable segments" if segments.empty?

flat = "\x00".b * span
segments.each { |virtual, offset, filesz| flat[virtual, filesz] = data[offset, filesz] }

padded = flat.bytesize + (CD_SECTOR - flat.bytesize % CD_SECTOR) % CD_SECTOR
abort "kernel-flat: #{padded} bytes exceed the stage 2 buffer " \
      "(#{MAX_KERNEL_BYTES})" if padded > MAX_KERNEL_BYTES

flat << ("\x00".b * (padded - flat.bytesize))
File.binwrite(out_path, flat)
puts "KERNEL.BIN: #{flat.bytesize} bytes (#{padded / CD_SECTOR} CD sectors)"
