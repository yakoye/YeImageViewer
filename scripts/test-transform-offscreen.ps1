param([string]$Viewer=(Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'New-RegressionRuntime.ps1')
$runtime=New-IsolatedViewerRuntime -SourceViewer $Viewer
Add-Type -AssemblyName System.Drawing
function Invoke-Headless([string[]]$Arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new($runtime.Viewer);$start.UseShellExecute=$false
    foreach($arg in $Arguments){[void]$start.ArgumentList.Add($arg)}
    $process=[Diagnostics.Process]::Start($start)
    try {if(-not $process.WaitForExit(90000)){$process.Kill();throw '生产离屏测试超时'}
        if($process.ExitCode -ne 0){throw "离屏测试失败：$($Arguments -join ' ')；证据：$($runtime.Directory)"}
    }finally{$process.Dispose()}
}
foreach($portrait in @($false,$true)) {
    $w=if($portrait){320}else{513};$h=if($portrait){513}else{320}
    $image=[Drawing.Bitmap]::new($w,$h)
    try {
        for($y=0;$y -lt $h;$y++){for($x=0;$x -lt $w;$x++){$image.SetPixel($x,$y,[Drawing.Color]::FromArgb(255,($x*3)%256,($y*5)%256,($x+$y)%256))}}
        foreach($ext in @('png','jpg')){
            $file=Join-Path $runtime.Directory ("pattern-$portrait.$ext")
            $format=if($ext -eq 'png'){[Drawing.Imaging.ImageFormat]::Png}else{[Drawing.Imaging.ImageFormat]::Jpeg}
            $image.Save($file,$format);$before=(Get-FileHash -LiteralPath $file).Hash
            $output=Join-Path $runtime.Directory ("render-$portrait-$ext")
            Invoke-Headless @('--transform-render-selftest',$file,$output)
            if((Get-FileHash -LiteralPath $file).Hash -ne $before){throw '只读渲染测试改写了输入素材'}
            Write-Host "PASS 48组旋转/双镜像/17%-50%-100%生产绘制：$portrait/$ext"
        }
    }finally{$image.Dispose()}
}
Invoke-Headless @('--original-save-selftest',(Join-Path $runtime.Directory 'original-save'))
Invoke-Headless @('--color-panel-offscreen-selftest',(Join-Path $runtime.Directory 'color-panel.txt'))
Write-Host "PASS 原图保存生产编码/元数据/备份回读专项。全部离屏，不代表窗口交互通过。证据：$($runtime.Directory)"
