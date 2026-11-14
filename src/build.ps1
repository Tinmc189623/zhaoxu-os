# Zhaoxu OS 构建入口（PowerShell -> WSL make）
#
# 构建系统是 Makefile；Windows 默认在 WSL2 里直接 make。只有显式
# -HostMode native 才走原生 MSYS2 的 Ruby 包装脚本（tools/build.rb）。
# -Check 使用 Ruby 的宿主探测脚本，因为它要枚举工具矩阵。
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
$wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
$useWsl = ($HostMode -ne 'native') -and $wsl

# ---- 探测 / 原生 MSYS2：Ruby 辅助脚本 --------------------------------
if ($Check -or -not $useWsl) {
    if (-not $ruby) {
        [Console]::Error.WriteLine('Ruby 不可用：-Check 和 -HostMode native 需要 tools/build.rb。')
        if ($wsl) {
            [Console]::Error.WriteLine('WSL2 已检测到；默认构建直接走 WSL2 的 make，不需要 Ruby。')
        } else {
            [Console]::Error.WriteLine('未检测到 wsl.exe；安装 WSL2，或安装 Ruby 后使用原生模式。')
        }
        exit 1
    }
    $arguments = @()
    if ($Headless) { $arguments += '--headless' }
    if ($Check) { $arguments += '--check' }
    if ($HostMode -eq 'native') {
        $arguments += '--host'; $arguments += 'native'
    } elseif ($HostMode -eq 'wsl') {
        $arguments += '--host'; $arguments += 'wsl'
    } elseif ($Check -and $wsl) {
        # --check 默认也报告首选的 WSL2 工具矩阵
        $arguments += '--host'; $arguments += 'wsl'
    }
    if ($Distro) { $arguments += '--distro'; $arguments += $Distro }
    if ($Jobs -gt 0) { $arguments += '--jobs'; $arguments += "$Jobs" }
    $arguments += $MakeArgs
    & $ruby (Join-Path $PSScriptRoot 'tools\build.rb') @arguments
    exit $LASTEXITCODE
}

# ---- 默认：WSL2 内的 make --------------------------------------------
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
