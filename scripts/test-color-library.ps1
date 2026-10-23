param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'New-RegressionRuntime.ps1')
. (Join-Path $PSScriptRoot 'Wait-DesktopTestSlot.ps1')
$runtime=New-IsolatedViewerRuntime -SourceViewer $Viewer
$env:YEIMAGEVIEWER_COLOR_DATA_DIR=Join-Path $runtime.Directory 'library-data'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/image-tools/orientation-alpha.png') -Destination (Join-Path $runtime.Directory 'palette-source.png')
Wait-YeViewerDesktopTestSlot
& (Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe') --color-navigation-probe $runtime.Viewer
if($LASTEXITCODE -ne 0){throw "颜色导航/永久历史/收藏真窗口失败，保留：$($runtime.Directory)"}
Write-Host "PASS 颜色数据完整真实窗口路径，独立测试数据：$($runtime.Directory)"
