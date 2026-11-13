#!/usr/bin/env ruby
# Validate packed ZXE v1 files against docs/superpowers/specs/2026-11-13-zxe-native-dynamic-design.md
# Usage: ruby zxe-check.rb file.zxe [file.zxe ...]

MAGIC = "ZXE\0".b
VERSION = 1
MACHINE = 0x003E
HEADER_SIZE = 128
DIR_ENTRY = 24
PAGE = 4096
MAX_FILE = 256 * 1024 * 1024
MAX_SEGMENTS = 32
MAX_DEPS = 64
MAX_IMPORTS = 4096
MAX_EXPORTS = 4096
MAX_RELOCS = 65_536
MAX_STRINGS = 1024 * 1024
IMAGE_WINDOW = 1024 * 1024 * 1024
TABLE_KINDS = {
  1 => ["segments", 48], 2 => ["dependencies", 16], 3 => ["imports", 16],
  4 => ["exports", 24], 5 => ["relocations", 24], 6 => ["strings", 1]
}.freeze
SEGMENT_PERMS = { 1 => 0b101, 2 => 0b001, 3 => 0b011, 4 => 0b011 }.freeze
RELOC_TYPES = [1, 2, 3, 4].freeze

$failures = 0

def check(condition, file, message)
  return if condition
  $failures += 1
  puts "#{file}: FAIL #{message}"
end

def utf8?(bytes)
  return false if bytes.empty?
  text = bytes + "\0".b
  valid = text.force_encoding(Encoding::UTF_8)
  valid.valid_encoding?
end

