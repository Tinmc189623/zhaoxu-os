#!/usr/bin/env ruby
# frozen_string_literal: true

# newlib.rb -- build newlib 4.6 for x86_64-elf with the host clang.
#
# The ZXE user ABI is position independent, so libc.a must be built with
# -fPIC.  autoconf rejects source paths containing spaces, so the tree lives
# in $HOME/newlib instead of the repository build directory.
#
#   ruby tools/newlib.rb          # download, build and install
#   ruby tools/newlib.rb --check  # print the install path if present
#
# The Ubuntu newlib-source package is used as the pinned upstream snapshot.

require "etc"
require "fileutils"
require "open3"

WORK = File.join(Dir.home, "newlib")
INSTALL = File.join(WORK, "install", "x86_64-elf")
VERSION = "4.6.0.20260123"

def run(*command, chdir: WORK, env: {})
  puts "+ #{command.join(' ')}"
  ok = system(env, *command, chdir: chdir)
  raise "command failed: #{command.join(' ')}" unless ok
end

def capture(*command, chdir: WORK, env: {})
  output, status = Open3.capture2e(env, *command, chdir: chdir)
  raise "command failed: #{command.join(' ')}\n#{output}" unless status.success?
  output
end

def check
  puts File.directory?(INSTALL) ? INSTALL : "missing"
  exit(File.directory?(INSTALL) ? 0 : 1)
end

def write_wrapper(path, body)
  File.write(path, "#!/bin/sh\n#{body}\n")
  File.chmod(0o755, path)
end

def fetch
  deb = File.join(WORK, "newlib.deb")
  unless File.file?(deb)
    run("apt-get", "download", "newlib-source")
    candidate = Dir[File.join(WORK, "newlib-source_*.deb")].max
    raise "apt-get download produced no package" unless candidate
    FileUtils.mv(candidate, deb)
  end
  FileUtils.rm_rf(File.join(WORK, "pkg"))
  FileUtils.mkdir_p(File.join(WORK, "pkg"))
  run("dpkg-deb", "-x", deb, File.join(WORK, "pkg"))
  tarball = Dir[File.join(WORK, "pkg", "usr", "src", "**", "newlib-*.tar.*")].first
  raise "newlib tarball not found in the source package" unless tarball
  src = File.join(WORK, "src")
  return if File.directory?(src)
  FileUtils.mkdir_p(src)
  run("tar", "-xf", tarball, "-C", src, "--strip-components=1")
end

def wrappers
  bin = File.join(WORK, "xbin")
  FileUtils.mkdir_p(bin)
  write_wrapper(File.join(bin, "x86_64-elf-gcc"),
                "exec clang --target=x86_64-unknown-elf -ffreestanding " \
                "-fno-builtin -fuse-ld=lld -Wl,-e,main \"$@\"")
  FileUtils.cp(File.join(bin, "x86_64-elf-gcc"),
               File.join(bin, "x86_64-elf-cc"))
  write_wrapper(File.join(bin, "x86_64-elf-as"),
                "exec clang --target=x86_64-unknown-elf -c -x assembler \"$@\"")
  write_wrapper(File.join(bin, "x86_64-elf-ar"), "exec llvm-ar-21 \"$@\"")
  write_wrapper(File.join(bin, "x86_64-elf-ranlib"), "exec llvm-ranlib-21 \"$@\"")
  write_wrapper(File.join(bin, "x86_64-elf-objcopy"), "exec llvm-objcopy-21 \"$@\"")
end

def build
  FileUtils.mkdir_p(WORK)
  fetch
  wrappers
  build_dir = File.join(WORK, "build")
  FileUtils.rm_rf(build_dir)
  FileUtils.rm_rf(File.join(WORK, "install"))
  FileUtils.mkdir_p(build_dir)
  env = { "PATH" => "#{File.join(WORK, 'xbin')}:#{ENV['PATH']}",
          "CFLAGS_FOR_TARGET" => "-fPIC -O2" }
  run(File.join(WORK, "src", "configure"),
      "--target=x86_64-elf", "--prefix=#{File.join(WORK, 'install')}",
      "--disable-multilib", "--disable-newlib-supplied-syscalls",
      "--disable-libgloss", "--disable-newlib-io-float",
      "--enable-newlib-io-long-long", "--enable-newlib-io-c99-formats",
      chdir: build_dir, env: env)
  run("make", "-j#{Etc.nprocessors}", chdir: build_dir, env: env)
  run("make", "install", chdir: build_dir, env: env)
  puts "newlib installed: #{INSTALL}"
end

if ARGV.include?("--check")
  check
else
  build
end
