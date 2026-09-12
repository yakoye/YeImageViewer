<#
.SYNOPSIS
    扫描 test/corpus 生成机器可读的 manifest.json。

.DESCRIPTION
    每个测试素材都必须有明确的预期结果，预期不能靠人工记忆。本脚本按目录类别推导
    预期：能正常解码的素材用 ImageMagick 读出真实尺寸并锁定，损坏类素材只要求"不
    崩溃、不卡死"，允许解码失败。

    手工维护的条目（例如已知不支持的格式变体）写在 manifest.overrides.json 里，
    重新生成时会被合并进来，不会丢失。
#>
param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$corpusRoot = Join-Path $repoRoot "test\corpus"
$manifestPath = Join-Path $corpusRoot "manifest.json"
$overridesPath = Join-Path $corpusRoot "manifest.overrides.json"

if (-not (Test-Path -LiteralPath $corpusRoot)) {
    throw "语料目录不存在：$corpusRoot，请先运行 generate-test-corpus.ps1。"
}

# 类别 → 预期模板。分类决定这个素材"算通过"的标准是什么。
$categoryRules = @{
    '00-reference'         = @{ Category = 'reference';          Severity = 'release-blocker'; Decodable = $true }
    # 现代格式变体：除了尺寸，带透明的素材还要求解码后 alpha 通道真的在。
    # 只验尺寸的话，解码器把 alpha 丢掉或整层填成不透明都能「通过」。
    '01-modern-formats'    = @{ Category = 'modern-formats';     Severity = 'release-blocker'; Decodable = $true
                                CheckAlpha = $true }
    # HDR / 专业格式：EXR、Radiance HDR、PFM、TIFF、PSD、ICO 的编码与位深变体
    '02-professional'      = @{ Category = 'professional';       Severity = 'release-blocker'; Decodable = $true
                                CheckAlpha = $true }
    # EXIF 方向组的期望尺寸不能取自 identify：它读的是存储尺寸、不应用方向，
    # orientation 5～8 会读成 400x600。正确应用方向后，八张都应显示为基准的 600x400。
    '12-exif'              = @{ Category = 'exif';               Severity = 'release-blocker'; Decodable = $true
                                FixedWidth = 600; FixedHeight = 400 }
    '13-dimensions'        = @{ Category = 'dimensions';         Severity = 'release-blocker'; Decodable = $true }
    '14-corrupt'           = @{ Category = 'corrupt';            Severity = 'release-blocker'; Decodable = $false }
    '15-extension-mismatch'= @{ Category = 'extension-mismatch'; Severity = 'normal';          Decodable = $true }
    '16-path-filename'     = @{ Category = 'path-filename';      Severity = 'release-blocker'; Decodable = $true }
    '17-large-image'       = @{ Category = 'large-image';        Severity = 'normal';          Decodable = $true }
}

function Get-ImageGeometry {
    param([string]$Path)
    # identify 对损坏文件会失败，这里的失败本身不是错误，交给调用方决定。
    $output = & magick identify -quiet -format "%w %h %n" -- "$Path[0]" 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($output)) { return $null }
    $parts = ($output -split '\s+') | Where-Object { $_ -ne '' }
    if ($parts.Count -lt 2) { return $null }
    return @{
        Width  = [int]$parts[0]
        Height = [int]$parts[1]
        Frames = if ($parts.Count -ge 3) { [int]$parts[2] } else { 1 }
    }
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

# 素材 alpha 通道的最小值，归一化到 0~255；没有 alpha 通道时返回 $null。
# 预期从素材推导，不手写——手写的预期迟早和素材脱节。
#
# 不用 %[opaque]：它只判"是否存在非 255 的 alpha"，对有损编码毫无意义。
# 实测 avif_smoke.avif 本无透明，但 AV1 把恒 255 的 alpha 面压成了最低 254，
# %[opaque] 因此报 false。若据此断言"alpha 必须小于 255"，解码器真把 alpha
# 搞坏了也能靠 254 蒙过去。改看最小值：真有透明的素材最小值贴近 0。
function Get-SourceMinAlpha {
    param([string]$Path)
    $output = & magick identify -quiet -format "%[fx:minima.a]" -- "$Path[0]" 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($output)) { return $null }
    $value = 0.0
    if (-not [double]::TryParse($output.Trim(), [ref]$value)) { return $null }
    # 无 alpha 通道时 minima.a 会给出 2.7e+303 这类垃圾值
    if ($value -lt 0 -or $value -gt 1) { return $null }
    return [int][Math]::Round($value * 255)
}

