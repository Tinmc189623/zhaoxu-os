#!/usr/bin/env ruby
# frozen_string_literal: true

# bash.rb -- cross-build GNU bash 5.3 for Zhaoxu OS.
#
# The source is vendored in third_party/bash (bash-5.3.tar.gz from
# https://ftp.gnu.org/gnu/bash/, signed by Chet Ramey, key
# 7C0135FB088AAF6C66C650B9BB5869F064EA74AB).
#
# autoconf rejects source paths containing spaces, so the vendored tree is
# copied to $HOME/bash-build/source and configured out of tree against the
# newlib install prepared by tools/newlib.rb.
#
#   ruby tools/bash.rb          # copy, configure and build out/bash.elf
#   ruby tools/bash.rb --check  # report whether out/bash.elf exists
#
# Run inside WSL2 with clang, lld and the newlib install present.

require "etc"
require "fileutils"

ROOT = File.expand_path("..", __dir__)
VENDORED = File.join(ROOT, "third_party", "bash")
WORK = File.join(Dir.home, "bash-build")
SOURCE = File.join(WORK, "source")
BUILD = File.join(WORK, "build")
XBIN = File.join(WORK, "xbin")
REPO = File.join(WORK, "repo")
NEWLIB = ENV["NEWLIB_ROOT"] || File.join(Dir.home, "newlib", "install", "x86_64-elf")
USER_LD = File.join(REPO, "user", "elf.ld")
POSIX_NAMES = %w[libgloss posix_termios posix_signal posix_account posix_path
                 posix_dirent posix_rlimit posix_time posix_select].freeze
OBJDIR = File.join(REPO, "build", "newlib")
CRT0 = File.join(OBJDIR, "crt0.o")
POSIX_OBJS = POSIX_NAMES.map { |name| File.join(OBJDIR, "#{name}.o") }

# Run a command and abort on failure.
def run(*command, chdir: WORK, env: {})
  puts "+ #{command.join(' ')}"
  ok = system(env, *command, chdir: chdir)
  raise "command failed: #{command.join(' ')}" unless ok
end

# Write an executable Ruby shim for the tools autoconf invokes by name.
def wrapper(name, body)
  path = File.join(XBIN, name)
  File.write(path, "#!/usr/bin/env ruby\n#{body}\n")
  File.chmod(0o755, path)
end

# Bash's !HAVE_NETWORK fallback calls internal_error()/gettext without
# including their headers, which fails on a target with no network stack.
# Report ENOSYS instead; Zhaoxu has no network stack yet.
def patch_network_fallback
  path = File.join(SOURCE, "lib", "sh", "netopen.c")
  text = File.read(path)
  old = "  internal_error (_(\"network operations not supported\"));\n  return -1;"
  new = "  errno = ENOSYS;\n  return -1;"
  raise "netopen.c fallback not found" unless text.include?(old)
  text = text.sub(old, new)
  text = text.sub("#else /* !HAVE_NETWORK */", "#else /* !HAVE_NETWORK */\n#include <errno.h>")
  File.write(path, text)
end

# Create the cross-tool shims.  Unlike the newlib wrappers, the compiler does
# not force -Wl,-e,main: ZXE binaries enter through crt0's _start.
def wrappers
  FileUtils.mkdir_p(XBIN)
  wrapper("x86_64-elf-gcc",
          'exec "clang", "--target=x86_64-unknown-elf", "-fuse-ld=lld", *ARGV')
  wrapper("x86_64-elf-cc",
          'exec "clang", "--target=x86_64-unknown-elf", "-fuse-ld=lld", *ARGV')
  wrapper("x86_64-elf-as",
          'exec "clang", "--target=x86_64-unknown-elf", "-c", ' \
          '"-x", "assembler", *ARGV')
  wrapper("x86_64-elf-ar", 'exec "llvm-ar-21", *ARGV')
  wrapper("x86_64-elf-ranlib", 'exec "llvm-ranlib-21", *ARGV')
  wrapper("x86_64-elf-objcopy", 'exec "llvm-objcopy-21", *ARGV')
  wrapper("x86_64-elf-cpp",
          'exec "clang", "--target=x86_64-unknown-elf", "-E", ' \
          '"-ffreestanding", "-D_POSIX_C_SOURCE=200809L", "-D_GNU_SOURCE", ' \
          "\"-I#{File.join(NEWLIB, 'include')}\", " \
          "\"-I#{File.join(REPO, 'user', 'lib', 'posix', 'include')}\", " \
          "*ARGV")
end

