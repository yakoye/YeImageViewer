param(
    [Parameter(Mandatory)][string]$InputDirectory,
    [string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),
    [string]$BaselineViewer,
    [ValidateRange(3,20)][int]$Rounds=5,
    [string]$OutputDirectory=(Join-Path $PSScriptRoot '../artifacts/compatibility-v1382/benchmark')
)
$ErrorActionPreference='Stop'
$files=@(Get-ChildItem -LiteralPath $InputDirectory -File | Sort-Object Name)
if(-not $files.Count) {throw '没有测试文件'}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$output=(Resolve-Path -LiteralPath $OutputDirectory).Path
$versions=@{当前=$Viewer}
if($BaselineViewer) {$versions['基线']=$BaselineViewer}
$temporary=Join-Path ([IO.Path]::GetTempPath()) ('YeViewer-DecodeBenchmark-'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $temporary)
$executables=@{}
foreach($version in $versions.Keys) {
    $directory=Join-Path $temporary $version
    [void](New-Item -ItemType Directory -Path $directory)
    $executables[$version]=Join-Path $directory 'YeImageViewer.exe'
    Copy-Item -LiteralPath $versions[$version] -Destination $executables[$version]
    Write-Host ("构建 $version SHA-256="+(Get-FileHash -LiteralPath $executables[$version]).Hash)
}
$rows=for($round=1;$round -le $Rounds;$round++) {
    $order=@($versions.Keys | Sort-Object)
    if($round % 2 -eq 0) {[array]::Reverse($order)}
    foreach($file in $files) {foreach($version in $order) {
        $result=Join-Path $temporary ([Guid]::NewGuid().ToString('N')+'.txt')
        $clock=[Diagnostics.Stopwatch]::StartNew()
        $process=Start-Process -FilePath $executables[$version] -ArgumentList @('--decode-probe',('"'+$file.FullName+'"'),('"'+$result+'"')) -WindowStyle Hidden -PassThru
        $finished=$process.WaitForExit(60000)
        if(-not $finished) {$process.Kill();$process.WaitForExit()}
        $clock.Stop()
        $fields=if(Test-Path -LiteralPath $result){[IO.File]::ReadAllText($result).Trim() -split "`t"}else{@('ERROR')}
        $row=[pscustomobject]@{文件=$file.Name;版本=$version;轮次=$round;总毫秒=$clock.ElapsedMilliseconds;解码毫秒=$(if($fields.Count -ge 16){[int]$fields[15]}else{$null});成功=($finished -and $process.ExitCode -eq 0 -and $fields[0] -eq 'OK');内嵌预览=$(if($fields.Count -ge 15){$fields[14] -eq '1'}else{$null});结果=($fields -join '|')}
        Write-Host ("完成 $round/$Rounds $version $($file.Name)：$($row.总毫秒) ms，成功=$($row.成功)")
        $row
    }}
}
$rows | ConvertTo-Json -Depth 4 | Out-File (Join-Path $output '全部轮次.json') -Encoding utf8
$summary=foreach($group in $rows | Group-Object 文件,版本) {
    $times=@($group.Group.总毫秒 | Sort-Object)
    $middle=[int][Math]::Floor($times.Count/2)
    $median=if($times.Count % 2){$times[$middle]}else{($times[$middle-1]+$times[$middle])/2.0}
    [pscustomobject]@{文件=$group.Group[0].文件;版本=$group.Group[0].版本;成功次数=@($group.Group | Where-Object 成功).Count;轮次=$Rounds;总耗时中位毫秒=$median}
}
$summary | ConvertTo-Json | Out-File (Join-Path $output '中位数.json') -Encoding utf8
$summary | Format-Table -AutoSize
Write-Host "本测试是已有文件缓存下的独立进程解码，不是冷盘、首帧或 GuoheView GUI 启动耗时；证据 $temporary"
