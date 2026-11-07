# 定位本机 QEMU 的 OVMF 固件（CODE + VARS 模板），向管线发射一个含 Code/Vars/Source 的对象
[CmdletBinding()]
param()

# 按文件名模式优先级在一组搜索根下递归查找首个真实存在的文件，找不到返回 $null
function Find-FirmwareFile {
    param(
        [string[]]$Roots,
        [string[]]$Patterns
    )
    foreach ($pattern in $Patterns) {
        foreach ($root in $Roots) {
            if (-not (Test-Path -LiteralPath $root)) { continue }
            $hit = Get-ChildItem -LiteralPath $root -Recurse -File -Filter $pattern -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($null -ne $hit) { return $hit.FullName }
        }
    }
    return $null
}

$codePatterns = @('edk2-x86_64-code*.fd', 'OVMF_CODE*.fd', 'OVMF.fd')
$varsPatterns = @('edk2-i386-vars*.fd', 'edk2-x86_64-vars*.fd', 'OVMF_VARS*.fd')

# 1) 环境变量优先
$envCode = $env:RAVENEL_OVMF_CODE
$envVars = $env:RAVENEL_OVMF_VARS
if (-not [string]::IsNullOrWhiteSpace($envCode) -and -not [string]::IsNullOrWhiteSpace($envVars) -and
    (Test-Path -LiteralPath $envCode -PathType Leaf) -and (Test-Path -LiteralPath $envVars -PathType Leaf)) {
    [pscustomobject]@{
        Code   = $envCode
        Vars   = $envVars
        Source = '环境变量 RAVENEL_OVMF_CODE / RAVENEL_OVMF_VARS'
    }
    exit 0
}

$code = $null
$vars = $null
$source = $null

# 2) 从 PATH 上的 qemu-system-x86_64 反推安装目录，在其 share 子树递归查找
$qemuCmd = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if ($null -eq $qemuCmd) { $qemuCmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue }
if ($null -ne $qemuCmd) {
    $qemuDir = Split-Path -Parent $qemuCmd.Source
    $shareDir = Join-Path $qemuDir 'share'
    $foundCode = Find-FirmwareFile -Roots @($shareDir) -Patterns $codePatterns
    $foundVars = Find-FirmwareFile -Roots @($shareDir) -Patterns $varsPatterns
    if ($foundCode -and $foundVars) {
        $code = $foundCode
        $vars = $foundVars
        $source = "PATH 上的 QEMU 安装目录 share 子树（$shareDir）"
    }
}

# 3) 退回常见安装位置做同样的递归查找
if (-not ($code -and $vars)) {
    $fallbackRoots = @('C:\Program Files\qemu', 'C:\Program Files (x86)\qemu')
    $foundCode = Find-FirmwareFile -Roots $fallbackRoots -Patterns $codePatterns
    $foundVars = Find-FirmwareFile -Roots $fallbackRoots -Patterns $varsPatterns
    if ($foundCode -and $foundVars) {
        $code = $foundCode
        $vars = $foundVars
        $source = '常见安装位置递归查找（C:\Program Files\qemu 等）'
    }
}

# 终检：两个文件必须都真实存在才返回
if (-not $code -or -not $vars -or
    -not (Test-Path -LiteralPath $code -PathType Leaf) -or
    -not (Test-Path -LiteralPath $vars -PathType Leaf)) {
    [Console]::Error.WriteLine('未找到 OVMF 固件（需要 edk2-x86_64-code.fd 与 edk2-i386-vars.fd 这类文件）。')
    [Console]::Error.WriteLine('解决办法：')
    [Console]::Error.WriteLine('  1. 安装 QEMU for Windows 官方安装包，下载页 https://www.qemu.org/download/ ；')
    [Console]::Error.WriteLine('     固件文件通常位于安装目录的 share\ 下，例如 C:\Program Files\qemu\share\edk2-x86_64-code.fd 。')
    [Console]::Error.WriteLine('  2. 或设置环境变量 RAVENEL_OVMF_CODE / RAVENEL_OVMF_VARS 分别指向 CODE 固件与 VARS 模板的完整路径。')
    exit 1
}

[pscustomobject]@{
    Code   = $code
    Vars   = $vars
    Source = $source
}
exit 0
