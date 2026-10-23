# 只复制检查器实际读取的文本，不复制库、构建产物或用户配置；绝不修改输入目录。
function New-SourceInvariantSnapshot {
    param([Parameter(Mandatory)][string]$RepoRoot)
    $sourceRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
    $snapshot = Join-Path ([IO.Path]::GetTempPath()) ('YeImageViewer-Invariants-' + [Guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $snapshot)
    $sources = @(Get-ChildItem -Path (Join-Path $sourceRoot 'YeImageViewer') -Recurse -File -Include *.cpp,*.h,*.rc)
    foreach ($relative in @('README.md','README_EN.md','test/README.md','tools/image-test-runner/probe-progressive-load.ps1')) {
        $sources += Get-Item -LiteralPath (Join-Path $sourceRoot $relative)
    }
    foreach ($file in $sources) {
        $relative = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName)
        $target = Join-Path $snapshot $relative
        [void](New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force)
        Copy-Item -LiteralPath $file.FullName -Destination $target
        if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
            throw "源码快照复制不一致，停止破坏测试：$relative"
        }
    }
    return $snapshot
}
