# Zhaoxu OS 构建入口（PowerShell -> Ruby -> Make）
#
# 实际构建由 tools/build.rb 完成；本脚本只负责在 Windows 上找到 Ruby 并
# 转发参数。没有 Ruby 时退回旧的 WSL2 转发路径，保证已有环境不中断。
[CmdletBinding()]
param(
    [switch]$Headless,
    [switch]$Check,
    [ValidateSet('auto', 'native', 'wsl')]
    [string]$HostMode = 'auto',
    [string]$Distro = '',
    [int]$Jobs = 4,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$MakeArgs = @()
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

function Find-Ruby {
    $command = Get-Command ruby -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in (Get-ChildItem 'C:\Ruby*\bin\ruby.exe' -ErrorAction SilentlyContinue)) {
        return $candidate.FullName
    }
    return $null
}

$ruby = Find-Ruby
if ($ruby) {
    $arguments = @()
    if ($Headless) { $arguments += '--headless' }
    if ($Check) { $arguments += '--check' }
    if ($HostMode -ne 'auto') { $arguments += '--host'; $arguments += $HostMode }
    if ($Distro) { $arguments += '--distro'; $arguments += $Distro }
    if ($Jobs -gt 0) { $arguments += '--jobs'; $arguments += "$Jobs" }
    $arguments += $MakeArgs
    & $ruby (Join-Path $PSScriptRoot 'tools\build.rb') @arguments
    exit $LASTEXITCODE
}

# ---- Ruby 不可用：退回 WSL2（旧行为）--------------------------------
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    [Console]::Error.WriteLine('未找到 Ruby，也未找到 wsl.exe。')
    [Console]::Error.WriteLine('原生 Windows 构建需要 Ruby + MSYS2（winget install RubyInstallerTeam.Ruby.3.3）。')
    exit 1
}

$distroName = if ($Distro) { $Distro } else { 'Ubuntu-26.04' }
$linuxPath = (& wsl.exe -d $distroName -- wslpath -a "$PSScriptRoot").Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($linuxPath)) {
    [Console]::Error.WriteLine("无法在 WSL 发行版 '$distroName' 中解析仓库路径。")
    [Console]::Error.WriteLine('可用发行版：wsl.exe -l -q；可用 -Distro 指定。')
    exit 1
}

$flag = if ($Headless) { 1 } else { 0 }
$targets = if ($MakeArgs.Count -gt 0) { $MakeArgs -join ' ' } else { '' }
$command = "cd '$linuxPath' && make -j$Jobs HEADLESS=$flag $targets"
Write-Host "WSL 发行版：$distroName"
Write-Host "构建目录：$linuxPath"
& wsl.exe -d $distroName -- bash -lc $command
exit $LASTEXITCODE
