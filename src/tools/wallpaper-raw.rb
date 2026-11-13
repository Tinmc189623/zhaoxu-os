#!/usr/bin/env ruby
# Convert the default wallpaper into a raw RGB888 frame for ADE.
# Usage: ruby wallpaper-raw.rb <source-image> <output.rgb> [width height]
#
# The runtime has no image decoder; the build emits a fixed-size raw frame that
# ADE scales with integer nearest-neighbour sampling.
#
# make invokes this script through the Windows Ruby interop, so when ffmpeg is
# not on the host PATH it is executed inside the configured WSL distro.

require "fileutils"
require "open3"

abort "usage: ruby wallpaper-raw.rb source.jpg out.rgb [width height]" unless
  (2..4).include?(ARGV.length)
source, output = ARGV[0], ARGV[1]
width = (ARGV[2] || 1920).to_i
height = (ARGV[3] || 1080).to_i
abort "missing #{source}" unless File.file?(source)
abort "width/height must be positive" unless width > 0 && height > 0

DISTRO = ENV.fetch("WSL_DISTRO", "Ubuntu-26.04")
WINDOWS = RUBY_PLATFORM =~ /mswin|mingw|cygwin/

def program?(name)
  system("#{name} -version >NUL 2>NUL") if WINDOWS
  system("#{name} -version >/dev/null 2>&1") unless WINDOWS
end

def windows_to_wsl(path)
  absolute = File.expand_path(path).tr("\\", "/")
  converted, status = Open3.capture2("wsl.exe", "-d", DISTRO, "--", "wslpath", "-a", absolute)
  abort "wslpath failed for #{path}" unless status.success?
  converted.strip
end

FileUtils.mkdir_p(File.dirname(output))
expected = width * height * 3

if WINDOWS && !program?("ffmpeg")
  source_wsl = windows_to_wsl(source)
  output_wsl = windows_to_wsl(output)
  command = ["wsl.exe", "-d", DISTRO, "--", "ffmpeg", "-y", "-loglevel", "error",
             "-i", source_wsl, "-vf",
             "scale=#{width}:#{height}:flags=bilinear",
             "-pix_fmt", "rgb24", "-f", "rawvideo", output_wsl]
  success = system(*command)
else
  command = ["ffmpeg", "-y", "-loglevel", "error", "-i", source,
             "-vf", "scale=#{width}:#{height}:flags=bilinear",
             "-pix_fmt", "rgb24", "-f", "rawvideo", output]
  success = system(*command)
end
abort "ffmpeg failed for #{source}" unless success

bytes = File.size(output)
abort "wallpaper size #{bytes} != #{expected}" unless bytes == expected
puts "wallpaper: #{width}x#{height} rgb24 (#{bytes} bytes) -> #{output}"