# 判定「素材确实带大面积透明」的门槛。0.5 的归一化 alpha 已经很低，
# 有损编码的抖动（几个量化级）远达不到这里。
$transparencyThreshold = 128
# 解码后允许的最小 alpha 上限。有损编码会让 0 变成 1~2，留到 16 足够宽松；
# 而解码器丢掉 alpha 会得到 255，离这个上限很远，两种情况不会混淆。
$decodedMinAlphaBound = 16
# 判定「不透明」的下限。有损编码会把恒 255 的 alpha 面压出 254（实测 AVIF），
# 留 250 的余量；而 alpha 被归零会得到 0，离得很远。
$opaqueThreshold = 250

$cases = @()
foreach ($folder in ($categoryRules.Keys | Sort-Object)) {
    $dir = Join-Path $corpusRoot $folder
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    $rule = $categoryRules[$folder]

    $files = Get-ChildItem -LiteralPath $dir -Recurse -File |
        Where-Object { $_.Name -notlike '_*' } | Sort-Object FullName
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($corpusRoot.Length).TrimStart('\', '/') -replace '\\', '/'
        # 扩展名必须留在 id 里：同名不同扩展的素材（empty.jpg / empty.png / …）
        # 去掉扩展名就会撞成同一个 id，报告里再也分不清是哪一条。
        $id = ($relative -replace '[/\\]', '-' -replace '\.', '-').ToLowerInvariant()

        $expected = [ordered]@{}
        if ($rule.Decodable) {
            $expected['open'] = $true
            $expected['crash'] = $false
            $expected['hang'] = $false
            if ($rule.ContainsKey('FixedWidth')) {
                $expected['width'] = $rule.FixedWidth
                $expected['height'] = $rule.FixedHeight
            }
            else {
                $geometry = Get-ImageGeometry $file.FullName
                if ($geometry) {
                    $expected['width'] = $geometry.Width
                    $expected['height'] = $geometry.Height
                }
            }
            if ($rule.ContainsKey('CheckAlpha')) {
                $sourceMinAlpha = Get-SourceMinAlpha $file.FullName
                if ($null -ne $sourceMinAlpha -and $sourceMinAlpha -le $transparencyThreshold) {
                    # 素材确实带大面积透明：解码后必须仍是 4 通道，
                    # 且最小 alpha 仍然贴近全透明，说明通道没被丢掉或填平。
                    $expected['channels'] = 4
                    $expected['maxMinAlpha'] = $decodedMinAlphaBound
                    $expected['sourceMinAlpha'] = $sourceMinAlpha
                }
                elseif ($null -eq $sourceMinAlpha -or $sourceMinAlpha -ge $opaqueThreshold) {
                    # 反过来也要管：不透明的素材必须解出不透明。
                    # 少了这一条就抓不到「整层 alpha 被归零、图变成全透明」这类缺陷——
                    # 那正是 16 位 PSD 目前的表现，图在界面上完全看不见。
                    $expected['minMinAlpha'] = $opaqueThreshold
                    if ($null -ne $sourceMinAlpha) { $expected['sourceMinAlpha'] = $sourceMinAlpha }
                }
            }
        }
        else {
            # 损坏素材：可以解码失败，但绝不允许崩溃、卡死或界面永久无响应。
            $expected['crash'] = $false
            $expected['hang'] = $false
            $expected['allowDecodeFailure'] = $true
        }

        $cases += [ordered]@{
            id        = $id
            path      = $relative
            category  = $rule.Category
            extension = $file.Extension.TrimStart('.').ToLowerInvariant()
            bytes     = $file.Length
            sha256    = Get-Sha256 $file.FullName
            expected  = $expected
            severity  = $rule.Severity
        }
    }
}

