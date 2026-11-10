#!/usr/bin/env ruby
# Fetch the system fonts into initramfs/data/.
#
# MiSans / MiSans Global are distributed under Xiaomi's font licence, which
# does not allow redistributing the font files, so they are downloaded at
# build time and never committed.  Inter is OFL 1.1.
#
# Run via `make fonts` from src/.

require 'fileutils'
require 'open3'

DEST = File.expand_path('../initramfs/data', __dir__)
FileUtils.mkdir_p(DEST)

def fetch(url, path)
  return if File.file?(path) && File.size(path) > 1024

  puts "fetch #{File.basename(path)}"
  ok = system('curl', '-fsSL', '--retry', '3', '-m', '600', '-o', path, url)
  abort "cannot download #{url}" unless ok && File.file?(path)
end

fetch('https://cdn.jsdelivr.net/gh/whjkd2005/MiSans-Fonts@main/MiSans-Regular.ttf',
      File.join(DEST, 'MiSans-Regular.ttf'))
fetch('https://cdn.jsdelivr.net/gh/dsrkafuu/misans@main/raw/Latin/MiSansLatinVF.ttf',
      File.join(DEST, 'MiSansLatinVF.ttf'))

inter_zip = File.join(DEST, '.Inter.zip')
fetch('https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip',
      inter_zip)

inter = File.join(DEST, 'Inter-Regular.otf')
unless File.file?(inter) && File.size(inter) > 1024
  extract = system('unzip', '-p', inter_zip, 'extras/otf/Inter-Regular.otf',
                   out: inter)
  abort 'cannot extract Inter-Regular.otf' unless extract &&
                                                 File.size(inter) > 1024
end
FileUtils.rm_f(inter_zip)

puts 'fonts ready'
