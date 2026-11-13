#!/usr/bin/env ruby
# Convert a constrained x86-64 ELF64 PIE plus ZXE JSON metadata sections.
# Usage: ruby zxe-pack.rb input.elf output.zxe [--shared]

require "digest"
require "json"

MAGIC = "ZXE\0".b
VERSION = 1
MACHINE_X86_64 = 0x003E
HEADER_SIZE = 128
DIRECTORY_SIZE = 24
MAX_FILE = 256 * 1024 * 1024
MAX_SEGMENTS = 32
MAX_DEPS = 64
MAX_IMPORTS = 4096
MAX_EXPORTS = 4096
MAX_RELOCS = 65536
MAX_STRINGS = 1024 * 1024
PAGE = 4096

TABLES = {
  1 => [:segments, 48],
  2 => [:dependencies, 16],
  3 => [:imports, 16],
  4 => [:exports, 24],
  5 => [:relocations, 24],
  6 => [:strings, 1]
}.freeze

RELOCATIONS = {
  "RELATIVE" => 1,
  "64" => 2,
  "GLOB_DAT" => 3,
  "JUMP_SLOT" => 4
}.freeze

abort "usage: ruby zxe-pack.rb input.elf output.zxe [--shared]" unless
  (ARGV.length == 2 || (ARGV.length == 3 && ARGV[2] == "--shared"))
input_path, output_path = ARGV
shared = ARGV.include?("--shared")
abort "missing input file: #{input_path}" unless File.file?(input_path)

data = File.binread(input_path)
abort "ELF input exceeds 256 MiB" if data.bytesize > MAX_FILE
abort "not an ELF64 little-endian image" unless
  data.bytesize >= 64 && data[0, 4] == "\x7FELF".b &&
  data.getbyte(4) == 2 && data.getbyte(5) == 1 && data.getbyte(6) == 1

e_type, e_machine, e_version = data[16, 8].unpack("v2V")
abort "expected ELF64 PIE (ET_DYN) for x86-64" unless
  e_type == 3 && e_machine == 62 && e_version == 1
entry, phoff, shoff = data[24, 24].unpack("Q<3")
_e_flags, ehsize, phentsize, phnum, shentsize, shnum, shstrndx =
  data[48, 18].unpack("Vv6")
abort "invalid ELF header sizes" unless ehsize >= 64 && phentsize >= 56 &&
  shentsize >= 64 && phnum.positive? && shnum.positive? && shstrndx < shnum
ph_end = phoff + phentsize * phnum
sh_end = shoff + shentsize * shnum
abort "ELF program-header table out of bounds" if ph_end < phoff || ph_end > data.bytesize
abort "ELF section-header table out of bounds" if sh_end < shoff || sh_end > data.bytesize

programs = phnum.times.map do |i|
  offset = phoff + i * phentsize
  type, flags, file_off, vaddr, _paddr, file_size, mem_size, align =
    data[offset, 56].unpack("V2Q<6")
  [type, flags, file_off, vaddr, file_size, mem_size, align]
end
abort "ELF input may contain only PT_LOAD program headers" unless
  programs.all? { |program| program[0] == 1 }

segments = []
programs.each do |_type, flags, file_off, vaddr, file_size, mem_size, align|
  next if mem_size.zero?
  abort "PT_LOAD has invalid permission bits" unless (flags & ~7).zero?
  abort "PT_LOAD must be readable" if (flags & 4).zero?
  abort "PT_LOAD has W+X permissions" if (flags & 3) == 3
  abort "PT_LOAD file_size exceeds mem_size" if file_size > mem_size
  abort "PT_LOAD file range out of bounds" if
    file_off + file_size < file_off || file_off + file_size > data.bytesize
  abort "PT_LOAD RVA must be 4 KiB aligned" unless (vaddr % PAGE).zero?
  abort "PT_LOAD alignment must be at least 4 KiB" if align < PAGE ||
    (align & (align - 1)) != 0
  segment_type, segment_flags = if file_size.zero?
    abort "zero-fill PT_LOAD must be writable" unless (flags & 2) != 0
    [4, 3]
  elsif (flags & 1) != 0
    [1, 5]
  elsif (flags & 2) != 0
    [3, 3]
  else
    [2, 1]
  end
  mem_size = (mem_size + PAGE - 1) & ~(PAGE - 1)
  segments << {
    type: segment_type,
    flags: segment_flags,
    source_offset: file_off,
    file_size: file_size,
    mem_size: mem_size,
    rva: vaddr,
    alignment: PAGE
  }
