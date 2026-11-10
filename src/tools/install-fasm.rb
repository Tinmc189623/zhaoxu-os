#!/usr/bin/env ruby
# Fetch the Ubuntu fasm package into this directory without root.

require 'fileutils'

here = File.expand_path(__dir__)
Dir.chdir(here)

abort 'apt-get download fasm failed' unless system('apt-get', 'download', 'fasm',
                                                    out: File::NULL,
                                                    err: File::NULL)

deb = Dir['fasm_*.deb'].max
abort 'no fasm_*.deb was downloaded' unless deb

abort 'dpkg-deb extraction failed' unless system('dpkg-deb', '-x', deb, 'root')
FileUtils.chmod(0o755, 'root/usr/bin/fasm')
FileUtils.cp('root/usr/bin/fasm', 'fasm')
FileUtils.rm_rf(['root', deb])

puts `./fasm`.lines.first(2)
