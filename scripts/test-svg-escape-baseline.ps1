# 从完整回归重用原生辅助类和实际快捷键函数，定位SVG初始状态；不改变产品/配置原件。
param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),[string]$SeedDatabase='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$source=[IO.File]::ReadAllText((Join-Path $root 'runTests.ps1'))
$start=$source.IndexOf('Add-Type @"');$end=$source.IndexOf('"@',$start+12)+2
& ([scriptblock]::Create($source.Substring($start,$end-$start)))
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($source,[ref]$tokens,[ref]$errors)
$function=$ast.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Set-CloseImageShortcut'},$true)
. ([scriptblock]::Create($function.Extent.Text))
. (Join-Path $PSScriptRoot 'New-RegressionRuntime.ps1')
$suiteRuntime=New-IsolatedViewerRuntime -SourceViewer $Viewer
if($SeedDatabase){Copy-Item -LiteralPath $SeedDatabase -Destination (Join-Path $suiteRuntime.Directory 'YeImageViewer.db')}
$env:YEIMAGEVIEWER_INTERACTION_PROBE=Join-Path $suiteRuntime.Directory 'svg-state.txt'
$env:YEIMAGEVIEWER_STARTUP_TRACE=Join-Path $suiteRuntime.Directory 'startup.txt'
$env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR='1'
$image=Join-Path $root 'test/SVG Blurring/SittingHuman.svg'
$process=Start-Process -FilePath $suiteRuntime.Viewer -ArgumentList ('"'+$image+'"') -PassThru
try {
    $deadline=[DateTime]::UtcNow.AddSeconds(6)
    do {Start-Sleep -Milliseconds 200;$process.Refresh()} while(-not $process.HasExited -and $process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if($process.HasExited -or -not $process.MainWindowHandle){throw 'SVG未建窗'}
    $window=[IntPtr]$process.MainWindowHandle
    Write-Host "runtime=$($suiteRuntime.Directory) initial-style=$([YeImageViewerTestNativeV1365]::GetWindowLongPtr($window,-16))"
    foreach($command in @(1100,1101,1102,1103,1100)){[void][YeImageViewerTestNativeV1365]::SendMessage($window,0x0111,[UIntPtr]$command,[IntPtr]::Zero);Start-Sleep -Milliseconds 150}
    Write-Host "after-background-style=$([YeImageViewerTestNativeV1365]::GetWindowLongPtr($window,-16))"
    Set-CloseImageShortcut -ViewerWindow $window -ProcessId $process.Id -Assign $false
    $style=[YeImageViewerTestNativeV1365]::GetWindowLongPtr($window,-16).ToInt64();Write-Host "after-shortcut-style=$style"
    if(Test-Path $env:YEIMAGEVIEWER_INTERACTION_PROBE){Get-Content $env:YEIMAGEVIEWER_INTERACTION_PROBE}
    if(($style -band 0x00C00000) -ne 0){throw 'SVG初始并非沉浸，保留此反例'}
    [void][YeImageViewerTestNativeV1365]::SendMessage($window,0x0100,[UIntPtr]0x1b,[IntPtr]::Zero);Start-Sleep -Milliseconds 250
    if(([YeImageViewerTestNativeV1365]::GetWindowLongPtr($window,-16).ToInt64() -band 0x00C00000) -eq 0){throw 'Esc未退出沉浸'}
    Write-Host 'PASS SVG独立初始/背景/设置/Esc状态链'
} finally {if(-not $process.HasExited){[void]$process.CloseMainWindow();if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id}}}