end
abort "ELF has no loadable segments" if segments.empty?
abort "too many loadable segments (max 32)" if segments.length > MAX_SEGMENTS
segments.sort_by! { |segment| segment[:rva] }
segments.each_with_index do |segment, i|
  if i.positive? && segments[i - 1][:rva] + segments[i - 1][:mem_size] > segment[:rva]
    abort "rounded PT_LOAD memory ranges overlap"
  end
end
image_size = segments.map { |segment| segment[:rva] + segment[:mem_size] }.max
abort "image exceeds 1 GiB" if image_size > 1024 * 1024 * 1024
unless shared
  abort "entry point is outside executable PT_LOAD" unless segments.any? do |segment|
    segment[:type] == 1 && entry >= segment[:rva] &&
      entry < segment[:rva] + segment[:mem_size]
  end
else
  # The ZXE shared-image entry is always zero; ELF e_entry is ignored.
end

sections = []
shnum.times do |i|
  offset = shoff + i * shentsize
  name, type, flags, address, file_off, size, link, info, align, entsize =
    data[offset, 64].unpack("V2Q<4V2Q<2")
  abort "ELF section out of bounds" if type != 8 &&
    (file_off + size < file_off || file_off + size > data.bytesize)
  sections << { name_offset: name, type: type, flags: flags, address: address,
                offset: file_off, size: size, link: link, info: info,
                align: align, entsize: entsize }
end
shstr = sections[shstrndx]
abort "invalid ELF section-name string table" unless shstr[:type] == 3
shstr_data = data[shstr[:offset], shstr[:size]]
sections.each do |section|
  n = section[:name_offset]
  abort "ELF section name outside string table" if n >= shstr_data.bytesize
  ending = shstr_data.index("\0", n)
  abort "unterminated ELF section name" unless ending
  section[:name] = shstr_data[n...ending]
end

metadata = {}
%w[.zxe.deps .zxe.imports .zxe.exports .zxe.relocs].each do |name|
  matching = sections.select { |section| section[:name] == name }
  abort "duplicate ELF metadata section #{name}" if matching.length > 1
  next if matching.empty?
  section = matching.first
  abort "metadata section #{name} must be non-allocating" unless
    (section[:flags] & 2).zero?
  bytes = data[section[:offset], section[:size]]
  abort "metadata section #{name} exceeds 1 MiB" if bytes.bytesize > MAX_STRINGS
  begin
    metadata[name] = JSON.parse(bytes.force_encoding(Encoding::UTF_8))
  rescue JSON::ParserError, EncodingError => error
    abort "invalid JSON in #{name}: #{error.message}"
  end
  abort "#{name} must contain a JSON array" unless metadata[name].is_a?(Array)
end

dependencies = metadata.fetch(".zxe.deps", [])
imports = metadata.fetch(".zxe.imports", [])
export_specs = metadata.fetch(".zxe.exports", [])
reloc_specs = metadata.fetch(".zxe.relocs", [])
abort "too many dependencies" if dependencies.length > MAX_DEPS
abort "too many imports" if imports.length > MAX_IMPORTS
abort "too many exports" if export_specs.length > MAX_EXPORTS
abort "too many relocations" if reloc_specs.length > MAX_RELOCS

string_data = "\0".b
string_offsets = { "" => 0 }
add_string = lambda do |value|
  abort "symbol/library name must be UTF-8" unless value.is_a?(String) &&
    value.encoding == Encoding::UTF_8 && value.valid_encoding?
  encoded = value.encode(Encoding::UTF_8).b
  abort "name is empty or longer than 255 bytes" if encoded.empty? || encoded.bytesize > 255
  abort "name contains NUL" if encoded.include?("\0")
  return string_offsets[encoded] if string_offsets.key?(encoded)
  offset = string_data.bytesize
  string_data << encoded << "\0".b
  string_offsets[encoded] = offset
  offset
end

dependency_table = dependencies.map do |item|
  abort "dependency must be an object" unless item.is_a?(Hash)
  name_offset = add_string.call(item.fetch("name"))
  abi = Integer(item.fetch("abi_major"))
  abort "dependency ABI major must be positive" unless abi.positive? && abi <= 0xFFFF_FFFF
  [name_offset, abi, 0, 0].pack("V4")
end.join.b

