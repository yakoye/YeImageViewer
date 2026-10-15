param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$root=(Resolve-Path (Join-Path $PSScriptRoot '../test/compatibility')).Path
$output=Join-Path ([IO.Path]::GetTempPath()) ('YeViewer-Compatibility-'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $output)
$isolated=Join-Path $output 'YeImageViewer.exe'
Copy-Item -LiteralPath $Viewer -Destination $isolated
$count=0
foreach($file in Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object Extension -In '.psd','.psb') {
    $result=Join-Path $output ($file.Name+'.txt')
    $dump=Join-Path $output $file.Name
    $process=Start-Process -FilePath $isolated -ArgumentList @('--decode-probe',('"'+$file.FullName+'"'),('"'+$result+'"'),('"'+$dump+'"')) -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(15000)) {$process.Kill();throw "解码超时：$($file.Name)"}
    if($process.ExitCode -ne 0) {throw "解码失败：$($file.Name)"}
    $reference=[Drawing.Bitmap]::new((Join-Path $file.DirectoryName ($file.BaseName+'.png')))
    $actual=[Drawing.Bitmap]::new((Join-Path $dump 'primary.png'))
    try {
        if($reference.Size -ne $actual.Size) {throw "尺寸不符：$($file.Name)"}
        $maxError=0
        for($y=0;$y -lt $actual.Height;$y++) {for($x=0;$x -lt $actual.Width;$x++) {
            $a=$actual.GetPixel($x,$y);$b=$reference.GetPixel($x,$y)
            foreach($channel in @('R','G','B','A')) {$maxError=[Math]::Max($maxError,[Math]::Abs([int]$a.$channel-[int]$b.$channel))}
        }}
        if($maxError -gt 3) {throw "像素不符：$($file.Name)，最大误差 $maxError"}
        Write-Host "PASS PSD/PSB 独立参考逐像素 $($file.Name)，最大误差 $maxError"
    } finally {$actual.Dispose();$reference.Dispose()}
    $count++
}
if($count -ne 60) {throw "夹具数量不符：$count"}
foreach($file in Get-ChildItem -LiteralPath $root -File -Filter 'cmyk-*.tif') {
    $dump=Join-Path $output $file.Name
    $result=Join-Path $output ($file.Name+'.txt')
    $process=Start-Process -FilePath $isolated -ArgumentList @('--decode-probe',('"'+$file.FullName+'"'),('"'+$result+'"'),('"'+$dump+'"')) -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(15000)) {$process.Kill();throw 'CMYK TIFF 解码超时'}
    if($process.ExitCode -ne 0) {throw "CMYK TIFF 解码失败：$($file.Name)"}
    $fields=[IO.File]::ReadAllText($result).Trim() -split "`t"
    if($file.BaseName -eq 'cmyk-multipage' -and ($fields[3] -ne '2' -or $fields[4] -ne 'animated')) {throw 'CMYK TIFF 丢失后续页面'}
    $actual=[Drawing.Bitmap]::new((Join-Path $dump 'primary.png'))
    try {
        $red=$actual.GetPixel(0,0);$white=$actual.GetPixel(3,0);$black=$actual.GetPixel(0,1)
        if($actual.Width -ne 4 -or $actual.Height -ne 2 -or $red.R -lt 250 -or $red.G -gt 3 -or $red.B -gt 3 -or
            $white.R -lt 250 -or $white.G -lt 250 -or $white.B -lt 250 -or $black.R -gt 3 -or $black.G -gt 3 -or $black.B -gt 3) {throw "CMYK TIFF 墨量／K／位深转换不符：$($file.Name)"}
    } finally {$actual.Dispose()}
    Write-Host "PASS 8/16 位 CMYK TIFF 颜色、压缩和页数 $($file.Name)"
}
$file=Join-Path $root 'metadata-large.tif'
$metadataDump=Join-Path $output 'metadata-test'
$process=Start-Process -FilePath $isolated -ArgumentList @('--decode-probe',('"'+$file+'"'),('"'+(Join-Path $output 'metadata.txt')+'"'),('"'+$metadataDump+'"')) -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(15000)) {$process.Kill();throw '大元数据夹具解码超时'}
if($process.ExitCode -ne 0) {throw '大元数据夹具解码失败'}
$text=[IO.File]::ReadAllText((Join-Path $metadataDump 'metadata.txt'))
if($text.Length -gt 8192 -or $text -notmatch '131072.*(省略|omitted)') {throw '大元数据仍展开或没有明确省略说明'}
Write-Host 'PASS TIFF 大型二进制元数据保留大小说明，不展开成巨型字符串'
Write-Host "SUMMARY PSD/PSB 正确出图 $count 项；证据目录：$output"
