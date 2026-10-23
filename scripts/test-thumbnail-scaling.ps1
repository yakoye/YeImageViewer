param(
    [string]$Tests = (Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe'),
    [string]$Provider = (Join-Path $PSScriptRoot '../x64/Release/YeThumbnailProvider.dll')
)
$ErrorActionPreference = 'Stop'
$fixtures = Join-Path $PSScriptRoot '../test/thumbnail-scaling'
foreach ($case in @(
    @('large-rgb.jpg', 'large-rgb-256.png', 256, 128),
    @('large-cmyk.jpg', 'large-cmyk-256.png', 256, 128),
    @('cmyk-icc.jpg', 'cmyk-icc-256.png', 256, 128),
    @('odd-cmyk.jpg', 'odd-cmyk-256.png', 256, 170),
    @('tall-alpha.png', 'tall-alpha-256.png', 1, 256),
    @('small.png', 'small.png', 5, 3),
    @('cmyk-16.tif', 'cmyk-16-256.png', 256, 128),
    @('cmyk-checker.tif', 'cmyk-checker-256.png', 256, 256)
)) {
    $name = $case[0]
    $image = Join-Path $fixtures $name
    $reference = Join-Path $fixtures $case[1]
    # 通过独立进程峰值提交量约束，不用容易受机器负载影响的耗时阈值。
    $output = & $Tests --thumbnail-memory-probe $Provider $image $reference
    if ($LASTEXITCODE -ne 0) { throw "缩略图像素回归失败：$name，$output" }
    $result = $output | ConvertFrom-Json
    if (-not $result.success -or $result.width -ne $case[2] -or $result.height -ne $case[3]) {
        throw "缩略图尺寸或有效性错误：$name，$output"
    }
    if ($result.peakCommitDelta -gt 64MB) { throw "缩略图仍在整图分配：$name，$output" }
    Write-Host "PASS 按目标尺寸解码 $name $output"
}
