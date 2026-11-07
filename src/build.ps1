# 从 boot\main.asm 汇编出 UEFI 可引导文件 out\BOOTX64.EFI 并校验 PE 头
[CmdletBinding()]
param([switch]$Headless)

# 按既定顺序探测本机 fasm.exe，返回完整路径；找不到返回 $null
function Find-FasmPath {
    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($env:FASM)) { $candidates.Add($env:FASM) }
    foreach ($name in @('fasm', 'fasm.exe')) {
        $cmd = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue
        if ($null -ne $cmd) { $candidates.Add($cmd.Source) }
    }
    $candidates.Add((Join-Path $PSScriptRoot 'tools\fasm.exe'))
    $candidates.Add((Join-Path $PSScriptRoot 'tools\fasm\FASM.EXE'))
    $candidates.Add('D:\fasm\FASM.EXE')
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

# 读取 PE 头，校验 MZ/PE 签名、x86_64 Machine、PE32+ Magic、EFI_APPLICATION 子系统，返回实测字段与结论
function Test-EfiPeHeader {
    param([string]$Path)

    $failures = [System.Collections.Generic.List[string]]::new()
    $machine = 0
    $magic = 0
    $subsystem = 0

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64) {
        return [pscustomobject]@{ Pass = $false; Detail = "文件过短，仅 $($bytes.Length) 字节，不是有效 PE"; }
    }

    $mzActual = '0x{0:X2}{1:X2}' -f $bytes[0], $bytes[1]
    if (-not ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A)) {
        $failures.Add("MZ 签名不符，实测 $mzActual")
    }

    $lfanew = [System.BitConverter]::ToUInt32($bytes, 60)
    if (($lfanew + 24 + 70) -gt [uint64]$bytes.Length) {
        $failures.Add("e_lfanew 越界，实测 0x$($lfanew.ToString('X8'))，文件仅 $($bytes.Length) 字节")
        return [pscustomobject]@{ Pass = $false; Detail = ($failures -join '；') }
    }

    $peActual = '0x{0:X2}{1:X2}{2:X2}{3:X2}' -f $bytes[$lfanew], $bytes[$lfanew + 1], $bytes[$lfanew + 2], $bytes[$lfanew + 3]
    if (-not ($bytes[$lfanew] -eq 0x50 -and $bytes[$lfanew + 1] -eq 0x45 -and
            $bytes[$lfanew + 2] -eq 0x00 -and $bytes[$lfanew + 3] -eq 0x00)) {
        $failures.Add("PE 签名不符，实测 $peActual（应为 0x50450000）")
    }

    $machine = [System.BitConverter]::ToUInt16($bytes, $lfanew + 4)
    if ($machine -ne 0x8664) {
        $failures.Add("Machine 不符，实测 0x$($machine.ToString('X4'))（应为 0x8664）")
    }

    $magic = [System.BitConverter]::ToUInt16($bytes, $lfanew + 24)
    if ($magic -ne 0x20B) {
        $failures.Add("Optional Header Magic 不符，实测 0x$($magic.ToString('X4'))（应为 0x20B 即 PE32+）")
    }

    $subsystem = [System.BitConverter]::ToUInt16($bytes, $lfanew + 24 + 68)
    if ($subsystem -ne 10) {
        $failures.Add("Subsystem 不符，实测 $subsystem（应为 10 即 EFI_APPLICATION）")
    }

    if ($failures.Count -gt 0) {
        return [pscustomobject]@{ Pass = $false; Detail = ($failures -join '；') }
    }
    return [pscustomobject]@{
        Pass = $true
        Detail = "MZ/PE 有效，Machine=0x$($machine.ToString('X4'))，Magic=0x$($magic.ToString('X4'))，Subsystem=$subsystem"
    }
}

if ($PSVersionTable.PSVersion.Major -lt 7) {
    [Console]::Error.WriteLine("必须使用 PowerShell 7+（当前 $($PSVersionTable.PSVersion)）")
    exit 1
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

$fasm = Find-FasmPath
if ($null -eq $fasm) {
    [Console]::Error.WriteLine('未找到 fasm.exe。请按以下任一方式安装：')
    [Console]::Error.WriteLine('  1. 下载 Flat Assembler 官方 Windows 包 https://flatassembler.net/fasmw17335.zip ，')
    [Console]::Error.WriteLine("     解压后把 FASM.EXE 放到 $(Join-Path $PSScriptRoot 'tools\fasm.exe') 或 D:\fasm\FASM.EXE ；")
    [Console]::Error.WriteLine('  2. 或设置环境变量 FASM 指向 fasm.exe 的完整路径。')
    exit 1
}

# 确保输出目录存在
$buildDir = Join-Path $PSScriptRoot 'build'
$outDir = Join-Path $PSScriptRoot 'out'
$espBootDir = Join-Path $PSScriptRoot 'esp\EFI\BOOT'
foreach ($dir in @($buildDir, $outDir, $espBootDir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

# 按参数重写给汇编源码 include 的配置头
$flag = if ($Headless) { '1' } else { '0' }
$configPath = Join-Path $buildDir 'config.inc'
[System.IO.File]::WriteAllText($configPath, "HEADLESS equ $flag`r`n")
Write-Host "配置头 build\config.inc：HEADLESS equ $flag"

$srcPath = Join-Path $PSScriptRoot 'boot\main.asm'
if (-not (Test-Path -LiteralPath $srcPath -PathType Leaf)) {
    [Console]::Error.WriteLine('缺少 boot\main.asm（内核源码尚未就位）')
    exit 1
}

$efiPath = Join-Path $outDir 'BOOTX64.EFI'
Write-Host "FASM：$fasm"
Write-Host "汇编：$srcPath -> $efiPath"

& $fasm $srcPath $efiPath
if ($LASTEXITCODE -ne 0) {
    [Console]::Error.WriteLine("fasm 汇编失败，退出码 $LASTEXITCODE")
    exit 1
}

$espEfiPath = Join-Path $espBootDir 'BOOTX64.EFI'
Copy-Item -LiteralPath $efiPath -Destination $espEfiPath -Force

$pe = Test-EfiPeHeader -Path $efiPath
if (-not $pe.Pass) {
    [Console]::Error.WriteLine("产物 PE 校验失败：$($pe.Detail)")
    exit 1
}

$sw.Stop()
$size = (Get-Item -LiteralPath $efiPath).Length
Write-Host "构建成功：out\BOOTX64.EFI（$size 字节），已复制到 esp\EFI\BOOT\BOOTX64.EFI"
Write-Host "PE 校验通过：$($pe.Detail)"
Write-Host "耗时 $([math]::Round($sw.Elapsed.TotalSeconds, 2)) 秒"
exit 0
