$ErrorActionPreference = 'Stop'
$destination = Join-Path $PSScriptRoot '../test/thumbnail-scaling'
[void](New-Item -ItemType Directory -Path $destination -Force)
# 无用户内容的纯色四象限，8192×4096，仅压缩后几百 KiB。
$rgb = Join-Path $destination 'large-rgb.jpg'
& magick -size 8192x4096 'xc:#e02010' -fill '#10c030' -draw 'rectangle 4096,0 8191,2047' -fill '#2040e0' -draw 'rectangle 0,2048 4095,4095' -fill '#e0c020' -draw 'rectangle 4096,2048 8191,4095' -sampling-factor 1x1 -quality 95 $rgb
if ($LASTEXITCODE -ne 0) { throw 'RGB 夹具生成失败' }
& magick $rgb -colorspace CMYK -sampling-factor 1x1 -quality 95 (Join-Path $destination 'large-cmyk.jpg')
if ($LASTEXITCODE -ne 0) { throw 'CMYK 夹具生成失败' }
& magick -size 3543x2362 'xc:#4080c0' -colorspace CMYK -sampling-factor 1x1 -quality 95 (Join-Path $destination 'odd-cmyk.jpg')
if ($LASTEXITCODE -ne 0) { throw '非整除尺寸 CMYK 生成失败' }
& magick (Join-Path $destination 'odd-cmyk.jpg') -colorspace sRGB -filter box -resize 256x170! (Join-Path $destination 'odd-cmyk-256.png')
if ($LASTEXITCODE -ne 0) { throw '非整除尺寸参考生成失败' }
foreach ($name in @('large-rgb', 'large-cmyk')) {
    & magick (Join-Path $destination ($name + '.jpg')) -colorspace sRGB -filter box -resize 256x128! (Join-Path $destination ($name + '-256.png'))
    if ($LASTEXITCODE -ne 0) { throw "参考图生成失败：$name" }
}

$profile = Join-Path $destination 'synthetic-cmyk.icc'
$rgbProfile = Join-Path $destination 'synthetic-srgb.icc'
& (Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe') --generate-thumbnail-profile $profile $rgbProfile
if ($LASTEXITCODE -ne 0) { throw '合成 ICC 生成失败' }
& magick (Join-Path $destination 'large-cmyk.jpg') -profile $profile -sampling-factor 1x1 -quality 95 (Join-Path $destination 'cmyk-icc.jpg')
if ($LASTEXITCODE -ne 0) { throw '带 ICC 的 CMYK JPEG 生成失败' }
& magick (Join-Path $destination 'cmyk-icc.jpg') -profile $rgbProfile -strip -depth 8 -filter box -resize 256x128! (Join-Path $destination 'cmyk-icc-256.png')
if ($LASTEXITCODE -ne 0) { throw 'ICC 参考图生成失败' }

& magick -size 8x4096 'xc:#4080c080' (Join-Path $destination 'tall-alpha.png')
if ($LASTEXITCODE -ne 0) { throw '透明长图生成失败' }
& magick (Join-Path $destination 'tall-alpha.png') -filter box -resize 1x256! (Join-Path $destination 'tall-alpha-256.png')
if ($LASTEXITCODE -ne 0) { throw '透明长图参考生成失败' }
& magick -size 5x3 'xc:#4080c0' (Join-Path $destination 'small.png')
if ($LASTEXITCODE -ne 0) { throw '小图生成失败' }
# 16 位 CMYK TIFF 无原生缩减接口，验证正确颜色的完整解码兜底。
& magick $rgb -resize 2048x1024! -colorspace CMYK -depth 16 -compress Zip (Join-Path $destination 'cmyk-16.tif')
if ($LASTEXITCODE -ne 0) { throw 'CMYK TIFF 生成失败' }
& magick (Join-Path $destination 'cmyk-16.tif') -colorspace sRGB -depth 8 -filter box -resize 256x128! (Join-Path $destination 'cmyk-16-256.png')
if ($LASTEXITCODE -ne 0) { throw 'CMYK TIFF 参考生成失败' }
& magick -size 1024x1024 pattern:gray50 -colorspace CMYK -depth 8 -compress Zip (Join-Path $destination 'cmyk-checker.tif')
if ($LASTEXITCODE -ne 0) { throw 'CMYK 密纹 TIFF 生成失败' }
& magick (Join-Path $destination 'cmyk-checker.tif') -colorspace sRGB -depth 8 -filter box -resize 256x256! (Join-Path $destination 'cmyk-checker-256.png')
if ($LASTEXITCODE -ne 0) { throw 'CMYK 密纹参考生成失败' }
