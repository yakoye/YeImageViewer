$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Wait-DesktopTestSlot.ps1')
# 子PowerShell内的只读进程查询夹具；不创建/结束任何真实桌面测试进程。
$script:slotQueries=0;$script:slotSleeps=0;$script:slotScenario=0
function Get-CimInstance {
    param([string]$ClassName)
    $script:slotQueries++
    if($script:slotScenario -eq 0){return [pscustomobject]@{ProcessId=1;Name='Shazao-markdown.exe';CommandLine='Shazao-markdown.exe user-document.md'}}
    if($script:slotScenario -eq 1 -and $script:slotQueries -eq 1){return [pscustomobject]@{ProcessId=2;Name='Shazao-markdown.exe';CommandLine='Temp\shazao-e2e-test\Shazao-markdown.exe --automation test.md'}}
    if($script:slotScenario -eq 2 -and $script:slotQueries -eq 1){return [pscustomobject]@{ProcessId=3;Name='python.exe';CommandLine='python pressure-cycles.py build/Release/Shazao-markdown.exe'}}
    if($script:slotScenario -eq 3){return [pscustomobject]@{ProcessId=4;Name='Shazao-markdown.exe';CommandLine='Shazao-markdown.exe --automation test.md'}}
    return @()
}
function Start-Sleep {param([int]$Milliseconds) $script:slotSleeps++}
Wait-YeViewerDesktopTestSlot -QuietSeconds 0
if($script:slotQueries -ne 1 -or $script:slotSleeps -ne 0){throw '普通编辑窗口被误判为桌面测试'}
Write-Host 'PASS 模拟普通编辑窗口不会阻塞本项目测试。'
foreach($scenario in @(1,2)) {
    $script:slotScenario=$scenario;$script:slotQueries=0;$script:slotSleeps=0
    Wait-YeViewerDesktopTestSlot -QuietSeconds 0
    if($script:slotQueries -ne 2 -or $script:slotSleeps -ne 1){throw '模拟桌面测试没有让行到结束'}
    Write-Host "PASS 模拟场景$scenario：等待其他桌面测试退出后恢复，无进程写操作。"
}
$script:slotScenario=3;$script:slotQueries=0;$timeoutDetected=$false
try{Wait-YeViewerDesktopTestSlot -MaximumWaitSeconds 0 -QuietSeconds 0}catch{$timeoutDetected=$_.Exception.Message -match '尚未运行本项窗口输入'}
if(-not $timeoutDetected){throw '等待超时未区分环境阻塞与产品失败'}
Write-Host 'PASS 模拟等待超时明确标为尚未运行输入，不冒充产品断言失败。'
