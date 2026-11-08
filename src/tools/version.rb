#!/usr/bin/env ruby
# version.rb -- sync the version from version.php (repo root) into every
# place the product surfaces it. Run after editing version.php:
#   ruby tools/version.rb          (from src/, or via `make version`)
#
# Pure Ruby, no PHP runtime: version.php is parsed as text.

ROOT = File.expand_path("../..", __dir__)
src = File.join(ROOT, "src")
php = File.read(File.join(ROOT, "version.php"))

def field(php, key)
  match = php.match(/'#{key}'\s*=>\s*'([^']+)'/)
  abort("version.php: missing '#{key}'") unless match
  match[1]
end

VERSION = field(php, "version")
CODENAME = field(php, "codename")
KERNEL = field(php, "kernel")
KERNEL_CN = field(php, "kernel_cn")
NAME = field(php, "name")

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

patch(File.join(src, "kernel", "main.c"), [
  [/#define ZHAOXU_VERSION "[^"]*"/,
   "#define ZHAOXU_VERSION \"#{VERSION}\""],
  [/#define ZHAOXU_CODENAME "[^"]*"/,
   "#define ZHAOXU_CODENAME \"#{CODENAME}\""],
])

patch(File.join(src, "apps", "shell.cpp"), [
  [/constexpr const char \*kVersion = "[^"]*";/,
   "constexpr const char *kVersion = \"#{VERSION}\";"],
  [/constexpr const char \*kCodename = "[^"]*";/,
   "constexpr const char *kCodename = \"#{CODENAME}\";"],
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
  [/当前版本 \*\*[0-9.]+\*\*/,
   "当前版本 **#{VERSION}**"],
])

puts "version.php -> #{VERSION} \"#{CODENAME}\" synchronized"
