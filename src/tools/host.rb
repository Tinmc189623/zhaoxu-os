# frozen_string_literal: true
#
# host.rb -- detect the host platform and resolve the tools the Zhaoxu OS
# build needs.  Pure Ruby so it runs on native Linux, macOS and Windows
# without a shell; only ZosHost.bash / ZosHost.wsl need external programs.
#
# Supported hosts (rule: the build must work on all four):
#   :linux    -- native Linux
#   :wsl      -- Linux inside WSL2 (reported separately so the caller can
#                translate Windows paths and forward RUBY)
#   :macos    -- native macOS (Homebrew LLVM + QEMU + xorriso/mtools)
#   :windows  -- native Windows through an MSYS2/MinGW bash environment
#
# The module never mutates the caller's environment.  build.rb / run.rb take
# the returned plan and build the environment themselves.

module ZosHost
  class Error < StandardError; end

  HOSTS = %i[linux wsl macos windows].freeze
  WINDOWS_RUBY = /(mswin|mingw|cygwin)/

  # MSYS2 and Git for Windows both put their POSIX sh here.
  BASH_CANDIDATES = [
    "C:/msys64/usr/bin/bash.exe",
    "C:/msys2/usr/bin/bash.exe",
    "C:/Program Files/Git/bin/bash.exe",
    "C:/Program Files (x86)/Git/bin/bash.exe",
  ].freeze

  # edk2/OVMF firmware names, in the order that QEMU packages ship them.
  OVMF_CODE_NAMES = %w[
    OVMF_CODE_4M.fd OVMF_CODE.fd edk2-x86_64-code.fd
  ].freeze
  OVMF_VARS_NAMES = %w[
    OVMF_VARS_4M.fd OVMF_VARS.fd edk2-i386-vars.fd
  ].freeze

  class << self
    # Detect the host from the Ruby platform plus the environment.  The
    # proc_version: keyword exists so tests can pin the WSL check.
    def detect(env: ENV, platform: RUBY_PLATFORM, proc_version: nil)
      return :windows if platform =~ WINDOWS_RUBY
      return :macos if platform =~ /darwin/

      raise Error, "unsupported Ruby platform: #{platform}" unless platform =~ /linux/

      version = proc_version
      version ||= File.readable?("/proc/version") ? File.read("/proc/version") : ""
      version = version.to_s
      if env["WSL_DISTRO_NAME"] || env["WSL_INTEROP"] ||
         version.match?(/microsoft/i)
        :wsl
      else
        :linux
      end
    end

    def windows_ruby?(platform = RUBY_PLATFORM)
      platform =~ WINDOWS_RUBY
    end

    # True when the Ruby process itself runs on Windows (so native Windows
    # is reachable); false on Linux/macOS/WSL.
    def native_windows?(env: ENV, platform: RUBY_PLATFORM)
      windows_ruby?(platform)
    end

    # The bash.exe used to run POSIX recipes on native Windows.  MSYS2 is
    # preferred over Git Bash because only MSYS2 carries the toolchain.
    def bash(env: ENV)
      extra = [env["ZOS_BASH"]].compact
      found = find_executable("bash", *extra)
      return found if found

      BASH_CANDIDATES.find { |path| File.file?(path) }
    end

    def msys2_root(env: ENV)
      candidates = [env["MSYS2_ROOT"], "C:/msys64"].compact
      candidates.find do |root|
        File.file?(File.join(root.tr("\\", "/"), "usr", "bin", "bash.exe"))
      end
    end

    # Convert C:\a\b to /c/a/b for MSYS2/Git-Bash and WSL.  The function is
    # a no-op for POSIX paths so callers can use it unconditionally.
    def to_posix_path(path)
      expanded = File.expand_path(path.to_s).tr("\\", "/")
      match = expanded.match(/\A([A-Za-z]):\/(.*)\z/)
      match ? "/#{match[1].downcase}/#{match[2]}" : expanded
    end

    # Search PATH (and optional extra directories) for a command.  Windows
    # candidates also try PATHEXT so ruby.exe is found for "ruby".
    def find_executable(name, *extra_dirs, env: ENV)
      return File.expand_path(name) if name.include?("/") || name.include?("\\")

      extensions = [""]
      if windows_ruby?
        extensions |= (env["PATHEXT"] || ".EXE;.CMD;.BAT;.COM").split(";")
                       .map { |ext| ext.downcase }
        extensions |= [".exe", ".cmd", ".bat"]
        extensions.uniq!
      end

      directories = extra_dirs.compact + env["PATH"].to_s.split(File::PATH_SEPARATOR)
      directories.each do |directory|
        next if directory.nil? || directory.empty?

        extensions.each do |extension|
          candidate = File.join(directory, name + extension)
          return File.expand_path(candidate) if File.file?(candidate)
        end
      end
      nil
    end

    # Directories a native-Windows build must prepend to PATH before make:
    # the MSYS2 POSIX tools, the MinGW toolchain and the user tool dirs.
    def windows_path_dirs(env: ENV)
      root = msys2_root(env: env)
      dirs = []
      dirs << File.join(root, "usr", "bin") if root
      dirs << File.join(root, "mingw64", "bin") if root
      dirs << env["ZOS_TOOL_DIR"] if env["ZOS_TOOL_DIR"]
      dirs
    end

    # Brew keg-only LLVM/lld are not on PATH; add them so clang, ld.lld and
    # llvm-objcopy resolve on a stock macOS install.
    def macos_path_dirs(env: ENV)
      prefixes = [env["HOMEBREW_PREFIX"], "/opt/homebrew", "/usr/local"].compact
      dirs = []
      prefixes.each do |prefix|
        dirs << File.join(prefix, "opt", "llvm", "bin")
        dirs << File.join(prefix, "opt", "lld", "bin")
        dirs << File.join(prefix, "bin")
      end
      dirs.select { |dir| File.directory?(dir) }
    end

    # Candidate OVMF code/vars pairs for the host.  Returns the first pair
    # where both files exist, or nil.
    def ovmf(host: detect, env: ENV)
      roots = ovmf_roots(host, env)
      code = first_file(roots, OVMF_CODE_NAMES)
      vars = first_file(roots, OVMF_VARS_NAMES)
      code && vars ? [code, vars] : nil
    end

    def ovmf_roots(host, env)
      case host
      when :macos
        prefixes = [env["HOMEBREW_PREFIX"], "/opt/homebrew", "/usr/local"].compact
        dirs = prefixes.flat_map { |prefix| [File.join(prefix, "share", "qemu")] }
        dirs + %w[/usr/share/OVMF /usr/share/edk2/ovmf]
      when :windows
        root = msys2_root(env: env)
        dirs = []
        if root
          dirs << File.join(root.tr("\\", "/"), "mingw64", "share", "qemu")
          dirs << File.join(root.tr("\\", "/"), "mingw64", "share", "edk2-ovmf", "x64")
          dirs << File.join(root.tr("\\", "/"), "mingw64", "share", "edk2-ovmf")
        end
        dirs + %w[C:/Program\ Files/qemu]
      else
        %w[/usr/share/OVMF /usr/share/edk2/ovmf /usr/share/qemu]
      end
    end

    def first_file(directories, names)
      directories.each do |directory|
        names.each do |name|
          path = File.join(directory, name)
          return path if File.file?(path)
        end
      end
      nil
    end

    # Serialise a command for `bash -lc`.  Only single quotes need escaping
    # inside single quotes.
    def shell_quote(value)
      "'#{value.to_s.gsub("'", %q('"'"'))}'"
    end

    # Build a `VAR=value` prefix for the make command line.
    def make_assignment(name, value)
      return nil if value.nil? || value.to_s.empty?

      "#{name}=#{shell_quote(value)}"
    end
  end
end
