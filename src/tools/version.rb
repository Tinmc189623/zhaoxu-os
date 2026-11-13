#!/usr/bin/env ruby
# version.rb -- sync the version from version.cfg (repo root) into every place
# the product surfaces it. Run after editing version.cfg:
#   ruby tools/version.rb          (from src/, or via `make version`)
#
# version.cfg is a flat `key = value` file: values may be quoted strings,
# bare numbers, or null. `#` starts a trailing comment outside quotes.

ROOT = File.expand_path("../..", __dir__)
src = File.join(ROOT, "src")
cfg_path = File.join(ROOT, "version.cfg")
abort "missing #{cfg_path}" unless File.file?(cfg_path)

def parse_cfg(path)
  values = {}
  File.readlines(path, chomp: true).each do |line|
    line = line.strip
    next if line.empty? || line.start_with?("#")
    key, rest = line.split("=", 2)
    next unless rest
    key = key.strip
    value = rest.strip
    if value.start_with?('"')
      finish = value.index('"', 1)
      abort "#{path}: unterminated string for #{key}" unless finish
      value = value[1...finish]
    else
      value = value.sub(/\s+#.*\z/, "").strip
      value = nil if value == "null" || value.empty?
    end
    values[key] = value
  end
  values
end

CFG = parse_cfg(cfg_path)

def required(key)
  value = CFG[key]
  abort "version.cfg: missing '#{key}'" if value.nil? || value.empty?
  value
end

VERSION = required("version")
CODENAME = required("codename")
KERNEL = required("kernel")
NAME = required("name")
KERNEL_CN = CFG["kernel_cn"] || KERNEL

def patch(path, pairs)
  text = File.read(path)
  pairs.each do |pattern, replacement|
    unless pattern.match?(text)
      abort "#{path}: pattern not found: #{pattern.source}"
    end
    text = text.sub(pattern, replacement)
  end
  File.write(path, text)
  puts "updated #{path.sub(ROOT + '/', '')}"
end

patch(File.join(src, "kernel", "include", "version.h"), [
  [/#define ZHAOXU_VERSION\s+"[^"]*"/,
   "#define ZHAOXU_VERSION  \"#{VERSION}\""],
  [/#define ZHAOXU_CODENAME\s+"[^"]*"/,
   "#define ZHAOXU_CODENAME \"#{CODENAME}\""],
  [/#define ZHAOXU_KERNEL\s+"[^"]*"/,
   "#define ZHAOXU_KERNEL   \"#{KERNEL}\""],
])

patch(File.join(src, "initramfs", "etc", "motd"), [
  [/Welcome to #{Regexp.escape(NAME)} [0-9.]+ "[^"]*"\./,
   "Welcome to #{NAME} #{VERSION} \"#{CODENAME}\"."],
  [/欢迎使用兆旭操作系统 [0-9.]+（[^）]*）/,
   "欢迎使用兆旭操作系统 #{VERSION}（#{KERNEL_CN}内核）"],
])

patch(File.join(src, "initramfs", "etc", "version"), [
  [/\A#{Regexp.escape(NAME)} [0-9.]+ "[^"]*"/,
   "#{NAME} #{VERSION} \"#{CODENAME}\""],
])

patch(File.join(ROOT, "README.md"), [
  [/\*\*当前版本：[0-9.]+ 「[^」]*」\*\*/,
   "**当前版本：#{VERSION} 「#{CODENAME}」**"],
])

puts "version.cfg -> #{VERSION} \"#{CODENAME}\" synchronized"
