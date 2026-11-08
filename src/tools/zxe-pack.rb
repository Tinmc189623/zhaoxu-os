#!/usr/bin/env ruby
# zxe-pack.rb -- convert a static non-PIE ELF64 into a Zhaoxu OS .zxe v1 image
# usage: ruby zxe-pack.rb in.elf out.zxe
#
# .zxe v1 layout (all little endian):
#   header (32 bytes)
#     0  u8[4]  magic 'Z' 'X' 'E' 0x01
#     4  u16    version = 1
#     6  u16    machine = 0x3E
#     8  u16    seg_count (1..16)
#     10 u16    hdr_size = align16(32 + 40 * seg_count)
#     12 u32    reserved = 0
#     16 u64    entry
#     24 u64    file_size
#   segment table: seg_count * 40 bytes
#     0 u64 vaddr (page aligned) | 8 u64 file_off (16 aligned)
#     16 u64 file_size | 24 u64 mem_size (>= file_size)
#     32 u64 flags (bit0 R, bit1 W, bit2 X)
#   segment data follows, each block padded to 16 bytes except the last.

abort "usage: #{$PROGRAM_NAME} in.elf out.zxe" unless ARGV.size == 2
abort "missing #{$0}" unless File.file?(ARGV[0])
data = File.binread(ARGV[0])
abort "not a 64-bit little endian ELF" unless
  data[0, 4] == "\x7FELF".b && data.getbyte(4) == 2 && data.getbyte(5) == 1
e_type, e_machine = data[16, 4].unpack("v2")
abort "not an EXEC x86-64 image" unless e_type == 2 && e_machine == 62
e_entry = data[24, 8].unpack1("Q<")
e_phoff = data[32, 8].unpack1("Q<")
e_phentsize, e_phnum = data[54, 4].unpack("v2")

segs = []
e_phnum.to_i.times do |i|
  ph = e_phoff + i * e_phentsize
  next unless ph + e_phentsize <= data.bytesize
  next unless data[ph, 4].unpack1("V") == 1 # PT_LOAD
  elf_flags = data[ph + 4, 4].unpack1("V")
  off = data[ph + 8, 8].unpack1("Q<")
  vaddr = data[ph + 16, 8].unpack1("Q<")
  filesz = data[ph + 32, 8].unpack1("Q<")
  memsz = data[ph + 40, 8].unpack1("Q<")
  next if memsz.zero?
  abort format("segment vaddr 0x%x below USER_LOAD_BASE", vaddr) if
    vaddr < 0x100_0000_0000
  abort format("segment vaddr 0x%x not page aligned", vaddr) unless
    (vaddr & 0xFFF).zero?
  abort "segment file range out of bounds" if off + filesz > data.bytesize
  abort "segment file_size exceeds mem_size" if filesz > memsz
  flags = ((elf_flags & 4) != 0 ? 1 : 0) |
          ((elf_flags & 2) != 0 ? 2 : 0) |
          ((elf_flags & 1) != 0 ? 4 : 0)
  segs << [vaddr, off, filesz, memsz, flags]
end
abort "no PT_LOAD segments" if segs.empty?
abort "too many segments (max 16)" if segs.size > 16

n = segs.size
hdr = (32 + 40 * n + 15) & ~15
cursor = hdr
plan = segs.map do |vaddr, off, filesz, memsz, flags|
  entry = [vaddr, cursor, filesz, memsz, flags]
  cursor = (cursor + filesz + 15) & ~15
  entry
end
file_size = plan.last[1] + plan.last[2] # end of final segment payload

out = String.new("", encoding: Encoding::BINARY)
out << "ZXE\x01".b << [1, 62, n, hdr].pack("v4") << [0].pack("V")
out << [e_entry, file_size].pack("Q<2")
plan.each { |vaddr, foff, fs, ms, fl| out << [vaddr, foff, fs, ms, fl].pack("Q<5") }
out << "\x00".b * (hdr - out.bytesize)
segs.each_with_index do |(_, off, filesz, _ms, _f), i|
  out << data[off, filesz]
  pad = (filesz + 15) & ~15
  out << "\x00".b * (pad - filesz) if i != segs.size - 1 && pad != filesz
end
abort "internal size mismatch" unless out.bytesize == file_size
File.binwrite(ARGV[1], out)
puts format("zxe: %d segs, entry 0x%x, %d bytes -> %s",
            n, e_entry, file_size, ARGV[1])
