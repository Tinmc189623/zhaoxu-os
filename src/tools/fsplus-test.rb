#!/usr/bin/env ruby
# frozen_string_literal: true

# fsplus-test.rb -- compile and run the FS+ host format tests.

require "rbconfig"
require "tmpdir"

ROOT = File.expand_path("..", __dir__)
FSDIR = File.join(ROOT, "fs", "fsplus")
TESTDIR = File.join(ROOT, "tests", "fsplus")
SOURCES = [
  File.join(FSDIR, "fsplus_crc32c.c"),
  File.join(FSDIR, "fsplus_super.c"),
  File.join(FSDIR, "fsplus_node.c"),
  File.join(TESTDIR, "format_test.c")
].freeze

def find_compiler
  candidates = [ENV["CC"], "clang", "gcc", "cc"].compact
  candidates.find do |candidate|
    system(candidate, "--version", out: File::NULL, err: File::NULL)
  end || raise("no C compiler found; set CC")
end

def run(command)
  puts "+ #{command.join(' ')}"
  raise "command failed: #{command.join(' ')}" unless system(*command)
end

compiler = find_compiler
Dir.mktmpdir("fsplus-test-") do |tmp|
  binary = File.join(tmp, "fsplus_format_test")
  binary += ".exe" if RbConfig::CONFIG["host_os"] =~ /mswin|mingw/
  run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
       "-I", FSDIR, *SOURCES, "-o", binary])
  run([binary])
end

puts "FS+ host test: PASS"
