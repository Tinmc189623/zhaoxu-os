# Zhaoxu OS 运行入口（PowerShell -> WSL make + QEMU）
#
# 构建系统是 Makefile；Windows 默认在 WSL2 里执行 make run-iso。
# 只有显式 -HostMode native 才走原生 MSYS2 的 Ruby 包装脚本。
[CmdletBinding()]
param(
    [int]$MemMB = 2048,
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
$wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
$useWsl = ($HostMode -ne 'native') -and $wsl

# ---- 原生 MSYS2：Ruby 辅助脚本 ---------------------------------------
if (-not $useWsl) {
    if (-not $ruby) {
        if ($wsl) {
            [Console]::Error.WriteLine('WSL2 已检测到：去掉 -HostMode native 就直接用 WSL2 的 make；native 模式需要 Ruby。')
        } else {
            [Console]::Error.WriteLine('未检测到 wsl.exe；安装 WSL2，或安装 Ruby 后使用 -HostMode native。')
        }
        exit 1
    }
    $arguments = @(
        '--display', $Display,
        '--machine', $Machine,
        '--mem', "$MemMB",
        '--smp', "$Smp",
        '--jobs', "$Jobs"
    )
    if ($HostMode -eq 'native') { $arguments += '--host'; $arguments += 'native' }
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

# ---- 默认：WSL2 内的 make run-iso ------------------------------------
$distroName = if ($Distro) { $Distro } else { 'Ubuntu-26.04' }
$linuxPath = (& wsl.exe -d $distroName -- wslpath -a "$PSScriptRoot").Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($linuxPath)) {
    [Console]::Error.WriteLine("无法在 WSL 发行版 '$distroName' 中解析仓库路径。")
    exit 1
}

$target = if ($MakeArgs.Count -gt 0) { $MakeArgs -join ' ' } else { 'run-iso' }
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
