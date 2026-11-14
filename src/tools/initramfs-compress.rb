#!/usr/bin/env ruby
# Compress the ustar initramfs embedded in the kernel.
#
# The BIOS stage 2 can only stage 448 KiB below the EBDA, and the live
# initramfs is the largest file-backed part of kernel.elf.  Wrap the zlib
# stream in a tiny header carrying the uncompressed size so the kernel can
# allocate exactly the right amount and inflate it before mounting rootfs.
require "zlib"

source, output = ARGV
abort "usage: initramfs-compress.rb input.tar output.z" unless
  source && output

data = File.binread(source)
compressed = Zlib::Deflate.deflate(data, Zlib::BEST_COMPRESSION)
File.binwrite(output, "ZIR1" + [data.bytesize].pack("V") + compressed)
puts format("initramfs: %d -> %d bytes (zlib)", data.bytesize,
            compressed.bytesize)
