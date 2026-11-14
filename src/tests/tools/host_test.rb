# frozen_string_literal: true
#
# host_test.rb -- unit tests for tools/host.rb (the cross-platform build
# host detector).  Run with: ruby tests/tools/host_test.rb
#
# No compiler or QEMU needed: the tests pin RUBY_PLATFORM / ENV instead of
# relying on the machine that runs them.

require "tmpdir"
require_relative "../../tools/host"

$failures = 0

def check(name)
  yield
  puts "ok   #{name}"
rescue StandardError => error
  $failures += 1
  puts "FAIL #{name}: #{error.message}"
end

def assert(condition, message = "assertion failed")
  raise message unless condition
end

check("detect native Linux") do
  assert(ZosHost.detect(platform: "x86_64-linux", env: {},
                        proc_version: "Linux version 6.8.0") == :linux)
end

check("detect WSL by environment") do
  assert(ZosHost.detect(platform: "x86_64-linux",
                        env: { "WSL_DISTRO_NAME" => "Ubuntu-26.04" },
                        proc_version: "Linux version 5.15") == :wsl)
end

check("detect WSL by /proc/version") do
  assert(ZosHost.detect(platform: "x86_64-linux", env: {},
                        proc_version: "Linux 5.15.90.1-microsoft-standard-WSL2") == :wsl)
end

check("detect macOS") do
  assert(ZosHost.detect(platform: "arm64-darwin23", env: {}) == :macos)
end

check("detect native Windows Ruby") do
  assert(ZosHost.detect(platform: "x64-mingw-ucrt", env: {}) == :windows)
  assert(ZosHost.detect(platform: "i386-mingw32", env: {}) == :windows)
end

check("reject unsupported platform") do
  begin
    ZosHost.detect(platform: "wasm32-unknown-emscripten", env: {})
    raise "expected ZosHost::Error"
  rescue ZosHost::Error
    nil
  end
end

check("windows_ruby?") do
  assert(ZosHost.windows_ruby?("x64-mingw-ucrt"))
  assert(!ZosHost.windows_ruby?("x86_64-linux"))
  assert(!ZosHost.windows_ruby?("arm64-darwin23"))
end

check("to_posix_path converts drive letters") do
  assert(ZosHost.to_posix_path("C:\\Users\\dev\\src").end_with?("/c/Users/dev/src"),
         ZosHost.to_posix_path("C:\\Users\\dev\\src"))
  assert(ZosHost.to_posix_path("C:/Ruby33-x64/bin/ruby.exe") == "/c/Ruby33-x64/bin/ruby.exe")
end

check("shell_quote escapes single quotes") do
  assert(ZosHost.shell_quote("a'b") == %q('a'"'"'b'))
  assert(ZosHost.shell_quote("plain") == "'plain'")
  assert(ZosHost.shell_quote("") == "''")
end

check("find_executable searches extra directories") do
  Dir.mktmpdir do |directory|
    name = ZosHost.windows_ruby? ? "zhaoxu-make-test.exe" : "zhaoxu-make-test"
    File.write(File.join(directory, name), "")
    found = ZosHost.find_executable("zhaoxu-make-test", directory,
                                    env: { "PATH" => "" })
    assert(found, "not found in #{directory}")
    assert(File.expand_path(found) == File.expand_path(File.join(directory, name)))
  end
end

check("find_executable returns nil for a missing tool") do
  assert(ZosHost.find_executable("zhaoxu-definitely-missing", env: { "PATH" => "" }).nil?)
end

check("ovmf finds a code/vars pair") do
  Dir.mktmpdir do |directory|
    File.write(File.join(directory, "OVMF_CODE.fd"), "")
    File.write(File.join(directory, "OVMF_VARS.fd"), "")
    code = ZosHost.first_file([directory], ZosHost::OVMF_CODE_NAMES)
    vars = ZosHost.first_file([directory], ZosHost::OVMF_VARS_NAMES)
    assert(code && vars, "OVMF pair not found")
    assert(File.basename(code) == "OVMF_CODE.fd")
    assert(File.basename(vars) == "OVMF_VARS.fd")
  end
end

check("make_assignment skips empty values") do
  assert(ZosHost.make_assignment("CC", "clang") == "CC='clang'")
  assert(ZosHost.make_assignment("CC", nil).nil?)
  assert(ZosHost.make_assignment("CC", "").nil?)
end

puts $failures.zero? ? "PASS" : "#{$failures} failure(s)"
exit($failures.zero? ? 0 : 1)
