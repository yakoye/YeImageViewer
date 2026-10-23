function New-IsolatedViewerRuntime {
    param([Parameter(Mandatory=$true)][string]$SourceViewer)
    $source = Get-Item -LiteralPath $SourceViewer -ErrorAction Stop
    $provider = Join-Path $source.DirectoryName 'YeThumbnailProvider.dll'
    if (-not (Test-Path -LiteralPath $provider -PathType Leaf)) { throw '隔离测试缺少缩略图 DLL' }
    $directory = Join-Path ([IO.Path]::GetTempPath()) ('YeImageViewer-Suite-' + [guid]::NewGuid().ToString('N'))
    [void](New-Item -ItemType Directory -Path $directory -ErrorAction Stop)
    if(-not $env:YEIMAGEVIEWER_COLOR_DATA_DIR){$env:YEIMAGEVIEWER_COLOR_DATA_DIR=Join-Path $directory 'color-data'}
    $viewer = Join-Path $directory 'YeImageViewer.exe'
    Copy-Item -LiteralPath $source.FullName -Destination $viewer -ErrorAction Stop
    Copy-Item -LiteralPath $provider -Destination (Join-Path $directory 'YeThumbnailProvider.dll') -ErrorAction Stop
    $layerBackend=Join-Path $source.DirectoryName 'YeLayerBackend.dll'
    if(-not(Test-Path -LiteralPath $layerBackend -PathType Leaf)){throw '隔离测试缺少PhotoCraft图层后端'}
    Copy-Item -LiteralPath $layerBackend -Destination (Join-Path $directory 'YeLayerBackend.dll') -ErrorAction Stop
    $notices=Join-Path $source.DirectoryName 'LAYER_BACKEND_NOTICES.txt'
    if(-not(Test-Path -LiteralPath $notices -PathType Leaf)){throw '隔离测试缺少PhotoCraft后端许可文件'}
    Copy-Item -LiteralPath $notices -Destination (Join-Path $directory 'LAYER_BACKEND_NOTICES.txt') -ErrorAction Stop
    [pscustomobject]@{Directory=$directory;Viewer=$viewer}
}

function Assert-IsolatedViewerRuntime {
    param([Parameter(Mandatory=$true)][string]$Viewer)
    $path = [IO.Path]::GetFullPath($Viewer)
    $directory = Split-Path -Parent $path
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $path.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $directory) -notmatch '^YeImageViewer-Suite-[a-f0-9]{32}$' -or
        (Test-Path -LiteralPath (Join-Path $directory 'YeImageViewer.db'))) {
        throw '回归必须使用新的隔离运行目录，不能沿用开发目录或旧配置'
    }
}
