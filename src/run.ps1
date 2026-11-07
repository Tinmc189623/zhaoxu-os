# 用 QEMU + OVMF 启动本仓库 esp 中构建的 Ravenel 系统
[CmdletBinding()]
param(
    [int]$MemMB = 512,
    [int]$Smp = 1,
    [string]$Machine = 'q35',
    [switch]$DryRun
)

# 按既定顺序探测 qemu-system-x86_64，返回完整路径；找不到返回 $null
function Find-QemuPath {
    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($name in @('qemu-system-x86_64', 'qemu-system-x86_64.exe')) {
        $cmd = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue
        if ($null -ne $cmd) { $candidates.Add($cmd.Source) }
    }
    $candidates.Add('C:\Program Files\qemu\qemu-system-x86_64.exe')
    $candidates.Add('C:\Program Files (x86)\qemu\qemu-system-x86_64.exe')
    if (-not [string]::IsNullOrWhiteSpace($env:QEMU)) { $candidates.Add($env:QEMU) }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

# 查询本机 QEMU 支持的显示后端并优先挑选 gtk/sdl；查不到或没有可用项时返回 $null（不传 -display）
function Get-DisplayBackend {
    param([string]$QemuPath)
    $help = & $QemuPath -display help 2>&1
    if ($LASTEXITCODE -ne 0) { return $null }
    $types = @($help | ForEach-Object { "$_".Trim() } | Where-Object { $_ -match '^[a-z][a-z0-9-]*$' })
    foreach ($preferred in @('gtk', 'sdl')) {
        if ($types -contains $preferred) { return $preferred }
    }
    return $null
}

# 把 QEMU 参数数组渲染成可读的 PowerShell 命令行文本，供 -DryRun 打印
function Format-CommandLine {
    param([string]$ExePath, [string[]]$Arguments)
    $parts = foreach ($arg in $Arguments) {
        if ($arg -match '[\s"]') { '"{0}"' -f ($arg -replace '"', '`"') } else { $arg }
    }
    return ('& "{0}" {1}' -f $ExePath, ($parts -join ' '))
}

if ($PSVersionTable.PSVersion.Major -lt 7) {
    [Console]::Error.WriteLine("必须使用 PowerShell 7+（当前 $($PSVersionTable.PSVersion)）")
    exit 1
}

$espEfiPath = Join-Path $PSScriptRoot 'esp\EFI\BOOT\BOOTX64.EFI'
if (-not (Test-Path -LiteralPath $espEfiPath -PathType Leaf)) {
    [Console]::Error.WriteLine('缺少 esp\EFI\BOOT\BOOTX64.EFI，请先执行 ./build.ps1')
    exit 1
}

$qemuPath = Find-QemuPath
if ($null -eq $qemuPath) {
    [Console]::Error.WriteLine('未找到 qemu-system-x86_64。请安装 QEMU for Windows 官方安装包：')
    [Console]::Error.WriteLine('  下载页 https://www.qemu.org/download/ （Windows 安装包会自带 OVMF 固件）；')
    [Console]::Error.WriteLine('  安装后把安装目录加入 PATH，或设置环境变量 QEMU 指向 qemu-system-x86_64.exe 的完整路径。')
    exit 1
}

$fw = & (Join-Path $PSScriptRoot 'tools\find-ovmf.ps1')
if ($LASTEXITCODE -ne 0 -or $null -eq $fw -or -not $fw.Code -or -not $fw.Vars) {
    exit 1
}

# 每次运行覆盖 VARS 模板副本，保证 NVRAM 可写且干净
$varsPath = Join-Path $PSScriptRoot 'out\OVMF_VARS.fd'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $varsPath) | Out-Null
Copy-Item -LiteralPath $fw.Vars -Destination $varsPath -Force

$display = Get-DisplayBackend -QemuPath $qemuPath

$qemuArgs = [System.Collections.Generic.List[string]]::new()
$qemuArgs.AddRange([string[]]@('-M', $Machine, '-m', "${MemMB}M", '-smp', "$Smp"))
$qemuArgs.Add('-drive')
$qemuArgs.Add("if=pflash,format=raw,unit=0,readonly=on,file=$($fw.Code)")
$qemuArgs.Add('-drive')
$qemuArgs.Add("if=pflash,format=raw,unit=1,file=$varsPath")
$qemuArgs.Add('-drive')
$qemuArgs.Add("format=raw,file=fat:rw:$(Join-Path $PSScriptRoot 'esp')")
$qemuArgs.Add('-serial')
$qemuArgs.Add('stdio')
$qemuArgs.Add('-net')
$qemuArgs.Add('none')
if ($display) {
    $qemuArgs.Add('-display')
    $qemuArgs.Add($display)
}

if ($DryRun) {
    Write-Host 'DryRun，将执行以下命令：'
    Write-Host (Format-CommandLine -ExePath $qemuPath -Arguments $qemuArgs.ToArray())
    exit 0
}

Write-Host "OVMF CODE：$($fw.Code)（来源：$($fw.Source)）"
Write-Host "OVMF VARS：$varsPath"
Write-Host "启动：$Machine / ${MemMB}M / smp=$Smp / display=$display"
& $qemuPath @qemuArgs
exit $LASTEXITCODE
