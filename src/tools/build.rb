# frozen_string_literal: true
#
# build.rb -- cross-platform build entry point for Zhaoxu OS.
#
# The build itself is still the GNU Makefile in this directory; this script
# detects the host (native Linux, macOS, Windows via MSYS2, or WSL2) and runs
# the same `make` with the right tool paths and environment.  It is pure Ruby
# (rule 6) and works on all four hosts:
#
#   ruby tools/build.rb                     # build everything (make all)
#   ruby tools/build.rb iso                 # any make target / variable works
#   ruby tools/build.rb --jobs 8 run-iso
#   ruby tools/build.rb --check             # report tool status for this host
#   ruby tools/build.rb --host wsl          # Windows: force the WSL path
#   ruby tools/build.rb --host native       # Windows: force MSYS2/MinGW
#
# Linux/macOS run make directly.  Windows finds the MSYS2 (or Git Bash)
# bash.exe, exports RUBY/FASM/OVMF in POSIX form, and runs make there; when
# MSYS2 is missing and a WSL distro is registered it can forward to WSL.

require "rbconfig"
require_relative "host"

module ZosBuild
  SRC = File.expand_path("..", __dir__)

  # Logical build tools and the commands that satisfy them, per host.  The
  # first existing candidate wins.
  TOOL_CANDIDATES = {
    linux: {
      "make" => %w[make], "ruby" => %w[ruby],
      "cc" => %w[gcc cc], "cxx" => %w[g++ c++],
      "gcc-ada" => %w[gcc-14 gnatgcc x86_64-elf-gcc],
      "ld" => %w[ld.lld lld], "objcopy" => %w[objcopy llvm-objcopy],
      "clang" => %w[clang], "lld-link" => %w[lld-link],
      "fasm" => %w[tools/fasm fasm], "tar" => %w[tar],
      "mkfs.vfat" => %w[mkfs.vfat mkfs.fat], "mmd" => %w[mmd],
      "mcopy" => %w[mcopy], "mksquashfs" => %w[mksquashfs],
      "xorriso" => %w[xorriso], "qemu-system-x86_64" => %w[qemu-system-x86_64],
    },
    wsl: {},
    macos: {
      "make" => %w[make], "ruby" => %w[ruby],
      "cc" => %w[clang cc], "cxx" => %w[clang++ c++],
      "gcc-ada" => %w[x86_64-elf-gcc gnatgcc gnat],
      "ld" => %w[ld.lld lld], "objcopy" => %w[llvm-objcopy objcopy],
      "clang" => %w[clang], "lld-link" => %w[lld-link],
      "fasm" => %w[fasm tools/fasm], "tar" => %w[tar],
      "mkfs.vfat" => %w[mkfs.vfat mkfs.fat], "mmd" => %w[mmd],
      "mcopy" => %w[mcopy], "mksquashfs" => %w[mksquashfs],
      "xorriso" => %w[xorriso], "qemu-system-x86_64" => %w[qemu-system-x86_64],
    },
    windows: {
      "make" => %w[make], "ruby" => %w[ruby],
      "cc" => %w[clang cc gcc], "cxx" => %w[clang++ c++ g++],
      "gcc-ada" => %w[x86_64-elf-gcc gnatgcc],
      "ld" => %w[ld.lld lld], "objcopy" => %w[llvm-objcopy objcopy],
      "clang" => %w[clang], "lld-link" => %w[lld-link],
      "fasm" => %w[fasm.exe fasm tools/fasm.exe tools/fasm],
      "tar" => %w[tar], "mkfs.vfat" => %w[mkfs.vfat mkfs.fat],
      "mmd" => %w[mmd], "mcopy" => %w[mcopy], "mksquashfs" => %w[mksquashfs],
      "xorriso" => %w[xorriso], "qemu-system-x86_64" => %w[qemu-system-x86_64],
    },
  }.freeze

  SCOPE_GROUPS = {
    base: %w[make ruby],
    kernel: %w[cc cxx ld objcopy clang lld-link fasm tar],
    ada: %w[gcc-ada],
    iso: %w[mkfs.vfat mmd mcopy mksquashfs xorriso],
    run: %w[qemu-system-x86_64],
  }.freeze

  INSTALL_HINTS = {
    linux: {
      "make" => "apt install make", "ruby" => "apt install ruby",
      "cc" => "apt install gcc", "cxx" => "apt install g++",
      "gcc-ada" => "apt install gcc-14 gnat-14", "ld" => "apt install lld",
      "objcopy" => "apt install binutils", "clang" => "apt install clang",
      "lld-link" => "apt install lld", "fasm" => "repository ships tools/fasm",
      "tar" => "apt install tar", "mkfs.vfat" => "apt install dosfstools",
      "mmd" => "apt install mtools", "mcopy" => "apt install mtools",
      "mksquashfs" => "apt install squashfs-tools",
      "xorriso" => "apt install xorriso",
      "qemu-system-x86_64" => "apt install qemu-system-x86",
    },
    macos: {
      "make" => "xcode-select --install", "ruby" => "brew install ruby",
      "cc" => "xcode-select --install", "cxx" => "xcode-select --install",
      "gcc-ada" => "AdaCore GNAT, then ADA=...", "ld" => "brew install lld",
      "objcopy" => "brew install llvm", "clang" => "brew install llvm",
      "lld-link" => "brew install lld", "fasm" => "FASM=... (macOS build)",
      "tar" => "system tar", "mkfs.vfat" => "brew install dosfstools",
      "mmd" => "brew install mtools", "mcopy" => "brew install mtools",
      "mksquashfs" => "brew install squashfs", "xorriso" => "brew install xorriso",
      "qemu-system-x86_64" => "brew install qemu",
    },
    windows: {
      "make" => "pacman -S make (MSYS2)", "ruby" => "winget install RubyInstallerTeam.Ruby.3.3",
      "cc" => "pacman -S mingw-w64-x86_64-clang", "cxx" => "pacman -S mingw-w64-x86_64-clang",
      "gcc-ada" => "x86_64-elf GNAT, then ADA=...", "ld" => "pacman -S mingw-w64-x86_64-lld",
      "objcopy" => "pacman -S mingw-w64-x86_64-llvm", "clang" => "pacman -S mingw-w64-x86_64-clang",
      "lld-link" => "pacman -S mingw-w64-x86_64-lld", "fasm" => "winget install fasm.fasm",
      "tar" => "pacman -S tar", "mkfs.vfat" => "pacman -S mingw-w64-x86_64-dosfstools",
      "mmd" => "pacman -S mingw-w64-x86_64-mtools", "mcopy" => "pacman -S mingw-w64-x86_64-mtools",
      "mksquashfs" => "pacman -S squashfs-tools", "xorriso" => "pacman -S xorriso",
      "qemu-system-x86_64" => "pacman -S mingw-w64-x86_64-qemu",
    },
  }.freeze

  class << self
    def main(argv)
      options = parse_options(argv.dup)
      return 0 if options.nil?

      host = options[:check] ? check_host(options) : resolve_host(options)
      return check(host, options) if options[:check]

      run_build(host, options)
    rescue ZosHost::Error => error
      warn "build: #{error.message}"
      1
    end

    # --check must work even when the host cannot build yet, so it does not
    # require a usable shell/toolchain the way resolve_host does.
    def check_host(options)
      detected = ZosHost.detect
      requested = options[:host].to_s
      case requested
      when "auto", "" then detected
      when "native" then detected == :windows ? :windows : detected
      when "wsl" then :wsl
      when "linux", "macos", "windows" then requested.to_sym
      else raise ZosHost::Error, "unknown --host #{requested}"
      end
    end

    def parse_options(argv)
      options = { jobs: nil, headless: nil, host: "auto", distro: nil,
                  check: nil, args: [] }
      until argv.empty?
        argument = argv.shift
        case argument
        when "--jobs", "-j"
          options[:jobs] = Integer(argv.shift)
        when /\A-j(\d+)\z/
          options[:jobs] = Integer(Regexp.last_match(1))
        when "--headless" then options[:headless] = 1
        when "--gui" then options[:headless] = 0
        when "--host" then options[:host] = argv.shift
        when /\A--host=(.+)\z/ then options[:host] = Regexp.last_match(1)
        when "--distro" then options[:distro] = argv.shift
        when /\A--distro=(.+)\z/ then options[:distro] = Regexp.last_match(1)
        when "--check" then options[:check] = :build
        when /\A--check=(.+)\z/ then options[:check] = Regexp.last_match(1).to_sym
        when "--help", "-h"
          puts help_text
          return nil
        else
          options[:args] = [argument] + argv
          break
        end
      end
      options
    end

    def help_text
      <<~TEXT
        usage: ruby tools/build.rb [options] [make target/variables...]

          --jobs N, -jN      parallel make jobs
          --headless         build without GOP graphics (HEADLESS=1)
          --gui              build with GOP graphics (HEADLESS=0)
          --host auto|native|wsl|linux|macos|windows
          --distro NAME      WSL distro (default: WSL_DISTRO_NAME or Ubuntu-26.04)
          --check[=scope]    report tool status; scope: base|kernel|ada|iso|run|all
          --help

        examples:
          ruby tools/build.rb
          ruby tools/build.rb iso
          ruby tools/build.rb --host wsl run-iso
      TEXT
    end

    def resolve_host(options)
      detected = ZosHost.detect
      requested = options[:host].to_s
      host =
        case requested
        when "auto", ""
          if detected == :windows
            if native_windows_usable? then :windows
            elsif wsl_available? then :wsl
            end
          else
            detected
          end
        when "native"
          if detected == :windows
            native_windows_usable? ? :windows : raise(ZosHost::Error,
                                                      "MSYS2/Git Bash with make not found (run --check)")
          else
            detected
          end
        when "wsl"
          if detected == :wsl
            :wsl
          elsif detected == :windows
            :wsl
          else
            raise ZosHost::Error, "WSL is only available from Windows"
          end
        when "linux", "macos", "windows"
          requested.to_sym
        else
          raise ZosHost::Error, "unknown --host #{requested}"
        end
      raise ZosHost::Error, "no usable build host; run --check" if host.nil?

      host
    end

    def native_windows_usable?
      bash = ZosHost.bash
      return false unless bash

      !ZosHost.find_executable("make", *windows_tool_dirs(bash)).nil?
    end

    def wsl_available?
      return false unless ZosHost.native_windows?

      !wsl_distros.empty?
    end

    # Registered WSL distros.  wsl.exe writes UTF-16LE when WSL is missing,
    # so force a sane decode before splitting lines.
    def wsl_distros
      output = decode_output(capture(wsl_executable, "-l", "-q"))
      output.split(/\r?\n/).map(&:strip).reject(&:empty?)
    rescue StandardError
      []
    end

    def decode_output(bytes)
      return "" if bytes.nil? || bytes.empty?

      if bytes.byteslice(0, 2) == "\xFF\xFE".b || bytes.include?("\x00".b)
        bytes.force_encoding(Encoding::UTF_16LE)
             .encode(Encoding::UTF_8, invalid: :replace, undef: :replace)
      else
        bytes.dup.force_encoding(Encoding::UTF_8).scrub
      end
    end

    def wsl_distro(distro = nil)
      distro || ENV["WSL_DISTRO_NAME"] || wsl_distros.first || "Ubuntu-26.04"
    end

    def windows_tool_dirs(bash = ZosHost.bash)
      root = ZosHost.msys2_root
      dirs = []
      if root
        dirs << File.join(root.tr("\\", "/"), "mingw64", "bin")
        dirs << File.join(root.tr("\\", "/"), "usr", "bin")
      elsif bash
        # Git for Windows keeps its POSIX tools under <root>/usr/bin.
        git_root = File.expand_path(File.join(File.dirname(bash), ".."))
        dirs << File.join(git_root.tr("\\", "/"), "usr", "bin")
        dirs << File.dirname(bash)
      end
      dirs << File.join(SRC, "tools")
      dirs
    end

    def make_arguments(options)
      arguments = []
      arguments << "-j#{options[:jobs]}" if options[:jobs]
      arguments << "HEADLESS=#{options[:headless]}" unless options[:headless].nil?
      arguments + options[:args]
    end

    def run_build(host, options)
      arguments = make_arguments(options)
      ok =
        case host
        when :linux, :macos then run_native(host, arguments)
        when :windows then run_windows(arguments)
        when :wsl
          ZosHost.native_windows? ? run_wsl(arguments, options[:distro]) :
                                    run_native(:linux, arguments)
        else raise ZosHost::Error, "unsupported host: #{host}"
        end
      ok ? 0 : 1
    end

    def run_native(host, arguments)
      env = native_env(host)
      system(env, "make", *arguments, chdir: SRC) || false
    end

    # Image/QEMU tools are looked up by name; pass the resolved paths to make
    # so MSYS2/Homebrew prefixes that are not on PATH still work.
    def tool_env(host)
      logical = host == :wsl ? :linux : host
      candidates = TOOL_CANDIDATES.fetch(logical)
      dirs = host == :macos ? ZosHost.macos_path_dirs : windows_tool_dirs
      variables = {
        "mkfs.vfat" => "MKFS_VFAT", "mmd" => "MMD", "mcopy" => "MCOPY",
        "mksquashfs" => "MKSQUASHFS", "xorriso" => "XORRISO",
        "qemu-system-x86_64" => "QEMU",
      }
      variables.each_with_object({}) do |(tool, variable), env|
        path = candidates.fetch(tool, []).map do |name|
          ZosHost.find_executable(name, *dirs)
        end.compact.first
        env[variable] = path if path
      end
    end

    def native_env(host)
      env = { "RUBY" => RbConfig.ruby }
      if host == :linux
        fasm = File.join(SRC, "tools", "fasm")
        env["FASM"] = fasm if File.file?(fasm)
      elsif host == :macos
        fasm = ZosHost.find_executable("fasm", *ZosHost.macos_path_dirs)
        env["FASM"] = fasm if fasm
      end
      ovmf = ZosHost.ovmf(host: host)
      env["OVMF_CODE"], env["OVMF_VARS"] = ovmf if ovmf
      env.merge!(tool_env(host))
      if host == :macos
        dirs = ZosHost.macos_path_dirs
        env["PATH"] = (dirs + [ENV["PATH"]]).compact.join(File::PATH_SEPARATOR)
        env["CC"] = ZosHost.find_executable("clang", *dirs) || "clang"
        env["CXX"] = ZosHost.find_executable("clang++", *dirs) || "clang++"
        env["LD"] = ZosHost.find_executable("ld.lld", *dirs) || "ld.lld"
        env["OBJCOPY"] = ZosHost.find_executable("llvm-objcopy", *dirs) || "llvm-objcopy"
        env["BOOT_CC"] = env["CC"]
        env["BOOT_LD"] = ZosHost.find_executable("lld-link", *dirs) || "lld-link"
      end
      env
    end

    # Environment exports (already `export`-ready) for a POSIX shell on
    # native Windows.  MSYS2's login profile supplies PATH; only the paths
    # make needs are exported.
    def windows_exports
      exports = ["RUBY=#{ZosHost.shell_quote(ZosHost.to_posix_path(RbConfig.ruby))}"]
      fasm = ZosHost.find_executable("fasm") ||
             (File.file?(File.join(SRC, "tools", "fasm.exe")) ?
              File.join(SRC, "tools", "fasm.exe") : nil)
      if fasm
        exports << "FASM=#{ZosHost.shell_quote(ZosHost.to_posix_path(fasm))}"
      end
      ovmf = ZosHost.ovmf(host: :windows)
      if ovmf
        exports << "OVMF_CODE=#{ZosHost.shell_quote(ZosHost.to_posix_path(ovmf[0]))}"
        exports << "OVMF_VARS=#{ZosHost.shell_quote(ZosHost.to_posix_path(ovmf[1]))}"
      end
      exports << "CC=clang" << "CXX=clang++" << "LD=ld.lld" <<
                   "OBJCOPY=llvm-objcopy" << "BOOT_CC=clang" << "BOOT_LD=lld-link"
      exports
    end

    def run_windows(arguments)
      bash = ZosHost.bash
      raise ZosHost::Error, "MSYS2/Git Bash not found" unless bash

      script = windows_exports.map { |line| "export #{line}" }.join("; ")
      script += "; cd #{ZosHost.shell_quote(ZosHost.to_posix_path(SRC))}"
      script += " && make #{arguments.map { |arg| ZosHost.shell_quote(arg) }.join(' ')}"
      system(bash, "-lc", script) || false
    end

    def run_wsl(arguments, distro)
      wsl = wsl_executable
      distro = wsl_distro(distro)
      src = capture(wsl, "-d", distro, "--", "wslpath", "-a", SRC.to_s).to_s.strip
      ruby = capture(wsl, "-d", distro, "--", "wslpath", "-a",
                     RbConfig.ruby.to_s).to_s.strip
      raise ZosHost::Error, "cannot translate paths in WSL distro #{distro}" if src.empty? || ruby.empty?

      script = "cd #{ZosHost.shell_quote(src)} && " \
               "RUBY=#{ZosHost.shell_quote(ruby)} " \
               "make #{arguments.map { |arg| ZosHost.shell_quote(arg) }.join(' ')}"
      system(wsl, "-d", distro, "--", "bash", "-lc", script) || false
    end

    def wsl_executable
      ZosHost.find_executable("wsl", "C:/Windows/System32") || "wsl.exe"
    end

    def capture(*command)
      IO.popen(command, err: %i[child out], &:read)
    rescue SystemCallError
      ""
    end

    # ---- tool check ---------------------------------------------------

    def check(host, options)
      scope = options[:check] || :build
      lookup = probe_tools(host, options[:distro])
      # Ruby is the interpreter already running this script; it need not be
      # on the shell PATH for the build (we pass the absolute path to make).
      lookup["ruby"] ||= RbConfig.ruby if %i[windows wsl].include?(host)
      # The repository ships a Linux/ELF fasm; report it on POSIX hosts even
      # when it is not on PATH (the Makefile defaults to tools/fasm there).
      if %i[linux macos wsl].include?(host)
        shipped_fasm = File.join(SRC, "tools", "fasm")
        lookup["tools/fasm"] ||= shipped_fasm if File.file?(shipped_fasm)
      end
      candidates = TOOL_CANDIDATES.fetch(host == :wsl ? :linux : host) do
        raise ZosHost::Error, "tool check is not supported on #{host}"
      end
      resolved = candidates.transform_values do |names|
        names.map { |name| lookup[name] }.compact.first
      end
      print_report(host, resolved, scope)
    end

    def probe_tools(host, distro)
      case host
      when :windows then probe_in_bash(ZosHost.bash, windows_exports)
      when :wsl
        ZosHost.native_windows? ? probe_in_wsl(distro) : probe_native(:linux)
      else probe_native(host)
      end
    end

    def probe_native(host)
      dirs = windows_tool_dirs
      dirs = ZosHost.macos_path_dirs + dirs if host == :macos
      candidates = TOOL_CANDIDATES.fetch(host)
      candidates.values.flatten.uniq.each_with_object({}) do |name, lookup|
        lookup[name] =
          if name.include?("/")
            path = File.join(SRC, name)
            File.file?(path) ? path : nil
          else
            ZosHost.find_executable(name, *dirs)
          end
      end
    end

    def probe_in_bash(bash, exports)
      return {} unless bash

      names = TOOL_CANDIDATES.fetch(:windows).values.flatten.uniq
      script = exports.map { |line| "export #{line}" }.join("; ")
      script += "; " + names.map do |name|
        "printf '%s\\t%s\\n' #{ZosHost.shell_quote(name)} \"$(command -v #{name} 2>/dev/null)\""
      end.join("; ")
      parse_probe_output(capture(bash, "-lc", script))
    end

    def probe_in_wsl(distro)
      return {} unless ZosHost.native_windows? && wsl_available?

      distro = wsl_distro(distro)
      names = TOOL_CANDIDATES.fetch(:linux).values.flatten.uniq
      script = names.map do |name|
        "printf '%s\\t%s\\n' #{ZosHost.shell_quote(name)} \"$(command -v #{name} 2>/dev/null)\""
      end.join("; ")
      parse_probe_output(decode_output(capture(wsl_executable, "-d", distro, "--",
                                               "bash", "-lc", script)))
    end

    def parse_probe_output(output)
      lookup = {}
      output.to_s.each_line do |line|
        name, path = line.strip.split("\t", 2)
        lookup[name] = path if name && path && !path.empty?
      end
      lookup
    end

    def print_report(host, resolved, scope)
      groups = SCOPE_GROUPS
      scopes = case scope
               when :all then %i[base kernel ada iso run]
               when :build then %i[base kernel]
               when :iso then %i[base kernel ada iso]
               else [scope]
               end
      hints = INSTALL_HINTS[host] || {}
      puts "host: #{host}"
      puts "src:  #{SRC}"
      if host == :windows
        bash = ZosHost.bash
        puts "shell: #{bash || 'MSYS2/Git Bash not found (native build unavailable)'}"
      elsif host == :wsl
        if ZosHost.native_windows?
          puts "wsl:   #{wsl_available? ? 'available' : 'not found'}"
        else
          puts "wsl:   inside WSL (#{ENV['WSL_DISTRO_NAME'] || 'unknown distro'})"
        end
      end
      failed = false
      scopes.each do |group|
        puts "#{group}:"
        (groups[group] || []).each do |name|
          path = resolved[name]
          if path
            puts format("  %-22s %s", name, path)
          else
            failed = true
            hint = hints[name]
            puts format("  %-22s MISSING%s", name, hint ? "  -- #{hint}" : "")
          end
        end
      end
      ovmf = ZosHost.ovmf(host: host)
      puts "firmware: #{ovmf ? ovmf.join(' + ') : 'OVMF not found (set OVMF_CODE/OVMF_VARS)'}"
      failed ? 1 : 0
    end
  end
end

exit(ZosBuild.main(ARGV)) if __FILE__ == $PROGRAM_NAME
