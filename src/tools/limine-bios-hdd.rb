#!/usr/bin/env ruby
# limine-bios-hdd.rb -- extract Limine's embedded BIOS disk image from
# limine-bios-hdd.h into a raw binary the installer can write to a target.
#
#   usage: ruby tools/limine-bios-hdd.rb <limine-bios-hdd.h> <out.bin>

header_path, output_path = ARGV
abort "usage: limine-bios-hdd.rb <limine-bios-hdd.h> <out.bin>" unless output_path

header = File.read(header_path)
body = header[/binary_limine_hdd_bin_data\[\]\s*=\s*\{(.*?)\};/m, 1]
abort "limine-bios-hdd: array not found" unless body
bytes = body.scan(/0x([0-9a-fA-F]{2})/).flatten.map { |hex| hex.to_i(16) }
abort "limine-bios-hdd: empty image" if bytes.empty?
File.binwrite(output_path, bytes.pack("C*"))
puts "limine-bios-hdd: #{bytes.length} bytes"
