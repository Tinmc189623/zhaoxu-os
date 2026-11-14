# frozen_string_literal: true
#
# run.rb -- cross-platform QEMU runner for Zhaoxu OS.
#
# Builds (through build.rb) and boots the ISO with the host's QEMU/OVMF.
# Same four hosts as build.rb: native Linux, macOS, Windows (MSYS2) and WSL2.
#
#   ruby tools/run.rb                       # UEFI, ISO only
#   ruby tools/run.rb --display gtk         # WSLg / desktop window
#   ruby tools/run.rb --smp 4 --mem 1024
#   ruby tools/run.rb run-bios              # any make run target
#   ruby tools/run.rb run-install           # ISO + blank target disk
#
# Anything after the options is passed to make as extra target/variables.

require "rbconfig"
require_relative "host"
require_relative "build"

module ZosRun
  SRC = ZosBuild::SRC

  RUN_TARGETS = %w[run-iso run run-bios run-install run-install-bios].freeze

  class << self
    def main(argv)
      options = parse_options(argv.dup)
      return 0 if options.nil?

      host = ZosBuild.resolve_host(options)
      ok =
        case host
        when :linux, :macos then run_native(host, options)
        when :windows then run_windows(options)
        when :wsl
          if ZosHost.native_windows?
            run_wsl(options)
          else
            run_native(:linux, options)
          end
        else raise ZosHost::Error, "unsupported host: #{host}"
        end
      ok ? 0 : 1
    rescue ZosHost::Error => error
      warn "run: #{error.message}"
      1
    end

    def parse_options(argv)
      options = {
        host: "auto", distro: nil, display: "none", machine: "q35",
        mem: 512, smp: 4, jobs: 4, headless: nil, args: [],
      }
      until argv.empty?
        argument = argv.shift
        case argument
        when "--host" then options[:host] = argv.shift
        when /\A--host=(.+)\z/ then options[:host] = Regexp.last_match(1)
        when "--distro" then options[:distro] = argv.shift
        when /\A--distro=(.+)\z/ then options[:distro] = Regexp.last_match(1)
        when "--display" then options[:display] = argv.shift
        when /\A--display=(.+)\z/ then options[:display] = Regexp.last_match(1)
        when "--machine" then options[:machine] = argv.shift
        when /\A--machine=(.+)\z/ then options[:machine] = Regexp.last_match(1)
        when "--mem" then options[:mem] = Integer(argv.shift)
        when /\A--mem=(\d+)\z/ then options[:mem] = Integer(Regexp.last_match(1))
        when "--smp" then options[:smp] = Integer(argv.shift)
        when /\A--smp=(\d+)\z/ then options[:smp] = Integer(Regexp.last_match(1))
        when "--jobs", "-j" then options[:jobs] = Integer(argv.shift)
        when /\A-j(\d+)\z/ then options[:jobs] = Integer(Regexp.last_match(1))
        when "--headless" then options[:headless] = 1
        when "--gui" then options[:headless] = 0
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
        usage: ruby tools/run.rb [options] [make target/variables...]

          --display none|gtk|sdl|curses  QEMU display backend (default none)
          --machine q35|pc               QEMU machine (default q35)
          --mem MiB                      guest RAM (default 512)
          --smp N                        guest CPUs (default 4)
          --jobs N, -jN                  build jobs
          --headless / --gui             build mode
          --host auto|native|wsl|...
          --distro NAME                  WSL distro
          --help

        targets: #{RUN_TARGETS.join(', ')}
      TEXT
    end

    def make_arguments(options)
      target = (options[:args].first if RUN_TARGETS.include?(options[:args].first)) ||
               "run-iso"
      extra = RUN_TARGETS.include?(options[:args].first) ? options[:args][1..] : options[:args]
      arguments = [target]
      arguments << "QEMU_MACHINE=#{options[:machine]}"
      arguments << "QEMU_MEM=#{options[:mem]}M"
      arguments << "QEMU_SMP=#{options[:smp]}"
      arguments << "QEMU_DISPLAY=-display #{options[:display]}"
      arguments << "HEADLESS=#{options[:headless]}" unless options[:headless].nil?
      arguments + (extra || [])
    end

    def run_native(host, options)
      env = ZosBuild.native_env(host)
      qemu = ZosHost.find_executable("qemu-system-x86_64", *tool_dirs(host))
      env["QEMU"] = qemu if qemu
      system(env, "make", *make_arguments(options), chdir: SRC) || false
    end

    def run_windows(options)
      bash = ZosHost.bash
      raise ZosHost::Error, "MSYS2/Git Bash not found" unless bash

      script = ZosBuild.windows_exports.map { |line| "export #{line}" }.join("; ")
      script += "; cd #{ZosHost.shell_quote(ZosHost.to_posix_path(SRC))}"
      script += " && make #{make_arguments(options).map { |arg| ZosHost.shell_quote(arg) }.join(' ')}"
      system(bash, "-lc", script) || false
    end

    def run_wsl(options)
      raise ZosHost::Error, "no registered WSL distro" unless ZosBuild.wsl_available?

      wsl = ZosBuild.wsl_executable
      distro = ZosBuild.wsl_distro(options[:distro])
      src = ZosBuild.decode_output(
        ZosBuild.capture(wsl, "-d", distro, "--", "wslpath", "-a", SRC.to_s)
      ).strip
      ruby = ZosBuild.decode_output(
        ZosBuild.capture(wsl, "-d", distro, "--", "wslpath", "-a",
                         RbConfig.ruby.to_s)
      ).strip
      raise ZosHost::Error, "cannot translate paths in WSL distro #{distro}" if src.empty? || ruby.empty?

      script = "cd #{ZosHost.shell_quote(src)} && " \
               "RUBY=#{ZosHost.shell_quote(ruby)} " \
               "make #{make_arguments(options).map { |arg| ZosHost.shell_quote(arg) }.join(' ')}"
      system(wsl, "-d", distro, "--", "bash", "-lc", script) || false
    end

    def tool_dirs(host)
      dirs = [File.join(SRC, "tools")]
      dirs += ZosHost.macos_path_dirs if host == :macos
      dirs
    end
  end
end

exit(ZosRun.main(ARGV))
