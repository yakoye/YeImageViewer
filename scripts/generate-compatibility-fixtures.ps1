$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot '../test/compatibility'
$inputFile=Join-Path $root 'palette.ppm'
foreach($format in @('psd','psb')) {
    foreach($mode in @('sRGB','CMYK','Gray','Lab')) {
        foreach($depth in @(8,16)) {
            foreach($compression in @('None','RLE','Zip')) {
                $name="$format-$mode-$depth-$compression"
                $file=Join-Path $root ($name+".$format")
                & magick $inputFile -colorspace $mode -depth $depth -compress $compression $file
                if($LASTEXITCODE -ne 0) {throw "生成失败：$name"}
                & magick ($file+'[0]') -colorspace sRGB -depth 8 (Join-Path $root ($name+'.png'))
                if($LASTEXITCODE -ne 0) {throw "参考图失败：$name"}
            }
        }
    }
}

$alphaRoot=Join-Path $root 'alpha'
[void](New-Item -ItemType Directory -Path $alphaRoot -Force)
foreach($format in @('psd','psb')) {foreach($depth in @(8,16)) {foreach($compression in @('None','RLE','Zip')) {
    $name="$format-alpha-$depth-$compression"
    $file=Join-Path $alphaRoot ($name+".$format")
    & magick $inputFile -alpha set -channel A -evaluate set 50% +channel -depth $depth -compress $compression $file
    if($LASTEXITCODE -ne 0) {throw "透明夹具生成失败：$name"}
    & magick ($file+'[0]') -depth 8 (Join-Path $alphaRoot ($name+'.png'))
    if($LASTEXITCODE -ne 0) {throw "透明参考失败：$name"}
}}}

foreach($depth in @(8,16)) {
    foreach($compression in @('None','LZW','Zip')) {
        $name="cmyk-$depth-$compression"
        $file=Join-Path $root ($name+'.tif')
        & magick $inputFile -colorspace CMYK -depth $depth -compress $compression $file
        if($LASTEXITCODE -ne 0) {throw "TIFF 生成失败：$name"}
        & magick ($file+'[0]') -colorspace sRGB -depth 8 (Join-Path $root ($name+'.png'))
    }
}
& magick (Join-Path $root 'cmyk-8-Zip.tif') (Join-Path $root 'cmyk-16-LZW.tif') (Join-Path $root 'cmyk-multipage.tif')
if($LASTEXITCODE -ne 0) {throw '多页 CMYK TIFF 生成失败'}

# 最小 TIFF 复现：可读像素只有 4×2，附一个 128 KiB 的 Photoshop 二进制标签。
# 原图有几十 MB 图层数据，不复制用户作品或 EXIF；此夹具只验证不能逐字节展开大值。
$memory=[IO.MemoryStream]::new()
$writer=[IO.BinaryWriter]::new($memory)
try {
    $entryCount=11
    $bitsOffset=8+2+12*$entryCount+4
    $pixelOffset=$bitsOffset+6
    $blobOffset=$pixelOffset+24
    $writer.Write([byte[]](73,73));$writer.Write([uint16]42);$writer.Write([uint32]8)
    $writer.Write([uint16]$entryCount)
    foreach($entry in @(@(256,4,1,4),@(257,4,1,2),@(258,3,3,$bitsOffset),@(259,3,1,1),@(262,3,1,2),
        @(273,4,1,$pixelOffset),@(277,3,1,3),@(278,4,1,2),@(279,4,1,24),@(284,3,1,1),@(37724,7,131072,$blobOffset))) {
        $writer.Write([uint16]$entry[0]);$writer.Write([uint16]$entry[1]);$writer.Write([uint32]$entry[2]);$writer.Write([uint32]$entry[3])
    }
    $writer.Write([uint32]0)
    $writer.Write([uint16]8);$writer.Write([uint16]8);$writer.Write([uint16]8)
    $writer.Write([byte[]](255,0,0,0,255,0,0,0,255,255,255,255,0,0,0,0,255,255,255,0,255,255,255,0))
    $writer.Write([byte[]]::new(131072))
    [IO.File]::WriteAllBytes((Join-Path $root 'metadata-large.tif'),$memory.ToArray())
} finally {$writer.Dispose();$memory.Dispose()}
