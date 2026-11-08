# 通过 WSL2 中的 QEMU + OVMF 启动 Zhaoxu OS
[CmdletBinding()]
param(
    [int]$MemMB = 512,
    [int]$Smp = 1,
    [string]$Machine = 'q35',
    [ValidateSet('none', 'gtk', 'sdl')]
    [string]$Display = 'none',
    [string]$Distro = 'Ubuntu-26.04',
    [int]$Jobs = 4,
    [switch]$DryRun
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

$displayArg = "-display $Display"
$command = "cd '$linuxPath' && make -j$Jobs run " +
           "QEMU_MACHINE=$Machine QEMU_MEM=${MemMB}M QEMU_SMP=$Smp " +
           "QEMU_DISPLAY='$displayArg'"

if ($DryRun) {
    Write-Host "WSL：$Distro"
    Write-Host "命令：$command"
    exit 0
}

Write-Host "WSL 发行版：$Distro"
Write-Host "QEMU：$Machine / ${MemMB} MiB / smp=$Smp / display=$Display"
Write-Host '串口输出直接连接到当前终端；在 Shell 中输入 help 查看命令。'
& wsl.exe -d $Distro -- bash -lc $command
exit $LASTEXITCODE