# Copy the vendored source to a space-free path and build the project's
# POSIX objects that bash links against.
def prepare
  raise "newlib is missing; run: ruby tools/newlib.rb" unless
    File.file?(File.join(NEWLIB, "lib", "libc.a"))
  FileUtils.mkdir_p(WORK)
  FileUtils.rm_f(REPO)
  FileUtils.ln_s(ROOT, REPO)
  FileUtils.rm_rf(SOURCE)
  FileUtils.mkdir_p(SOURCE)
  run("cp", "-a", "#{VENDORED}/.", SOURCE)
  patch_network_fallback
  targets = POSIX_NAMES.map { |name| "build/newlib/#{name}.o" } +
            ["build/newlib/crt0.o"]
  run("make", "-C", REPO, "-j#{Etc.nprocessors}", *targets)
  wrappers
end

# Configure and build bash against newlib, then pack the ZXE image.
def build
  prepare
  FileUtils.rm_rf(BUILD)
  FileUtils.mkdir_p(BUILD)

  cflags = [
    "-O2", "-std=gnu11", "-ffreestanding", "-fPIE", "-fno-stack-protector",
    "-fno-asynchronous-unwind-tables", "-mno-red-zone",
    "-D_POSIX_C_SOURCE=200809L", "-D_GNU_SOURCE", "-D_POSIX_VERSION=200809L",
    "-I#{File.join(REPO, 'user', 'lib', 'posix', 'include')}",
    "-I#{File.join(NEWLIB, 'include')}"
  ].join(" ")
  ldflags = [
    "-nostdlib", "-Wl,--no-pie,-Bsymbolic,--build-id=none",
    "-Wl,-T,#{USER_LD}", "-Wl,-z,max-page-size=0x1000",
    # Executable links still accept undefined symbols in some cases; keep the
    # AC_CHECK_FUNCS probes strict.
    "-Wl,--no-undefined"
  ].join(" ")
  libs = [CRT0, *POSIX_OBJS,
          File.join(NEWLIB, "lib", "libc.a"),
          File.join(NEWLIB, "lib", "libm.a")].join(" ")
  env = {
    "PATH" => "#{XBIN}:#{ENV['PATH']}",
    "CC" => "x86_64-elf-gcc",
    # Without an explicit CPP, configure probes headers with the host cpp and
    # finds glibc headers that do not exist for the target.
    "CPP" => File.join(XBIN, "x86_64-elf-cpp"),
    "CFLAGS" => cflags,
    "LDFLAGS" => ldflags,
    "LIBS" => libs,
    "CC_FOR_BUILD" => "cc",
    "CFLAGS_FOR_BUILD" => "-O2 -std=gnu17",
    # clang treats strchr as a builtin, so gnulib's undeclared-builtin probe
    # cannot distinguish the two cases and aborts; the answer it wants is
    # "none needed".
    "ac_cv_c_undeclared_builtin_options" => "none needed",
    # newlib has no sigjmp_buf/sigsetjmp; cross configure otherwise assumes
    # POSIX signals are present.
    "bash_cv_func_sigsetjmp" => "missing",
    # The Zhaoxu POSIX glue implements sigaction/sigprocmask.
    "bash_cv_posix_signals" => "yes",
    # posix_time.c provides times(); the kernel reports no CPU accounting.
    "ac_cv_func_times" => "yes",
    # posix_path.c implements getcwd(NULL, 0) by allocating the result.
    "bash_cv_getcwd_malloc" => "yes",
    # Link bash's bundled termcap; the host's -ltermcap is not a target library.
    "bash_cv_termcap_lib" => "gnutermcap"
  }

  run(File.join(SOURCE, "configure"),
      "--host=x86_64-elf",
      "--build=x86_64-pc-linux-gnu",
      "--without-bash-malloc",
      "--disable-nls",
      "--disable-job-control",
      "--without-curses",
      chdir: BUILD, env: env)
  # Host tools must not link the target crt0/libc; their generated Makefiles
  # append $(LIBS), which belongs only on the bash link line.
  %w[support builtins].each do |dir|
    makefile = File.join(BUILD, dir, "Makefile")
    next unless File.file?(makefile)
    text = File.read(makefile).gsub(" $(LIBS)", "")
    text = text.gsub(/^LIBS_FOR_BUILD = .*$/, "LIBS_FOR_BUILD =")
    File.write(makefile, text)
  end
  run("make", "-j#{Etc.nprocessors}", chdir: BUILD, env: env)

  binary = File.join(BUILD, "bash")
  raise "bash binary was not produced" unless File.file?(binary)
  output = File.join(REPO, "out", "bash.elf")
  FileUtils.cp(binary, output)
  puts "bash ELF: out/bash.elf"
end

if ARGV.include?("--check")
  path = File.join(ROOT, "out", "bash.elf")
  puts File.file?(path) ? path : "missing"
  exit(File.file?(path) ? 0 : 1)
else
  build
end
