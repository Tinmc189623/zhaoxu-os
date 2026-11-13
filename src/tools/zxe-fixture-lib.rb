#!/usr/bin/env ruby
# Emit a minimal ZXE v1 shared library used by the dynamic-link smoke test.
# The library exports ztest_value (returns 42) from a CODE segment.
# Usage: ruby zxe-fixture-lib.rb out/libztest.zxe

abort "usage: ruby zxe-fixture-lib.rb out.zxe" unless ARGV.length == 1
output = ARGV[0]
MAGIC = "ZXE\0".b
HEADER = 128
DIR = 24
SEG = 48
EXP = 24
PAGE = 4096

code = [0xB8, 42, 0, 0, 0, 0xC3].pack("C*") # mov eax, 42; ret

strings = String.new("\0".b, encoding: Encoding::BINARY)
name_offset = strings.bytesize
strings << "ztest_value\0".b

dir_end = HEADER + DIR * 3
segments_at = (dir_end + 7) & ~7
exports_at = segments_at + SEG
strings_at = exports_at + EXP
payload_at = (strings_at + strings.bytesize + 7) & ~7
image_size = PAGE * 2
file_size = payload_at + code.bytesize

out = String.new(encoding: Encoding::BINARY)
out << MAGIC
out << [1, 0x3E, HEADER, 1, DIR, 3].pack("v6")     # shared library
out << [file_size, 0, image_size, HEADER].pack("Q<4")
out << ("\0".b * 16)                                # build id
out << [1, 0].pack("V2")
out << ("\0".b * 56)
abort "header size" unless out.bytesize == HEADER

out << [1, 1, segments_at, 1, SEG].pack("VVQ<V2")
out << [4, 1, exports_at, 1, EXP].pack("VVQ<V2")
out << [6, 1, strings_at, strings.bytesize, 1].pack("VVQ<V2")

out << ("\0".b * (segments_at - out.bytesize))
out << [1, 0b101, payload_at, code.bytesize, PAGE, PAGE, PAGE].pack("V2Q<5")
out << [name_offset, 1, PAGE, 0, 1].pack("V2Q<V2")
out << strings
out << ("\0".b * (payload_at - out.bytesize))
out << code
abort "internal size #{out.bytesize} != #{file_size}" unless out.bytesize == file_size
File.binwrite(output, out)
puts "fixture library: #{file_size} bytes -> #{output}"
