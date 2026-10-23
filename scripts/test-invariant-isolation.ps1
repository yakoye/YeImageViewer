param([string]$RepoRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'New-SourceInvariantSnapshot.ps1')
$fixture = New-SourceInvariantSnapshot -RepoRoot $RepoRoot
$files = @(Get-ChildItem -LiteralPath $fixture -Recurse -File)
$hashes = @{}
foreach ($file in $files) { $hashes[$file.FullName] = (Get-FileHash -LiteralPath $file.FullName).Hash }
$locked = $null
try {
    # 在私有夹具锁住 main.cpp：旧脚本会尝试写原目录并失败；新脚本只改它自己的快照。
    $locked = [IO.File]::Open((Join-Path $fixture 'YeImageViewer/src/main.cpp'),
        [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    & (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File (Join-Path $PSScriptRoot 'verify-source-invariant-checks.ps1') -RepoRoot $fixture
    if ($LASTEXITCODE -ne 0) { throw '输入源文件只读时，源码破坏检查未能安全运行' }
} finally {
    if ($locked) { $locked.Dispose() }
}
foreach ($file in $files) {
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne $hashes[$file.FullName]) {
        throw "破坏检查改变了输入目录：$($file.FullName)"
    }
}
Write-Host "PASS 输入源码只读时，全部破坏检查通过且所有输入文件哈希保持不变。夹具：$fixture"
