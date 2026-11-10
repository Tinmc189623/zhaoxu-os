#!/usr/bin/env ruby
# Locate the host QEMU OVMF firmware (CODE + VARS) and print it as JSON.
#
#   { "Code": "...", "Vars": "...", "Source": "..." }
#
# ZHAOXU_OVMF_CODE / ZHAOXU_OVMF_VARS win over any discovery.  Older
# RAVENEL_OVMF_* variables are still honoured.

require 'json'

CODE_PATTERNS = ['edk2-x86_64-code*.fd', 'OVMF_CODE*.fd', 'OVMF.fd'].freeze
VARS_PATTERNS = ['edk2-i386-vars*.fd', 'edk2-x86_64-vars*.fd',
                 'OVMF_VARS*.fd'].freeze

def find_first(roots, patterns)
  patterns.each do |pattern|
    roots.each do |root|
      next unless File.directory?(root)

      Dir.glob(File.join(root, '**', pattern)).sort.each do |hit|
        return hit if File.file?(hit)
      end
    end
  end
  nil
end

def env_pair(names)
  names.each do |code_name, vars_name|
    code = ENV[code_name]
    vars = ENV[vars_name]
    next if code.to_s.empty? || vars.to_s.empty?
    next unless File.file?(code) && File.file?(vars)

    return [code, vars, "environment #{code_name} / #{vars_name}"]
  end
  nil
end

def qemu_share_dirs
  exe = ENV.fetch('PATH', '').split(File::PATH_SEPARATOR).filter_map do |dir|
    %w[qemu-system-x86_64 qemu-system-x86_64.exe].map { |n| File.join(dir, n) }
                                               .find { |p| File.file?(p) }
  end.first
  return [] unless exe

  [File.join(File.dirname(exe), 'share')]
end

def common_roots
  roots = []
  roots << File.join(ENV['ProgramFiles'], 'qemu') if ENV['ProgramFiles']
  if ENV['ProgramFiles(x86)']
    roots << File.join(ENV['ProgramFiles(x86)'], 'qemu')
  end
  roots << '/usr/share/OVMF' << '/usr/share/edk2' << '/usr/share/qemu'
  roots
end

found = env_pair([%w[ZHAOXU_OVMF_CODE ZHAOXU_OVMF_VARS],
                  %w[RAVENEL_OVMF_CODE RAVENEL_OVMF_VARS]])

candidates = [qemu_share_dirs, common_roots]
candidates.each do |roots|
  break if found

  code = find_first(roots, CODE_PATTERNS)
  vars = find_first(roots, VARS_PATTERNS)
  next unless code && vars

  found = [code, vars, roots.first]
end

unless found
  warn 'OVMF firmware not found (need edk2-x86_64-code.fd and edk2-i386-vars.fd).'
  warn 'Install QEMU or set ZHAOXU_OVMF_CODE / ZHAOXU_OVMF_VARS.'
  exit 1
end

puts JSON.pretty_generate('Code' => found[0], 'Vars' => found[1],
                          'Source' => found[2])