import_table = imports.map do |item|
  abort "import must be an object" unless item.is_a?(Hash)
  dependency_index = Integer(item.fetch("dependency_index"))
  abort "import dependency index out of bounds" unless
    dependency_index >= 0 && dependency_index < dependencies.length
  abi = Integer(item.fetch("abi_major"))
  abort "import ABI does not match its dependency" unless
    abi == Integer(dependencies[dependency_index].fetch("abi_major"))
  flags = item["weak"] == true ? 1 : 0
  [dependency_index, add_string.call(item.fetch("name")), abi, flags].pack("V4")
end.join.b

ELF_RELOCATIONS = { 1 => "64", 6 => "GLOB_DAT", 7 => "JUMP_SLOT",
                    8 => "RELATIVE" }.freeze
sections.each do |section|
  next unless section[:type] == 4 && (section[:flags] & 2) != 0
  abort "ELF RELA entry size must be 24" unless section[:entsize] == 24
  abort "ELF RELA section size is not a multiple of 24" unless
    (section[:size] % 24).zero?
  abort "ELF RELA symbol table index is invalid" if section[:link] >= sections.length
  symbol_table = sections[section[:link]]
  abort "ELF RELA link is not a symbol table" unless
    [2, 11].include?(symbol_table[:type]) && symbol_table[:entsize] == 24
  symbol_strings = sections[symbol_table[:link]]
  abort "ELF symbol string table is invalid" unless symbol_strings[:type] == 3
  names = data[symbol_strings[:offset], symbol_strings[:size]]
  (section[:size] / 24).times do |i|
    rel_offset = section[:offset] + i * 24
    target, info, addend = data[rel_offset, 24].unpack("Q<Q<q<")
    elf_type = info & 0xFFFF_FFFF
    symbol_index = info >> 32
    type_name = ELF_RELOCATIONS[elf_type]
    abort "unsupported ELF dynamic relocation type #{elf_type}" unless type_name
    if type_name == "RELATIVE"
      abort "ELF RELATIVE relocation has nonzero symbol index" unless symbol_index.zero?
      zxe_symbol_index = 0
    else
      symbol_count = symbol_table[:size] / 24
      abort "ELF relocation symbol index out of bounds" if symbol_index >= symbol_count
      symbol_offset = symbol_table[:offset] + symbol_index * 24
      name_offset = data[symbol_offset, 4].unpack1("V")
      abort "ELF relocation symbol name out of bounds" if name_offset >= names.bytesize
      name_end = names.index("\0", name_offset)
      abort "ELF relocation symbol name is unterminated" unless name_end
      symbol_name = names[name_offset...name_end]
      matches = imports.each_index.select do |import_index|
        imports[import_index].fetch("name") == symbol_name
      end
      abort "no unique ZXE import for ELF symbol #{symbol_name}" unless matches.length == 1
      zxe_symbol_index = matches.first
    end
    reloc_specs << { "target_rva" => target, "type" => type_name,
                     "symbol_index" => zxe_symbol_index, "addend" => addend }
  end
end
abort "too many relocations" if reloc_specs.length > MAX_RELOCS

symtab = sections.select { |section| section[:type] == 2 }
  .to_h { |section| [section[:index] || sections.index(section), section] }
symbols = {}
symtab.each_value do |table|
  next unless table[:entsize] == 24 && table[:link] < sections.length
  strtab = sections[table[:link]]
  next unless strtab[:type] == 3
  names = data[strtab[:offset], strtab[:size]]
  (table[:size] / 24).times do |i|
    offset = table[:offset] + i * 24
    name_offset, info, _other, shndx, value, size =
      data[offset, 24].unpack("VCCvQ<2")
    next if name_offset >= names.bytesize || shndx.zero?
    end_name = names.index("\0", name_offset)
    next unless end_name
    name = names[name_offset...end_name]
    next if name.empty? || (info >> 4) != 1
    abort "duplicate global ELF symbol #{name}" if symbols.key?(name)
    symbols[name] = [value, size]
  end
end

export_table = export_specs.map do |item|
  abort "export must be an object" unless item.is_a?(Hash)
  abi = Integer(item.fetch("abi_major"))
  symbol_name = item.fetch("symbol")
  abort "export must request GLOBAL visibility" unless item.fetch("global", true) == true
  symbol = symbols[symbol_name]
  abort "exported ELF symbol not found: #{symbol_name}" unless symbol
  value, symbol_size = symbol
  segment = segments.find { |s| value >= s[:rva] && value < s[:rva] + s[:mem_size] }
  abort "exported symbol outside PT_LOAD: #{symbol_name}" unless segment
  kind = item.fetch("type", "function")
  size = Integer(item.fetch("size", symbol_size))
  abort "non-function export requires nonzero size" if kind != "function" && size <= 0
  abort "unsupported export type #{kind}" unless %w[function object].include?(kind)
  [add_string.call(item.fetch("name")), abi, value, size, 1].pack("V2Q<V2")
