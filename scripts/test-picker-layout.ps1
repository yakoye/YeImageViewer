param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),[string]$OutputDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$viewerPath=(Resolve-Path -LiteralPath $Viewer).Path
if(-not $OutputDirectory){$OutputDirectory=Join-Path ([IO.Path]::GetTempPath()) ('YePickerLayout-'+[guid]::NewGuid().ToString('N'))}
if(Test-Path -LiteralPath $OutputDirectory){throw '拒绝覆盖既有离屏证据目录'}
[void](New-Item -ItemType Directory -Path $OutputDirectory)
$report=Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) 'layout.txt'
$process=Start-Process -FilePath $viewerPath -ArgumentList @('--picker-layout-selftest',('"'+$report+'"')) -PassThru -WindowStyle Hidden
try{
    if(-not $process.WaitForExit(30000)){$process.Kill();throw '取色布局离屏检查超时'}
    $result=if(Test-Path -LiteralPath $report){[IO.File]::ReadAllText($report)}else{''}
    if($process.ExitCode -ne 0 -or $result -notmatch 'passed=516 failed=0' -or @(Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.png').Count -ne 12){throw "取色布局离屏检查失败，保留证据：$report"}
}finally{$process.Dispose()}
Write-Host "PASS 取色HUD离屏516项，三语/浅深/七格式/两种说明状态/三档DPI；12张预览：$OutputDirectory。不是桌面输入或完整验收。"
