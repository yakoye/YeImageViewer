param([string]$Viewer = (Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'))
$ErrorActionPreference = 'Stop'
$base = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$owned = Join-Path $base ('YeViewer-ArchiveTest-' + [Guid]::NewGuid().ToString('N'))
try {
    $source = Join-Path $owned 'source'
    [void](New-Item -ItemType Directory -Path $source)
    Copy-Item -LiteralPath $Viewer -Destination (Join-Path $source 'YeImageViewer.exe')
    $version = (Get-Item -LiteralPath $Viewer).VersionInfo.ProductVersion
    $package = "YeImageViewer-v$version-win-x64-portable"
    foreach ($name in @("$package.zip", "$package.7z", 'SHA256SUMS.txt', 'YeImageViewer.db')) {
        [IO.File]::WriteAllText((Join-Path $source $name),'仅归档契约的最小夹具')
    }
    $roots = @((Join-Path $owned 'one'), (Join-Path $owned 'two'))
    & (Join-Path $PSScriptRoot 'archive-release.ps1') -ReleaseDirectory $source -ArchiveRoots $roots
    foreach ($root in $roots) {
        $destination = Join-Path $root "YeImageViewer\v$version"
        if (@(Get-ChildItem -LiteralPath $destination -File).Count -ne 4 -or
            (Test-Path -LiteralPath (Join-Path $destination 'YeImageViewer.db'))) {throw '归档误带配置或遗漏产物'}
    }
    & (Join-Path $PSScriptRoot 'archive-release.ps1') -ReleaseDirectory $source -ArchiveRoots $roots
    $original = (Get-FileHash -LiteralPath (Join-Path $roots[0] "YeImageViewer\v$version\$package.zip")).Hash
    [IO.File]::WriteAllText((Join-Path $source "$package.zip"),'内容不同的同版本夹具')
    $rejected = $false
    try {& (Join-Path $PSScriptRoot 'archive-release.ps1') -ReleaseDirectory $source -ArchiveRoots $roots}
    catch {$rejected = $_.Exception.Message.Contains('拒绝覆盖历史版本')}
    if (-not $rejected) {throw '归档覆盖了同版本的历史文件'}
    if ((Get-FileHash -LiteralPath (Join-Path $roots[0] "YeImageViewer\v$version\$package.zip")).Hash -ne $original) {throw '失败路径仍改写了历史压缩包'}
    Write-Host 'PASS 双目录归档、大小/哈希校验、只带产物、重复归档及拒绝覆盖历史版本。'
} finally {
    $resolved = [IO.Path]::GetFullPath($owned)
    if ($resolved.StartsWith($base,[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved).StartsWith('YeViewer-ArchiveTest-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