# 相机 RAW：素材体积大，只放在本地 test/corpus/_local/03-raw/（已被 .gitignore 挡住）。
# 预期从 raw-expectations.json 读，而不是扫目录——那个文件是提交进仓库的。
# 否则没下过素材的人重建 manifest 时，RAW 用例会凭空消失，报告上还看不出少测了东西。
# 本地缺文件时 runner 会记 SKIPPED，如实显示"素材不存在于本地"。
$rawExpectPath = Join-Path $corpusRoot "raw-expectations.json"
if (Test-Path -LiteralPath $rawExpectPath) {
    $rawDoc = Get-Content -LiteralPath $rawExpectPath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($sample in $rawDoc.samples) {
        $relative = "_local/03-raw/" + $sample.fileName
        $id = ($relative -replace '[/\\]', '-' -replace '\.', '-').ToLowerInvariant()

        $isUnsupported = ($sample.PSObject.Properties['knownUnsupported'] -and $sample.knownUnsupported)
        $expected = [ordered]@{ open = (-not $isUnsupported); crash = $false; hang = $false }
        if ($isUnsupported) {
            # 本机解码器确实打不开的格式：如实记 KNOWN_UNSUPPORTED，不算通过也不算失败。
            # 理由随预期一起进 manifest，不靠人工记忆。
            $expected['knownUnsupported'] = $true
        }
        # 尺寸由 ImageMagick 的 raw 委托独立读出、并已按方向转正（见 fetch-raw-corpus.ps1）。
        # 读不出来的样本不登记尺寸，只要求能解码且不崩，不能凭空编一个数。
        elseif ($sample.width -gt 0 -and $sample.height -gt 0) {
            $expected['width'] = [int]$sample.width
            $expected['height'] = [int]$sample.height
        }

        $comment = ("{0} {1}；来源 raw.pixls.us（CC0），上游文件名 {2}" -f `
                        $sample.make, $sample.model, $sample.upstreamName)
        if ($isUnsupported -and $sample.unsupportedReason) {
            $comment = [string]$sample.unsupportedReason
        }

        $cases += [ordered]@{
            id        = $id
            path      = $relative
            category  = 'raw'
            extension = $sample.extension
            bytes     = [int64]$sample.bytes
            sha256    = ([string]$sample.sha256).ToUpperInvariant()
            expected  = $expected
            # 能解的 RAW 设为阻断级：38 个扩展名是主打功能，Canon CR2 之类突然解不开
            # 必须挡住发布。已知不支持的那几个保持 normal——它们的 FAIL 只意味着
            # 「标记过期、该去掉了」，是登记问题，不该因此拦住版本。
            severity  = if ($isUnsupported) { 'normal' } else { 'release-blocker' }
            comment   = $comment
        }
    }
}

# 合并人工维护的条目：已知不支持、已知限制等只能人工判断，不能被重新生成冲掉。
if (Test-Path -LiteralPath $overridesPath) {
    $overrides = Get-Content -LiteralPath $overridesPath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($override in $overrides.cases) {
        $existing = $cases | Where-Object { $_.id -eq $override.id } | Select-Object -First 1
        if ($existing) {
            foreach ($property in $override.PSObject.Properties) {
                if ($property.Name -eq 'id') { continue }
                $existing[$property.Name] = $property.Value
            }
        }
        else {
            $cases += $override
        }
    }
}

$manifest = [ordered]@{
    version     = 1
    generatedAt = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
    note        = '由 scripts/build-corpus-manifest.ps1 生成；人工条目写在 manifest.overrides.json。'
    suites      = [ordered]@{
        core    = @('reference', 'dimensions', 'extension-mismatch', 'path-filename', 'exif')
        modern  = @('modern-formats')
        pro     = @('professional')
        raw     = @('raw')
        corrupt = @('corrupt')
        large   = @('large-image')
    }
    cases       = $cases
}

$json = $manifest | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($manifestPath, $json, (New-Object Text.UTF8Encoding $false))

$byCategory = $cases | Group-Object { $_.category } | Sort-Object Name
Write-Host "已写入 $manifestPath"
Write-Host "用例总数: $($cases.Count)"
foreach ($group in $byCategory) {
    Write-Host ("  {0,-20} {1}" -f $group.Name, $group.Count)
}
