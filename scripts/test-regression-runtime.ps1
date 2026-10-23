$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'New-RegressionRuntime.ps1')
$temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$source = Join-Path $temp ('YeImageViewer-RuntimeTest-' + [guid]::NewGuid().ToString('N'))
$owned = @($source)
try {
    [void](New-Item -ItemType Directory -Path $source)
    $exe = Join-Path $source 'YeImageViewer.exe'
    $dll = Join-Path $source 'YeThumbnailProvider.dll'
    $db = Join-Path $source 'YeImageViewer.db'
    [IO.File]::WriteAllBytes($exe,[byte[]](1,2,3,4))
    [IO.File]::WriteAllBytes($dll,[byte[]](5,6,7,8))
    [IO.File]::WriteAllBytes((Join-Path $source 'YeLayerBackend.dll'),[byte[]](9,10,11,12))
    [IO.File]::WriteAllText((Join-Path $source 'LAYER_BACKEND_NOTICES.txt'),'隔离运行也必须完整携带后端许可')
    [IO.File]::WriteAllText($db,'开发目录配置必须保持不动')
    $before = (Get-FileHash -LiteralPath $db).Hash
    $rejected = $false
    try { Assert-IsolatedViewerRuntime -Viewer $exe } catch { $rejected = $true }
    if (-not $rejected) { throw '旧的直接运行方式没有被拒绝' }
    $one = New-IsolatedViewerRuntime -SourceViewer $exe
    $owned += $one.Directory
    Assert-IsolatedViewerRuntime -Viewer $one.Viewer
    foreach ($name in @('YeImageViewer.exe','YeThumbnailProvider.dll','YeLayerBackend.dll','LAYER_BACKEND_NOTICES.txt')) {
        if ((Get-FileHash -LiteralPath (Join-Path $source $name)).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $one.Directory $name)).Hash) { throw '运行文件副本不同' }
    }
    [IO.File]::WriteAllText((Join-Path $one.Directory 'YeImageViewer.db'),'测试自己新写的配置')
    $two = New-IsolatedViewerRuntime -SourceViewer $exe
    $owned += $two.Directory
    Assert-IsolatedViewerRuntime -Viewer $two.Viewer
    if ($one.Directory -eq $two.Directory -or (Get-FileHash -LiteralPath $db).Hash -ne $before) {
        throw '隔离运行沿用配置或改写了源配置'
    }
    Write-Host 'PASS 回归运行隔离：旧方式被拒绝、EXE/DLL 哈希一致、不复制 DB、每轮独立、源配置不变。'
} finally {
    foreach ($directory in $owned) {
        $resolved = [IO.Path]::GetFullPath($directory)
        if (-not $resolved.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or
            (Split-Path -Leaf $resolved) -notmatch '^YeImageViewer-(Suite|RuntimeTest)-[a-f0-9]{32}$') {
            throw '拒绝清理不属于隔离测试的目录'
        }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
}
