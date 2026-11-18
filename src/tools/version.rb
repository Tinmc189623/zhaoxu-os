#!/usr/bin/env ruby
# version.rb -- sync version.cfg (repo root) into every place the product
# surfaces it.  Run after editing version.cfg:
#   ruby tools/version.rb          (from src/, or via `make version`)
#
# version.cfg is the single source of truth.  Its schema is:
#
#   name           = "Zhaoxu OS"   OS display name                 (required)
#   name_cn        = null          optional Chinese OS name
#   kernel         = "Yuanshuo"    kernel display name             (required)
#   kernel_cn      = null          optional Chinese kernel name
#   os_version     = 0.5.0         OS version: README, motd, ISO   (required)
#   kernel_version = 0.5.0         kernel version: version.h       (required)
#   build_date     = "2026-11-16"  build date, YYYY-MM-DD           (required)
#   codename       = "Yuanshuo"    release codename                (required)
#
# Values may be quoted strings, bare numbers, or null.  `#` starts a trailing
# comment outside quotes.

require "date"

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

# Reject the old single `version` key loudly: os_version and kernel_version
# are separate fields in the version.cfg schema.
if CFG.key?("version") && (!CFG.key?("os_version") || !CFG.key?("kernel_version"))
  abort "version.cfg: use 'os_version' and 'kernel_version', not 'version'"
end

# Reject the retired `build` counter: the schema carries a build date instead.
if CFG.key?("build") && !CFG.key?("build_date")
  abort "version.cfg: use 'build_date' (YYYY-MM-DD), not 'build'"
end

OS_VERSION = required("os_version")
KERNEL_VERSION = required("kernel_version")
BUILD_DATE = required("build_date")
CODENAME = required("codename")
KERNEL = required("kernel")
NAME = required("name")
NAME_CN = CFG["name_cn"] || NAME

[["os_version", OS_VERSION], ["kernel_version", KERNEL_VERSION]].each do |key, value|
  abort "version.cfg: #{key} '#{value}' is not a dotted version" unless value =~ /\A\d+(\.\d+)+\z/
end
# A build date must be a real calendar day in YYYY-MM-DD form; Date.iso8601
# rejects month 13 and day 30 in February, the round trip rejects zero padding.
begin
  build_day = Date.iso8601(BUILD_DATE)
rescue ArgumentError
  abort "version.cfg: build_date '#{BUILD_DATE}' is not a valid date"
end
unless BUILD_DATE =~ /\A\d{4}-\d{2}-\d{2}\z/ && build_day.strftime("%Y-%m-%d") == BUILD_DATE
  abort "version.cfg: build_date '#{BUILD_DATE}' is not YYYY-MM-DD"
end

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
  [%r{/\* Synced from [^*]*\*/},
   "/* Synced from version.cfg by tools/version.rb (make version). */"],
  [/#define ZHAOXU_VERSION\s+"[^"]*"/,
   "#define ZHAOXU_VERSION         \"#{OS_VERSION}\""],
  [/#define ZHAOXU_CODENAME\s+"[^"]*"/,
   "#define ZHAOXU_CODENAME        \"#{CODENAME}\""],
  [/#define ZHAOXU_KERNEL\s+"[^"]*"/,
   "#define ZHAOXU_KERNEL          \"#{KERNEL}\""],
  [/#define ZHAOXU_KERNEL_VERSION\s+"[^"]*"/,
   "#define ZHAOXU_KERNEL_VERSION  \"#{KERNEL_VERSION}\""],
  [/#define ZHAOXU_BUILD\s+"[^"]*"/,
   "#define ZHAOXU_BUILD           \"#{BUILD_DATE}\""],
])

patch(File.join(src, "initramfs", "etc", "motd"), [
  [/Welcome to #{Regexp.escape(NAME)} [0-9.]+ "[^"]*"( \([^)]*\))?\./,
   "Welcome to #{NAME} #{OS_VERSION} \"#{CODENAME}\" (build #{BUILD_DATE})."],
  [/^欢迎使用[^\n]*? [0-9.]+（[^）]*）/,
   "欢迎使用 #{NAME_CN} #{OS_VERSION}（构建 #{BUILD_DATE}）"],
])

patch(File.join(src, "initramfs", "etc", "version"), [
  [/\A#{Regexp.escape(NAME)}[^\n]*/,
   "#{NAME} #{OS_VERSION} \"#{CODENAME}\" build #{BUILD_DATE}"],
])

patch(File.join(ROOT, "README.md"), [
  [/\*\*Current version: [0-9.]+ 「[^」]*」, kernel [0-9.]+, build [0-9-]+\*\*/,
   "**Current version: #{OS_VERSION} 「#{CODENAME}」, kernel #{KERNEL_VERSION}, " \
   "build #{BUILD_DATE}**"],
])

puts "version.cfg -> #{NAME} #{OS_VERSION} \"#{CODENAME}\", " \
     "#{KERNEL} kernel #{KERNEL_VERSION}, build #{BUILD_DATE} synchronized"
