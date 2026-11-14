# Zhaoxu OS 运行入口（PowerShell -> Ruby -> Make + QEMU）
#
# 实际启动由 tools/run.rb 完成（原生 Windows / WSL / Linux / macOS 统一
# 处理 QEMU 与 OVMF 路径）。没有 Ruby 时退回旧的 WSL2 转发路径。
[CmdletBinding()]
param(
    [int]$MemMB = 512,
    [int]$Smp = 4,
    [string]$Machine = 'q35',
    [ValidateSet('none', 'gtk', 'sdl', 'curses')]
    [string]$Display = 'none',
    [ValidateSet('auto', 'native', 'wsl')]
    [string]$HostMode = 'auto',
    [string]$Distro = '',
    [int]$Jobs = 4,
    [switch]$DryRun,
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
    $arguments = @(
        '--display', $Display,
        '--machine', $Machine,
        '--mem', "$MemMB",
        '--smp', "$Smp",
        '--jobs', "$Jobs"
    )
    if ($HostMode -ne 'auto') { $arguments += '--host'; $arguments += $HostMode }
    if ($Distro) { $arguments += '--distro'; $arguments += $Distro }
    $arguments += $MakeArgs
    if ($DryRun) {
        Write-Host "Ruby：$ruby"
        Write-Host "命令：ruby tools/run.rb $($arguments -join ' ')"
        exit 0
    }
    & $ruby (Join-Path $PSScriptRoot 'tools\run.rb') @arguments
    exit $LASTEXITCODE
}

# ---- Ruby 不可用：退回 WSL2（旧行为）--------------------------------
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    [Console]::Error.WriteLine('未找到 Ruby，也未找到 wsl.exe。')
    exit 1
}

$distroName = if ($Distro) { $Distro } else { 'Ubuntu-26.04' }
$linuxPath = (& wsl.exe -d $distroName -- wslpath -a "$PSScriptRoot").Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($linuxPath)) {
    [Console]::Error.WriteLine("无法在 WSL 发行版 '$distroName' 中解析仓库路径。")
    exit 1
}

$target = if ($MakeArgs.Count -gt 0) { $MakeArgs -join ' ' } else { 'run' }
$command = "cd '$linuxPath' && make -j$Jobs $target " +
           "QEMU_MACHINE=$Machine QEMU_MEM=${MemMB}M QEMU_SMP=$Smp " +
           "QEMU_DISPLAY='-display $Display'"

if ($DryRun) {
    Write-Host "WSL：$distroName"
    Write-Host "命令：$command"
    exit 0
}

Write-Host "WSL 发行版：$distroName"
Write-Host "QEMU：$Machine / ${MemMB} MiB / smp=$Smp / display=$Display"
& wsl.exe -d $distroName -- bash -lc $command
exit $LASTEXITCODE
