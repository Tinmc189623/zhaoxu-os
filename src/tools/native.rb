# frozen_string_literal: true
#
# native.rb -- build Zhaoxu OS on a native Windows host without MSYS2/make.
#
# The regular build path is the GNU Makefile.  On Windows the toolchain may
# exist without GNU make (LLVM + FASM + Ruby + Git's tar), so this driver runs
# the same compile/link/package steps directly.  It is pure Ruby (rule 6).
#
# Supported targets:
#   all     BOOTX64.EFI, OSLDRX64.efi, kernel.elf and esp/  (make all)
#   clean   remove build/out/esp artifacts
# ISO targets need xorriso, mkfs.vfat, mtools and mksquashfs and are rejected
# here; they remain the Makefile's job on hosts that have those tools.
#
# Ada drivers are not compiled by this driver (no GNAT on a plain Windows
# host).  The release step reuses the drivers.zxe produced by the last Ada
# build; the driver fails loudly if that artifact is missing.

require "fileutils"
require "open3"
require "rbconfig"

module ZosNative
  SRC = File.expand_path("..", __dir__)

  class BuildError < StandardError; end

  class << self
    # ---- tool discovery -------------------------------------------------

    def llvm_bin(env = ENV)
      candidates = [env["LLVM_BIN"], "C:/Program Files/LLVM/bin"]
      candidates.compact.find { |dir| File.file?(File.join(dir, "clang.exe")) }
    end

    def tools(env = ENV)
      bin = llvm_bin(env)
      raise BuildError, "LLVM not found (set LLVM_BIN to the directory holding clang.exe)" unless bin

      {
        clang: File.join(bin, "clang.exe"),
        clangxx: File.join(bin, "clang++.exe"),
        ld_lld: File.join(bin, "ld.lld.exe"),
        lld_link: File.join(bin, "lld-link.exe"),
        objcopy: File.join(bin, "llvm-objcopy.exe"),
        ruby: RbConfig.ruby,
        fasm: fasm_path(env),
        tar: tar_path(env),
      }
    end

    def fasm_path(env = ENV)
      candidates = [env["FASM"], File.join(SRC, "tools", "fasm.exe"),
                    "D:/fasm/FASM.EXE", "C:/fasm/FASM.EXE"]
      candidates.compact.find { |path| File.file?(path) }
    end

    def tar_path(env = ENV)
      candidates = [env["ZOS_TAR"], "C:/Program Files/Git/usr/bin/tar.exe",
                    "C:/Program Files (x86)/Git/usr/bin/tar.exe"]
      candidates.compact.find { |path| File.file?(path) }
    end

    def available?(env = ENV)
      tools_list = tools(env)
      %i[clang clangxx ld_lld lld_link objcopy ruby fasm tar].all? { |k| tools_list[k] }
    rescue BuildError
      false
    end

    def missing(env = ENV)
      list = tools(env)
      list.select { |_k, v| v.nil? }.keys.map(&:to_s)
    rescue BuildError => error
      [error.message]
    end

    # ---- CLI ------------------------------------------------------------

    def main(argv)
      target = argv.first || "all"
      case target
      when "all"
        build_all
      when "clean"
        clean
      when "host-test"
        run([RbConfig.ruby, "tests/tools/host_test.rb"])
      else
        warn "native build: target '#{target}' needs the Makefile toolchain " \
             "(make + Ada GNAT + xorriso/mtools/squashfs for ISO); " \
             "this no-make driver supports: all, clean, host-test"
        return 1
      end
      0
    rescue BuildError => error
      warn "native build: #{error.message}"
      1
    end

    # ---- build ----------------------------------------------------------

    def build_all
      @tools = tools
      prepare_directories
      write_headless_header
      build_uefi
      build_userland
      build_bios
      build_initramfs
      link_kernel
      copy_esp
      report
    end

    def prepare_directories
      %w[build out esp/EFI/BOOT build/iso].each do |dir|
        FileUtils.mkdir_p(File.join(SRC, dir))
      end
    end

    def write_headless_header
      File.write(File.join(SRC, "build", "headless.h"), "#define BUILD_HEADLESS 0\n")
    end

    def build_uefi
      cc = @tools[:clang]
      ld = @tools[:lld_link]
      boot_cflags = [
        "-target", "x86_64-pc-win32", "-m64", "-O2", "-ffreestanding",
        "-fno-builtin", "-fno-stack-protector", "-mno-stack-arg-probe",
        "-mno-red-zone", "-fno-asynchronous-unwind-tables", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-DHEADLESS=0",
      ]
      boot_ldflags = ["/subsystem:efi_application", "/nodefaultlib", "/debug:none"]
      compile("boot/efi_bootmgr.c", "build/efi_bootmgr.o", boot_cflags, cc: cc)
      compile("boot/efi_main.c", "build/efi_main.o", boot_cflags, cc: cc)
      link_efi("build/efi_bootmgr.o", "out/BOOTX64.EFI", "efi_start", ld, boot_ldflags)
      link_efi("build/efi_main.o", "out/OSLDRX64.efi", "osloader_start", ld, boot_ldflags)
      FileUtils.cp(File.join(SRC, "out", "OSLDRX64.efi"),
                   File.join(SRC, "out", "osloader.elf")) if false
    end

    def link_efi(object, output, entry, ld, flags)
      run([ld, *flags, "/entry:#{entry}", object, "/out:#{output}"])
    end

    # ---- userland -------------------------------------------------------

    def build_userland
      build_ade
      build_csh
      build_ext4fs
      build_user_program("init", ["user/init.c"], [], "build/init.uelf", "out/init.zxe")
      build_user_program("hello", ["user/hello.c"], [], "build/hello.uelf", "out/hello.zxe")
      build_user_program("ping", ["user/ping.c"], [], "build/ping.uelf", "out/ping.zxe")
      build_user_program("pong", ["user/pong.c"], [], "build/pong.uelf", "out/pong.zxe")
      build_user_program("welcome_client", make_var("WELCOME_SRCS"),
                         ["-Iuser", "-Iuser/lib/include", "-Ikernel/include"],
                         "build/welcome_client.uelf", "out/welcome_client.zxe")
      build_user_program("calc_client", make_var("CALC_SRCS"),
                         ["-Iuser", "-Iuser/lib/include", "-Ikernel/include"],
                         "build/calc_client.uelf", "out/calc_client.zxe")
      build_user_program("install_client", make_var("INSTALL_SRCS"),
                         ["-Iuser", "-Iuser/lib/include", "-Ikernel/include"],
                         "build/install_client.uelf", "out/install_client.zxe")
    end

    def build_user_program(name, sources, includes, uelf, zxe)
      objects = with_userlib(sources).map do |source|
        object = "build/#{name}/" + source.sub(/\.c\z/, ".o")
        compile(source, object, user_cflags + includes)
        object
      end
      link_shared(objects, uelf)
      run([@tools[:ruby], "tools/zxe-pack.rb", uelf, zxe])
    end

    # The pattern rule links userlib.c for single-file programs; the client
    # source lists already include it.
    def with_userlib(sources)
      return sources if sources.include?("user/userlib.c")

      sources + ["user/userlib.c"]
    end

    def link_shared(objects, uelf)
      run([@tools[:ld_lld], "-m", "elf_x86_64", "-shared", "-Bsymbolic",
           "--build-id=none", "-T", user_link_script,
           "-z", "max-page-size=0x1000", "-z", "norelro", "--no-rosegment",
           "-o", uelf, *objects])
    end

    # lld rejects the stock user.ld because .dynamic would give the .data
    # output section SHT_DYNAMIC and then .got has the wrong type.  Giving
    # .dynamic its own output section keeps all three PT_LOAD segments and
    # the resulting file is still ET_DYN.  The repository linker script is
    # left untouched for GNU ld hosts.
    def user_link_script
      script = File.join(SRC, "build", "user.ld")
      return script if File.file?(script)

      text = File.read(File.join(SRC, "user", "user.ld"))
      text = text.sub("        *(.dynamic)\n", "")
      text = text.sub("    .data : ALIGN(4096)",
                      "    .dynamic : ALIGN(4096)\n" \
                      "    {\n        *(.dynamic)\n    } :data\n\n" \
                      "    .data : ALIGN(4096)")
      FileUtils.mkdir_p(File.dirname(script))
      File.write(script, text)
      script
    end

    def build_ade
      ade_build = "build/ade"
      objects = []
      make_var("ADE_C_SRCS").each do |source|
        object = File.join(ade_build, source.sub(/\.c\z/, ".o"))
        compile(source, object, ade_cflags)
        objects << object
      end
      make_var("ADE_FONT_SRCS").each do |source|
        object = File.join(ade_build, source.sub(/\.c\z/, ".o"))
        compile(source, object, ade_font_cflags)
        objects << object
      end
      make_var("ADE_CXX_SRCS").each do |source|
        object = File.join(ade_build, source.sub(/\.cpp\z/, ".o"))
        compile(source, object, ade_cxxflags, cc: @tools[:clangxx])
        objects << object
      end
      link_shared(objects, "build/aqua.uelf")
      run([@tools[:ruby], "tools/zxe-pack.rb", "build/aqua.uelf", "out/aqua.zxe"])
    end

    def build_csh
      objects = []
      make_var("CSH_SRCS").each do |source|
        object = "build/csh/" + source.sub(/\.c\z/, ".o")
        compile(source, object, user_cflags + %w[-Iapps/csh -Iuser -Ikernel/include])
        objects << object
      end
      link_shared(objects, "build/8sh.uelf")
      run([@tools[:ruby], "tools/zxe-pack.rb", "build/8sh.uelf", "out/8sh.zxe"])
    end

    def build_ext4fs
      objects = []
      make_var("EXT4FS_SRCS").each do |source|
        object = "build/ext4fs/" + source.sub(/\.c\z/, ".o")
        compile(source, object,
                user_cflags + %w[-Ifs/ext4 -Iapps/ext4fs -Iuser -Ikernel/include])
        objects << object
      end
      link_shared(objects, "build/ext4fs.uelf")
      run([@tools[:ruby], "tools/zxe-pack.rb", "build/ext4fs.uelf",
           "out/ext4fs.zxe"])
    end

    # ---- BIOS chain -----------------------------------------------------

    def build_bios
      fasm = @tools[:fasm]
      run([fasm, "-dHEADLESS=0", "boot/bios_stage2.asm", "out/bios_stage2.bin"])
      size = File.size(File.join(SRC, "out", "bios_stage2.bin"))
      sectors = (size + 511) / 512
      File.write(File.join(SRC, "build", "bios_stage1.inc"),
                 format("STAGE2_SECTORS = %u\n", sectors))
      run([fasm, "boot/bios_stage1.asm", "out/bios_stage1.bin"])
      run([@tools[:ruby], "tools/bios-img.rb", "out/bios_stage1.bin",
           "out/bios_stage2.bin", "out/bios.img"])
      blob_object("out/bios_stage1.bin", "build/bios_stage1.o", "bios_stage1",
                  ".biosblob")
      blob_object("out/bios_stage2.bin", "build/bios_stage2.o", "bios_stage2",
                  ".biosblob")
    end

    def blob_object(input, output, prefix, section)
      # objcopy's binary-input symbol is _binary_<path with every
      # non-alphanumeric character turned into '_'>_start.
      symbol = input.gsub(/[^A-Za-z0-9]/, "_")
      run([@tools[:objcopy], "-I", "binary", "-O", "elf64-x86-64",
           "-B", "i386:x86-64",
           "--rename-section=.data=#{section},alloc,load,readonly,data,contents",
           "--redefine-sym=_binary_#{symbol}_start=#{prefix}_start",
           "--redefine-sym=_binary_#{symbol}_end=#{prefix}_end",
           "--redefine-sym=_binary_#{symbol}_size=#{prefix}_size",
           input, output])
    end

    # ---- initramfs in the kernel ---------------------------------------

    def build_initramfs
      tree = File.join(SRC, "build", "initramfs")
      FileUtils.rm_rf(tree)
      directory(tree, "bin")
      directory(tree, "bin/apps")
      directory(tree, "boot")
      directory(tree, "etc")
      directory(tree, "mnt")
      directory(tree, "data")
      directory(tree, "data/DOCS")
      copy("out/init.zxe", "#{tree}/bin/init")
      copy("out/aqua.zxe", "#{tree}/bin/aqua")
      copy("out/8sh.zxe", "#{tree}/bin/8sh")
      copy("out/ext4fs.zxe", "#{tree}/bin/ext4fs")
      copy("out/welcome_client.zxe", "#{tree}/bin/apps/welcome.zxe")
      copy("out/calc_client.zxe", "#{tree}/bin/apps/calc.zxe")
      copy("out/install_client.zxe", "#{tree}/bin/apps/install.zxe")
      copy("out/BOOTX64.EFI", "#{tree}/boot/BOOTX64.EFI")
      copy("initramfs/etc/motd", "#{tree}/etc/motd")
      copy("initramfs/etc/version", "#{tree}/etc/version")
      copy("initramfs/data/hello.txt", "#{tree}/data/HELLO.TXT")
      copy("initramfs/data/info.txt", "#{tree}/data/DOCS/INFO.TXT")
      copy("out/hello.zxe", "#{tree}/data/HELLO.ZXE")
      copy("build/hello.uelf", "#{tree}/data/HELLO.ELF")
      copy("out/ping.zxe", "#{tree}/data/PING.ZXE")
      copy("out/pong.zxe", "#{tree}/data/PONG.ZXE")
      copy_drivers("#{tree}/bin/drivers")
      run([@tools[:tar], "--format=ustar", "--owner=0", "--group=0",
           "--numeric-owner", "-cf", "out/initramfs.tar", "-C",
           "build/initramfs", "."])
      blob_object("out/initramfs.tar", "build/initramfs.o", "initramfs",
                  ".initramfs")
    end

    # Drivers are Ada; a native Windows host without GNAT reuses the artifact
    # from the last full build instead of silently shipping an empty binary.
    def copy_drivers(destination)
      source = File.join(SRC, "out", "drivers.zxe")
      unless File.file?(source)
        raise BuildError,
              "out/drivers.zxe is missing and this host has no GNAT Ada " \
              "compiler; build the drivers on a host with gnat-14 first"
      end
      FileUtils.cp(source, destination)
    end

    # ---- kernel ---------------------------------------------------------

    def link_kernel
      objects = []
      make_var("C_SRCS").each do |source|
        object = "build/" + source.sub(/\.c\z/, ".o")
        compile(source, object, kernel_cflags)
        objects << object
      end
      make_var("ASM_SRCS").each do |source|
        object = "build/" + source.sub(/\.S\z/, ".o")
        run([@tools[:clang], "--target=x86_64-elf", "-m64", "-c", source,
             "-o", object])
        objects << object
      end
      objects << "build/initramfs.o"
      objects << "build/bios_stage1.o"
      objects << "build/bios_stage2.o"
      run([@tools[:ld_lld], "-m", "elf_x86_64", "-T", "kernel/linker.ld",
           "-nostdlib", "-static", "--build-id=none",
           "-z", "max-page-size=0x1000", "-o", "out/kernel.elf", *objects])
    end

    def copy_esp
      FileUtils.cp(File.join(SRC, "out", "BOOTX64.EFI"),
                   File.join(SRC, "esp/EFI/BOOT/BOOTX64.EFI"))
      FileUtils.cp(File.join(SRC, "out", "OSLDRX64.efi"),
                   File.join(SRC, "esp/EFI/BOOT/OSLDRX64.efi"))
    end

    def report
      puts "native build: all"
      %w[out/BOOTX64.EFI out/OSLDRX64.efi out/kernel.elf
         out/initramfs.tar esp/EFI/BOOT/BOOTX64.EFI].each do |path|
        size = File.size(File.join(SRC, path))
        puts format("  %-28s %8d bytes", path, size)
      end
    end

    # ---- flags ----------------------------------------------------------

    def cross
      ["--target=x86_64-elf"]
    end

    def kernel_cflags
      cross + %w[
        -m64 -std=gnu11 -O2 -g -ffreestanding -fno-pie -fno-pic
        -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
        -mcmodel=kernel -mgeneral-regs-only -mno-sse -mno-sse2 -mno-mmx
        -mno-avx -Wall -Wextra -Wno-unused-parameter
        -Ikernel/include -Ikernel -Ikernel/font
        -DPROC_SELFTEST=0 -DDEBUG_SERIAL=0 -DSCHED_STATS=0
      ]
    end

    def user_cflags
      cross + %w[
        -m64 -std=gnu11 -O2 -ffreestanding -fpie
        -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone
        -mno-sse -mno-sse2 -mno-mmx -mno-avx -Wall -Wextra
        -Wno-unused-parameter
      ]
    end

    def user_ldflags
      cross + %w[
        -fuse-ld=lld -nostdlib -shared -Wl,-Bsymbolic,--build-id=none
        -Wl,-T,user/user.ld -Wl,-z,max-page-size=0x1000
      ]
    end

    def ade_cflags
      user_cflags + %w[
        -I. -Iuser -Iuser/lib/include -Ikernel/include -Ikernel -Ikernel/font
        -DZOS_USER
      ]
    end

    def ade_font_cflags
      (ade_cflags - %w[-mno-sse -mno-sse2]) + %w[-msse2 -mno-avx -fno-math-errno]
    end

    def ade_cxxflags
      (ade_cflags - %w[-std=gnu11]) + %w[
        -std=gnu++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics
        -fno-use-cxa-atexit
      ]
    end

    # ---- Makefile source lists -----------------------------------------

    def makefile_text
      @makefile_text ||= File.read(File.join(SRC, "Makefile"))
    end

    # Read `NAME := a b \` continuation lists; only plain file lists are used
    # here (the Ada/ISO lists stay in the Makefile).
    def make_var(name)
      lines = makefile_text.lines
      start = lines.index { |line| line =~ /\A#{Regexp.escape(name)}\s*:?=\s*/ }
      raise BuildError, "Makefile variable #{name} not found" unless start

      joined = +""
      index = start
      loop do
        line = lines[index]
        body = line.sub(/\A#{Regexp.escape(name)}\s*:?=\s*/, "") if index == start
        body ||= line
        joined << body.sub(/\\\s*\z/, "")
        break unless body.rstrip.end_with?("\\")

        index += 1
      end
      joined.split(/\s+/).reject { |word| word.empty? || word.start_with?("$(") }
    end

    # ---- process helpers ------------------------------------------------

    def compile(source, object, flags, cc: nil)
      cc ||= @tools[:clang]
      flags = flags + ["-DHEADLESS=0"] if source == "kernel/fs/install.c"
      FileUtils.mkdir_p(File.dirname(File.join(SRC, object)))
      run([cc, *flags, "-c", source, "-o", object])
    end

    def directory(base, relative)
      FileUtils.mkdir_p(File.join(base, relative))
    end

    def copy(relative, destination)
      FileUtils.cp(File.join(SRC, relative), destination)
    end

    def run(command)
      # clang's -fuse-ld=lld looks up ld.lld on PATH, and LLVM is commonly
      # installed outside PATH on a plain Windows host.
      env = { "PATH" => [llvm_bin, ENV["PATH"]].compact.join(File::PATH_SEPARATOR) }
      stdout, stderr, status = Open3.capture3(env, *command, chdir: SRC)
      return if status.success?

      raise BuildError,
            "command failed (#{status.exitstatus}): #{command.join(' ')}\n" \
            "#{stdout}\n#{stderr}"
    end

    def clean
      %w[build out esp].each do |dir|
        FileUtils.rm_rf(File.join(SRC, dir))
      end
      puts "native build: clean"
    end
  end
end

if __FILE__ == $PROGRAM_NAME
  exit(ZosNative.main(ARGV))
end
