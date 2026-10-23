param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'New-RegressionRuntime.ps1')
$runtime=New-IsolatedViewerRuntime -SourceViewer $Viewer
$source=Join-Path $runtime.Directory 'source.png'
$psd=Join-Path $runtime.Directory 'layer.psd'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/image-tools/orientation-alpha.png') -Destination $source
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/image-tools/two-layers.psd') -Destination $psd
$before=(Get-FileHash -LiteralPath $source).Hash
function Invoke-Probe([string[]]$Arguments,[int]$Expected=0) {
    $start=[Diagnostics.ProcessStartInfo]::new($runtime.Viewer)
    $start.UseShellExecute=$false
    foreach($argument in $Arguments){[void]$start.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::Start($start)
    try {
        if(-not $process.WaitForExit(30000)){$process.Kill();throw '轻工具生产路径测试超时'}
        if($process.ExitCode -ne $Expected){
            if($Arguments[0] -eq '--image-tools-selftest' -and (Test-Path -LiteralPath $Arguments[2])){Write-Host ([IO.File]::ReadAllText($Arguments[2]))}
            throw "轻工具探针退出码错误：$($Arguments[0])，$($process.ExitCode) != $Expected"
        }
    }finally{$process.Dispose()}
}
$result=Join-Path $runtime.Directory 'windows.txt'
Invoke-Probe @('--image-tools-selftest',$psd,$result)
$content=[IO.File]::ReadAllText($result)
if($content -notmatch 'picker=1' -or $content -notmatch 'layers=1'){throw '取色/图层真实子控件自检失败'}
Write-Host 'PASS 取色真实 HEX/RGB 输入控件双向同步、无效输入、实际客户区布局、桌面快照已知色块及 PSD 双图层选择/预览自检。'
$visibility=Join-Path $runtime.Directory 'visibility-stack.psd'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/image-tools/visibility-stack.psd') -Destination $visibility
Invoke-Probe @('--psd-dock-selftest',$visibility,(Join-Path $runtime.Directory 'psd-dock-offscreen'))
Write-Host 'PASS 主窗口PSD停靠组件离屏生产路径：多层开关、透明度、恢复、显示通道及96/192/288 DPI浅深色布局；非真实屏幕截图。'
$assertions=0
foreach($rotation in 0..3){foreach($horizontal in 0..1){foreach($vertical in 0..1){
    $target=Join-Path $runtime.Directory "copy-$rotation-$horizontal-$vertical.png"
    Invoke-Probe @('--export-copy-probe',$source,$target,"$rotation","$horizontal","$vertical")
    $expected=[Drawing.Bitmap]::new($source)
    $actual=[Drawing.Bitmap]::new($target)
    try {
        switch($rotation){1{$expected.RotateFlip([Drawing.RotateFlipType]::Rotate270FlipNone)} 2{$expected.RotateFlip([Drawing.RotateFlipType]::Rotate180FlipNone)} 3{$expected.RotateFlip([Drawing.RotateFlipType]::Rotate90FlipNone)}}
        if($horizontal){$expected.RotateFlip([Drawing.RotateFlipType]::RotateNoneFlipX)}
        if($vertical){$expected.RotateFlip([Drawing.RotateFlipType]::RotateNoneFlipY)}
        if($expected.Width -ne $actual.Width -or $expected.Height -ne $actual.Height){throw '导出尺寸错误'}
        for($y=0;$y -lt $expected.Height;$y++){for($x=0;$x -lt $expected.Width;$x++){
            $a=$actual.GetPixel($x,$y);$e=$expected.GetPixel($x,$y)
            if($a.A -ne $e.A -or ($e.A -ne 0 -and ($a.R -ne $e.R -or $a.G -ne $e.G -or $a.B -ne $e.B))){throw "导出像素方向或透明度错误：$rotation/$horizontal/$vertical/$x/$y"}
        }}
    }finally{$expected.Dispose();$actual.Dispose()}
    $assertions++
}}}
$target=Join-Path $runtime.Directory 'copy-0-0-0.png'
$saved=(Get-FileHash -LiteralPath $target).Hash
Invoke-Probe @('--export-copy-probe',$source,$source,'1','0','0') 2
Invoke-Probe @('--export-copy-probe',$source,$target,'2','1','1') 2
if((Get-FileHash -LiteralPath $source).Hash -ne $before -or (Get-FileHash -LiteralPath $target).Hash -ne $saved){throw '保存副本改写了源文件或已有文件'}
Invoke-Probe @('--export-copy-probe',(Join-Path $runtime.Directory 'missing.png'),(Join-Path $runtime.Directory 'bad.png'),'0','0','0') 2
if(Test-Path -LiteralPath (Join-Path $runtime.Directory 'bad.png')){throw '解码失败却创建了导出文件'}
$svg=Join-Path $runtime.Directory 'vector.svg'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/current image restore/01-small.svg') -Destination $svg
Invoke-Probe @('--export-copy-probe',$svg,(Join-Path $runtime.Directory 'vector-copy.png'),'0','0','0')
Write-Host "PASS $assertions 组生产导出/独立 GDI+ 像素对比，源文件及已有副本保护、解码失败无落盘和 SVG 平面导出。证据：$($runtime.Directory)"
$unitProgram=Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe'
& $unitProgram --corner-close-probe $runtime.Viewer $source
if($LASTEXITCODE -ne 0){throw '真实普通全屏右上角点击关闭回归失败'}
foreach($probe in @('--presentation-scale-probe','--presentation-stale-hover-probe')) {
    $transition=New-IsolatedViewerRuntime -SourceViewer $Viewer
    $transitionImage=Join-Path $transition.Directory 'portrait.svg'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/current image restore/05-tall-capped.svg') -Destination $transitionImage
    & $unitProgram $probe $transition.Viewer $transitionImage
    if($LASTEXITCODE -ne 0){throw "双击/留白退出沉浸保持图片实际大小回归失败：$probe"}
}
