param([switch]$UnexpectedBackspace)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'Wait-DesktopTestSlot.ps1')
Wait-YeViewerDesktopTestSlot
$runner=Join-Path $repo 'runTests.ps1'
$text=[IO.File]::ReadAllText($runner)
$blocks=@([regex]::Matches($text,'(?s)Add-Type @"\r?\n(.*?)\r?\n"@') | Where-Object {$_.Groups[1].Value.Contains('public static class YeImageViewerTestNativeV1365')})
if($blocks.Count -ne 1){throw '未唯一找到生产回归原生适配器'}
if(-not ('YeImageViewerTestNativeV1365' -as [type])) {Add-Type -TypeDefinition $blocks[0].Groups[1].Value}
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($text,[ref]$tokens,[ref]$errors)
if($errors.Count){throw '回归脚本语法错误'}
$function=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Set-CloseImageShortcut'},$true)
if(-not $function){throw '没有找到真实测试函数'}
. ([scriptblock]::Create($function.Extent.Text))
. (Join-Path $repo 'scripts/New-RegressionRuntime.ps1')
. (Join-Path $repo 'scripts/KeyboardRecordingAudit.ps1')
$source=Join-Path $repo 'x64/Release/YeImageViewer.exe'
$sourceDb=Join-Path (Split-Path $source) 'YeImageViewer.db'
$before=if(Test-Path -LiteralPath $sourceDb){(Get-FileHash -LiteralPath $sourceDb).Hash}else{'missing'}
$suiteRuntime=New-IsolatedViewerRuntime -SourceViewer $source
Assert-IsolatedViewerRuntime -Viewer $suiteRuntime.Viewer
$image=Join-Path $suiteRuntime.Directory 'probe.png'
Copy-Item -LiteralPath (Join-Path $repo 'test/format corpus/files/common.png') -Destination $image
$process=Start-Process -FilePath $suiteRuntime.Viewer -ArgumentList ('"'+$image+'"') -PassThru
try {
    [YeViewerRecordingAudit]::Start([uint32]$process.Id)
    $deadline=[DateTime]::UtcNow.AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 100
        $window=[YeImageViewerTestNativeV1365]::FindProcessWindow($process.Id,'D3D11WndClass')
    }while($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if($window -eq [IntPtr]::Zero){throw '隔离程序未建窗'}
    Start-Sleep -Milliseconds 500
    Set-CloseImageShortcut -ViewerWindow $window -ProcessId $process.Id -Assign $false
    # 在自己的设置窗口制造本次实际遇到的 Ctrl 按下状态，不能仅测试干净键盘。
    [void][YeImageViewerTestNativeV1365]::SendMessage($window,0x0100,[UIntPtr]0x72,[IntPtr]::Zero)
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $setting=[YeImageViewerTestNativeV1365]::FindProcessWindow($process.Id,'YeImageViewerSettingWnd')
    }while($setting -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if($setting -eq [IntPtr]::Zero){throw '未能为修饰键回归打开设置'}
    [YeImageViewerTestNativeV1365]::ActivateForKeyboard($setting)
    if([YeImageViewerTestNativeV1365]::GetForegroundWindow() -ne $setting){
        $foregroundWindow=[YeImageViewerTestNativeV1365]::GetForegroundWindow()
        $foregroundClass=New-Object Text.StringBuilder 256
        [uint32]$foregroundProcess=0
        [void][YeImageViewerTestNativeV1365]::GetClassName($foregroundWindow,$foregroundClass,256)
        [void][YeImageViewerTestNativeV1365]::GetWindowThreadProcessIdForEnum($foregroundWindow,[ref]$foregroundProcess)
        throw "修饰键回归前台不符：目标=$setting，可见=$([YeImageViewerTestNativeV1365]::IsWindowVisible($setting))，启用=$([YeImageViewerTestNativeV1365]::IsWindowEnabled($setting))；实际=$foregroundWindow，类=$foregroundClass，进程=$foregroundProcess"
    }
    [YeImageViewerTestNativeV1365]::keybd_event(0xA2,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    if(([YeImageViewerTestNativeV1365]::GetAsyncKeyState(0x11) -band 0x8000) -eq 0){throw '未实际制造 Ctrl 按下条件'}
    $diagnosticKey=if($UnexpectedBackspace){[byte]8}else{[byte]27}
    Set-CloseImageShortcut -ViewerWindow $window -ProcessId $process.Id -Assign $true -DiagnosticKey $diagnosticKey
    [void][YeImageViewerTestNativeV1365]::SendMessage($window,0x0100,[UIntPtr]0x1B,[IntPtr]::Zero)
    if(-not $process.WaitForExit(5000)){throw 'Esc 录制为27后没有关闭程序'}
    $after=if(Test-Path -LiteralPath $sourceDb){(Get-FileHash -LiteralPath $sourceDb).Hash}else{'missing'}
    if($after -ne $before){throw '诊断污染了开发配置'}
    Write-Host 'PASS 同一回归函数实际清空/录回 Esc=27、关闭图片且源 DB 哈希不变。'
}finally{
    Write-Host ('快捷键输入只读诊断（仅被测进程的 Esc/Backspace）：'+[Environment]::NewLine+[YeViewerRecordingAudit]::Stop())
    [YeImageViewerTestNativeV1365]::keybd_event(0xA2,0,2,[UIntPtr]::Zero)
    [YeImageViewerTestNativeV1365]::keybd_event(0x1B,0,2,[UIntPtr]::Zero)
    if(-not $process.HasExited){Stop-Process -Id $process.Id -Force}
    Write-Host "保留诊断目录：$($suiteRuntime.Directory)"
}
