# 通过 WSL2 构建 Zhaoxu OS（FASM 引导层 + C 内核 + Ada 驱动 + C++ Shell）
[CmdletBinding()]
param(
    [switch]$Headless,
    [string]$Distro = 'Ubuntu-26.04',
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    [Console]::Error.WriteLine('未找到 wsl.exe，请先安装并配置 WSL2。')
    exit 1
}

$linuxPath = (& wsl.exe -d $Distro -- wslpath -a "$PSScriptRoot").Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($linuxPath)) {
    [Console]::Error.WriteLine("无法在 WSL 发行版 '$Distro' 中解析仓库路径。")
    [Console]::Error.WriteLine('可用发行版：wsl.exe -l -q；可用 -Distro 指定。')
    exit 1
}

$flag = if ($Headless) { 1 } else { 0 }
$command = "cd '$linuxPath' && make -j$Jobs HEADLESS=$flag"

Write-Host "WSL 发行版：$Distro"
Write-Host "构建目录：$linuxPath"
Write-Host "HEADLESS=$flag, jobs=$Jobs"
& wsl.exe -d $Distro -- bash -lc $command
$code = $LASTEXITCODE

if ($code -eq 0) {
    $efi = Join-Path $PSScriptRoot 'out\BOOTX64.EFI'
    if (Test-Path -LiteralPath $efi) {
        $size = (Get-Item -LiteralPath $efi).Length
        Write-Host "构建成功：out/BOOTX64.EFI（$size 字节）"
        Write-Host '已部署：esp/EFI/BOOT/BOOTX64.EFI'
    }
}
exit $code
