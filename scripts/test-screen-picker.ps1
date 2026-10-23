param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'New-RegressionRuntime.ps1')
. (Join-Path $PSScriptRoot 'Wait-DesktopTestSlot.ps1')
Wait-YeViewerDesktopTestSlot
$runtime=New-IsolatedViewerRuntime -SourceViewer $Viewer
$report=Join-Path $runtime.Directory 'screen-picker.txt'
$process=Start-Process -FilePath $runtime.Viewer -ArgumentList @('--screen-picker-selftest',('"'+$report+'"')) -PassThru -WindowStyle Hidden
if(-not $process.WaitForExit(30000)) {
    # 只终止本测试刚创建且句柄精确匹配的子进程，不触碰其他项目。
    $process.Kill()
    throw "快取专项超时；原始运行区：$($runtime.Directory)"
}
if(Test-Path -LiteralPath $report){Get-Content -LiteralPath $report | Write-Host}
if($process.ExitCode -ne 0){throw "快取专项失败或桌面受阻，exit=$($process.ExitCode)，证据：$report"}
Write-Host "PASS 快照 HUD 真窗口/剪贴板/GDI专项（窗口消息驱动，非多屏/真实按键全矩阵）。证据：$report"
$integration=New-IsolatedViewerRuntime -SourceViewer $Viewer
$image=Join-Path $integration.Directory 'sample.png'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/format corpus/files/common.png') -Destination $image
$tests=Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe'
Wait-YeViewerDesktopTestSlot
& $tests --picker-window-probe $integration.Viewer $image
if($LASTEXITCODE -ne 0){throw "快取主窗口集成失败。运行区：$($integration.Directory)"}
Write-Host "PASS 快取主窗口入口/重入/20次会话清理/退出专项。运行区：$($integration.Directory)"