ARGV.each do |path|
  abort "missing #{path}" unless File.file?(path)
  data = File.binread(path)
  check(data.bytesize <= MAX_FILE, path, "file exceeds 256 MiB")
  check(data.bytesize >= HEADER_SIZE, path, "shorter than the fixed header")
  magic = data[0, 4]
  check(magic == MAGIC, path, "bad magic #{magic.inspect}")
  version, machine, header_size, flags, dir_entry, dir_count =
    data[4, 12].unpack("v6")
  check(version == VERSION, path, "version #{version} != 1")
  check(machine == MACHINE, path, "machine 0x#{machine.to_s(16)} != 0x3e")
  check(header_size == HEADER_SIZE, path, "header_size #{header_size} != 128")
  check((flags & ~1).zero?, path, "undefined header flags 0x#{flags.to_s(16)}")
  shared = (flags & 1) != 0
  check(dir_entry == DIR_ENTRY, path, "directory entry size #{dir_entry} != 24")
  check(dir_count.between?(1, 16), path, "directory_count #{dir_count} out of range")
  file_size, entry_rva, image_size, dir_offset =
    data[16, 32].unpack("Q<4")
  check(file_size == data.bytesize, path, "file_size #{file_size} != #{data.bytesize}")
  check((dir_offset % 8).zero?, path, "directory offset not 8-byte aligned")
  check(dir_offset == HEADER_SIZE, path, "directory does not follow the header")
  api_major = data[64, 4].unpack1("V")
  check(api_major == 1, path, "api_abi_major #{api_major} != 1")
  reserved0 = data[68, 4].unpack1("V")
  check(reserved0.zero?, path, "reserved0 not zero")
  check(data[72, 56] == "\0".b * 56, path, "reserved1 not zero")
  check(image_size.positive? && image_size <= IMAGE_WINDOW,
        path, "image_size #{image_size} outside 1 GiB window")
  check(image_size % PAGE == 0, path, "image_size not page aligned")
  if shared
    check(entry_rva.zero?, path, "shared image entry_rva must be zero")
  else
    check(entry_rva < image_size, path, "entry_rva outside image")
  end

  dir_end = dir_offset + dir_count * DIR_ENTRY
  check(dir_end <= data.bytesize, path, "directory table truncated")
  dirs = dir_count.times.map do |i|
    kind, dflags, offset, count, entry_size = data[dir_offset + i * DIR_ENTRY, DIR_ENTRY].unpack("VVQ<V2")
    { kind: kind, flags: dflags, offset: offset, count: count, entry_size: entry_size }
  end
  check(dirs.map { |d| d[:kind] }.uniq.length == dirs.length,
        path, "duplicate table kind")
  ranges = []
  dirs.each do |d|
    check((d[:flags] & ~1).zero?, path, "undefined directory flags")
    check((d[:offset] % 8).zero?, path, "table #{d[:kind]} offset not aligned")
    check(d[:offset] >= dir_end, path, "table #{d[:kind]} overlaps directory")
    expected = TABLE_KINDS[d[:kind]]
    if expected.nil?
      check((d[:flags] & 1).zero?, path, "unknown required table kind #{d[:kind]}")
      check(d[:entry_size].positive?, path, "unknown table entry_size zero")
    else
      check(d[:entry_size] == expected[1],
            path, "table #{expected[0]} entry_size #{d[:entry_size]} != #{expected[1]}")
      limit = { 1 => MAX_SEGMENTS, 2 => MAX_DEPS, 3 => MAX_IMPORTS,
                4 => MAX_EXPORTS, 5 => MAX_RELOCS, 6 => MAX_STRINGS }[d[:kind]]
      check(d[:count] <= limit, path, "table #{expected[0]} count #{d[:count]} > #{limit}")
    end
    ends = d[:offset] + d[:count].to_i * d[:entry_size]
    check(ends >= d[:offset] && ends <= data.bytesize, path, "table #{d[:kind]} out of bounds")
    ranges << [d[:offset], ends]
  end
  ranges.combination(2).each do |a, b|
    check(a[1] <= b[0] || b[1] <= a[0], path, "tables overlap")
  end
  metadata_end = ranges.map(&:last).max.to_i

  tables = {}
  dirs.each do |d|
    next unless TABLE_KINDS.key?(d[:kind])
    tables[d[:kind]] = [d[:offset], d[:count]]
  end
  check(tables.key?(1) && tables[1][1].positive?, path, "missing segment table")
  check(tables.key?(6) && tables[6][1].positive?, path, "missing string table")

  strings_offset, strings_size = tables[6]
  strings = data[strings_offset, strings_size]

  read_name = lambda do |offset|
    return nil if offset >= strings.bytesize
    finish = strings.index("\0", offset)
    return nil unless finish
    bytes = strings[offset...finish]
    return nil if bytes.empty? || bytes.bytesize > 255
    return nil unless utf8?(bytes)
    bytes
  end

  segments = []
  seg_offset, seg_count = tables[1]
  seg_count.times do |i|
    type, perms, file_offset, file_size, mem_size, rva, alignment =
      data[seg_offset + i * 48, 48].unpack("V2Q<5")
    check(SEGMENT_PERMS.key?(type), path, "segment #{i} has unknown type #{type}")
    check(perms == SEGMENT_PERMS[type], path, "segment #{i} permission mismatch")
    check((perms & 0b110) != 0b110, path, "segment #{i} is writable and executable")
    check(alignment == PAGE, path, "segment #{i} alignment #{alignment} != 4096")
    check((rva % PAGE).zero?, path, "segment #{i} RVA not page aligned")
    check(mem_size.positive? && (mem_size % PAGE).zero?,
          path, "segment #{i} memory size not page aligned")
    check(file_size <= mem_size, path, "segment #{i} file size exceeds memory size")
    check(type == 4 ? file_size.zero? : file_size.positive?,
          path, "segment #{i} zero/data file length mismatch")
    check((file_offset % 8).zero?, path, "segment #{i} file offset not aligned")
    check(file_offset >= metadata_end && file_offset <= data.bytesize,
          path, "segment #{i} file offset outside payload area")
    check(file_offset + file_size <= data.bytesize,
          path, "segment #{i} payload truncated")
    check(rva + mem_size <= image_size, path, "segment #{i} exceeds image_size")
    check(rva + mem_size > rva, path, "segment #{i} range overflows")
    segments << { type: type, rva: rva, mem: mem_size, file: file_size,
                  offset: file_offset }
  end
  segments.combination(2).each do |a, b|
    check(a[:rva] + a[:mem] <= b[:rva] || b[:rva] + b[:mem] <= a[:rva],
          path, "segment memory ranges overlap")
    next if a[:file].zero? || b[:file].zero?
    check(a[:offset] + a[:file] <= b[:offset] || b[:offset] + b[:file] <= a[:offset],
          path, "segment file ranges overlap")
  end
  check(segments.map { |s| s[:rva] + s[:mem] }.max == image_size,
        path, "image_size does not cover the segments")
  unless shared
    code = segments.select { |s| s[:type] == 1 }
    check(code.any? { |s| entry_rva >= s[:rva] && entry_rva < s[:rva] + s[:mem] },
          path, "entry_rva not inside a CODE segment")
  end

  imports = []
  if tables.key?(3)
    import_offset, import_count = tables[3]
    import_count.times do |i|
      dependency_index, name_offset, abi, iflags =
        data[import_offset + i * 16, 16].unpack("V4")
      check(read_name.call(name_offset), path, "import #{i} has invalid name")
      check(abi.positive?, path, "import #{i} ABI major zero")
      check((iflags & ~1).zero?, path, "import #{i} undefined flags")
      imports << { dependency_index: dependency_index, abi: abi }
    end
  end
  if tables.key?(2)
    dep_offset, dep_count = tables[2]
    dep_count.times do |i|
      name_offset, abi, dflags, reserved = data[dep_offset + i * 16, 16].unpack("V4")
      name = read_name.call(name_offset)
      check(name, path, "dependency #{i} has invalid name")
      if name
        check(!name.include?("/") && !name.include?("\\") && !name.include?(":"),
              path, "dependency #{i} name contains a path")
        check(name != "." && name != "..", path, "dependency #{i} name is a path")
      end
      check(abi.positive?, path, "dependency #{i} ABI major zero")
      check(dflags.zero? && reserved.zero?, path, "dependency #{i} reserved fields set")
    end
    imports.each do |imp|
      check(imp[:dependency_index] < dep_count,
            path, "import dependency index #{imp[:dependency_index]} out of range")
    end
  end
  if tables.key?(4)
    export_offset, export_count = tables[4]
    seen = {}
    export_count.times do |i|
      name_offset, abi, value_rva, size, eflags =
        data[export_offset + i * 24, 24].unpack("V2Q<V2")
      name = read_name.call(name_offset)
      check(name, path, "export #{i} has invalid name")
      check(abi.positive?, path, "export #{i} ABI major zero")
      check(eflags == 1, path, "export #{i} is not GLOBAL")
      check(value_rva < image_size, path, "export #{i} value outside image")
      check(size.zero? || size <= image_size - value_rva,
            path, "export #{i} size exceeds image")
      if name
        key = [name, abi]
        check(!seen.key?(key), path, "duplicate export #{name}")
        seen[key] = true
      end
    end
  end
  if tables.key?(5)
    reloc_offset, reloc_count = tables[5]
    reloc_count.times do |i|
      target, type, symbol_index, _addend =
        data[reloc_offset + i * 24, 24].unpack("Q<VVq<")
      check(RELOC_TYPES.include?(type), path, "relocation #{i} type #{type} unsupported")
      check((target % 8).zero?, path, "relocation #{i} target not 8-byte aligned")
      check(target + 8 <= image_size, path, "relocation #{i} target outside image")
      writable = segments.any? do |s|
        [3, 4].include?(s[:type]) && target >= s[:rva] &&
          target + 8 <= s[:rva] + s[:mem]
      end
      check(writable, path, "relocation #{i} target is not writable")
      if type == 1
        check(symbol_index.zero?, path, "RELATIVE relocation #{i} has a symbol")
      else
        check(symbol_index < imports.length,
              path, "relocation #{i} symbol index out of range")
        check(imports[symbol_index] && imports[symbol_index][:abi].positive?,
              path, "relocation #{i} import ABI invalid")
      end
    end
  end
  puts "#{path}: OK (#{segments.length} segments, #{imports.length} imports, " \
       "#{dirs.sum { |d| d[:kind] == 5 ? d[:count] : 0 }} relocations, " \
       "image #{image_size} bytes#{shared ? ', shared' : ''})"
end

exit($failures.zero? ? 0 : 1)
