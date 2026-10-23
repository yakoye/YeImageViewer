param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),[switch]$LegacyBlur)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
& (Join-Path $PSScriptRoot 'test-background-source.ps1')
. (Join-Path $PSScriptRoot 'New-RegressionRuntime.ps1')
. (Join-Path $PSScriptRoot 'Wait-DesktopTestSlot.ps1')
Wait-YeViewerDesktopTestSlot
$runtime=New-IsolatedViewerRuntime -SourceViewer $Viewer
$fixture=Join-Path $runtime.Directory 'background-alpha.svg'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/image-tools/background-alpha.svg') -Destination $fixture
$testProgram=Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe'
$previous=$env:YEIMAGEVIEWER_TEST_LEGACY_BLUR
try {
    if($LegacyBlur){$env:YEIMAGEVIEWER_TEST_LEGACY_BLUR='1'}
    & $testProgram --background-window-probe $runtime.Viewer $fixture
    if($LASTEXITCODE -ne 0){throw "背景真窗口合成回归失败，保留运行区：$($runtime.Directory)"}
} finally {$env:YEIMAGEVIEWER_TEST_LEGACY_BLUR=$previous}
Write-Host "PASS 背景真实像素、两种窗口形态和重启持久化。运行区：$($runtime.Directory)"