end.join.b

relocation_table = reloc_specs.map do |item|
  abort "relocation must be an object" unless item.is_a?(Hash)
  type_name = item.fetch("type").to_s
  type = RELOCATIONS[type_name]
  abort "unsupported ZXE relocation #{type_name}" unless type
  target = Integer(item.fetch("target_rva"))
  symbol_index = Integer(item.fetch("symbol_index", 0))
  addend = Integer(item.fetch("addend", 0))
  abort "RELATIVE relocation symbol_index must be zero" if type == 1 && !symbol_index.zero?
  abort "non-RELATIVE relocation symbol index out of bounds" if
    type != 1 && (symbol_index < 0 || symbol_index >= imports.length)
  abort "relocation target must be aligned and writable" unless
    (target % 8).zero? && segments.any? do |segment|
      [3, 4].include?(segment[:type]) && target >= segment[:rva] &&
        target + 8 <= segment[:rva] + segment[:mem_size]
    end
  abort "relocation addend out of signed 64-bit range" unless
    addend >= -(1 << 63) && addend < (1 << 63)
  [target, type, symbol_index, addend].pack("Q<VVq<")
end.join.b

abort "string table exceeds 1 MiB" if string_data.bytesize > MAX_STRINGS
segment_payloads = segments.map { |s| data[s[:source_offset], s[:file_size]] }
table_blobs = [
  segments.map { |s| [s[:type], s[:flags], 0, s[:file_size], s[:mem_size],
                        s[:rva], s[:alignment]].pack("V2Q<5") }.join.b,
  dependency_table,
  import_table,
  export_table,
  relocation_table,
  string_data
]
counts = [segments.length, dependencies.length, imports.length,
          export_specs.length, reloc_specs.length, string_data.bytesize]

cursor = HEADER_SIZE + DIRECTORY_SIZE * TABLES.length
directory = String.new(encoding: Encoding::BINARY)
TABLES.each do |kind, (_name, entry_size)|
  cursor = (cursor + 7) & ~7
  blob = table_blobs[kind - 1]
  directory << [kind, 1, cursor, counts[kind - 1], entry_size].pack("VVQ<V2")
  cursor += blob.bytesize
end
payload_cursor = (cursor + 7) & ~7
segment_file_offsets = []
segments.each_with_index do |segment, i|
  payload_cursor = (payload_cursor + 7) & ~7
  segment_file_offsets << payload_cursor
  payload_cursor += segment[:file_size]
end
file_size = payload_cursor
abort "ZXE output exceeds 256 MiB" if file_size > MAX_FILE

# Patch segment file offsets into the segment-table blob.
segment_blob = String.new(encoding: Encoding::BINARY)
segments.each_with_index do |s, i|
  segment_blob << [s[:type], s[:flags], segment_file_offsets[i], s[:file_size],
                   s[:mem_size], s[:rva], s[:alignment]].pack("V2Q<5")
end
table_blobs[0] = segment_blob

out = String.new(encoding: Encoding::BINARY)
out << MAGIC << [VERSION, MACHINE_X86_64, HEADER_SIZE,
                (shared ? 1 : 0), DIRECTORY_SIZE, TABLES.length].pack("v6")
out << [file_size, (shared ? 0 : entry), image_size, HEADER_SIZE].pack("Q<4")
out << Digest::SHA256.digest(data)[0, 16]
out << [1, 0].pack("V2")
out << "\0".b * 56
abort "internal header size mismatch" unless out.bytesize == HEADER_SIZE
out << directory
TABLES.each do |kind, _|
  target = directory[(kind - 1) * DIRECTORY_SIZE + 8, 8].unpack1("Q<")
  out << "\0".b * (target - out.bytesize)
  out << table_blobs[kind - 1]
end
segment_payloads.each_with_index do |payload, i|
  target = segment_file_offsets[i]
  out << "\0".b * (target - out.bytesize)
  out << payload
end
abort "internal output size mismatch (#{out.bytesize} != #{file_size})" unless
  out.bytesize == file_size
File.binwrite(output_path, out)
puts format("zxe: v1 %d segments, %d deps, %d imports, %d exports, %d relocs, %d bytes -> %s",
            segments.length, dependencies.length, imports.length,
            export_specs.length, reloc_specs.length, file_size, output_path)
