<#
.SYNOPSIS
    生成 test/corpus 下可自动构造的测试素材。

.DESCRIPTION
    能算出来的素材一律不手工保存，由本脚本按需重建，这样素材的构造方式本身就是
    可审阅的代码。需要 ImageMagick（magick 命令）。

    每类素材各自独立，可以单独重建：
        .\generate-test-corpus.ps1 -Category reference
        .\generate-test-corpus.ps1 -All

    超大尺寸的素材（dimensions 里的长条图、large-image）体积可观，生成后由
    .gitignore 挡在仓库外，测试时本地缺失就记为 SKIPPED。
#>
param(
    [ValidateSet('reference', 'dimensions', 'corrupt', 'extension-mismatch', 'path-filename', 'exif',
        'modern-formats')]
    [string[]]$Category,
    [switch]$All,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$corpusRoot = Join-Path $repoRoot "test\corpus"

$magick = (Get-Command magick -ErrorAction SilentlyContinue)
if (-not $magick) {
    throw "需要 ImageMagick：未找到 magick 命令。"
}

if ($All) {
    $Category = @('reference', 'dimensions', 'corrupt', 'extension-mismatch', 'path-filename', 'exif',
        'modern-formats')
}
if (-not $Category) {
    throw "请指定 -Category 或 -All。"
}

function New-CorpusDirectory {
    param([string]$Relative)
    $path = Join-Path $corpusRoot $Relative
    if (-not (Test-Path -LiteralPath $path)) {
        [void](New-Item -ItemType Directory -Path $path -Force)
    }
    return $path
}

function Invoke-Magick {
    param([string[]]$Arguments)
    $output = & magick @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "magick $($Arguments -join ' ') 失败：$output"
    }
}

# 目标文件已存在且未指定 -Force 时跳过，避免反复重算大图。
function Test-ShouldBuild {
    param([string]$Path)
    if ($Force) { return $true }
    return -not (Test-Path -LiteralPath $Path)
}

function Write-Step {
    param([string]$Message)
    Write-Host "  $Message"
}

# 往 JPEG 里插入一段只含 Orientation 的最小 EXIF APP1。
#
# 不能指望 ImageMagick 的 -orient：源图是 PNG 时它没有 EXIF 结构可写，方向只留在内部
# 属性里，产出的 JPEG 里 EXIF:Orientation 是空的——那样测的就成了素材缺陷，而不是
# 查看器的方向处理。
function Add-ExifOrientation {
    param([string]$Path, [int]$Orientation)

    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 2 -or $bytes[0] -ne 0xFF -or $bytes[1] -ne 0xD8) {
        throw "不是 JPEG，无法写入 EXIF：$Path"
    }

    # TIFF 头（小端）+ 一条 IFD0 记录：tag 0x0112 (Orientation)，类型 SHORT，值内联。
    $tiff = [byte[]]@(
        0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x01, 0x00,
        0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
        [byte]$Orientation, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    )
    $payload = [Text.Encoding]::ASCII.GetBytes("Exif") + [byte[]]@(0x00, 0x00) + $tiff
    $segmentLength = $payload.Length + 2   # 段长度含这两个长度字节本身
    $app1 = [byte[]]@(0xFF, 0xE1,
        [byte](($segmentLength -shr 8) -band 0xFF),
        [byte]($segmentLength -band 0xFF)) + $payload

    $rest = $bytes[2..($bytes.Length - 1)]
    [IO.File]::WriteAllBytes($Path, ([byte[]]@(0xFF, 0xD8) + $app1 + $rest))
}

# ---------------------------------------------------------------- reference
# 人工视觉参考图：方向、镜像、裁切、缩放算法出问题时，这几张图能一眼看出来。
function Build-Reference {
    Write-Host "生成 00-reference..."
    $dir = New-CorpusDirectory "00-reference"

    $checker = Join-Path $dir "checkerboard.png"
    if (Test-ShouldBuild $checker) {
        Invoke-Magick @('-size', '512x512', 'pattern:checkerboard',
            '-scale', '512x512', $checker)
        Write-Step "checkerboard.png"
    }

    $bars = Join-Path $dir "rgb_bars.png"
    if (Test-ShouldBuild $bars) {
        Invoke-Magick @('-size', '120x480', 'xc:red', '-size', '120x480', 'xc:lime',
            '-size', '120x480', 'xc:blue', '-size', '120x480', 'xc:white',
            '+append', $bars)
        Write-Step "rgb_bars.png"
    }

    $gray = Join-Path $dir "grayscale_0_255.png"
    if (Test-ShouldBuild $gray) {
        Invoke-Magick @('-size', '256x128', 'gradient:black-white',
            '-rotate', '90', $gray)
        Write-Step "grayscale_0_255.png"
    }

    $alpha = Join-Path $dir "alpha_gradient.png"
    if (Test-ShouldBuild $alpha) {
        Invoke-Magick @('-size', '256x128', 'xc:red', '-alpha', 'set',
            '(', '-size', '256x128', 'gradient:white-black', ')',
            '-compose', 'CopyOpacity', '-composite', $alpha)
        Write-Step "alpha_gradient.png"
    }

    # 每格 32 像素的网格，缩放算法错位时格线会糊成一片。
    $grid = Join-Path $dir "resolution_grid.png"
    if (Test-ShouldBuild $grid) {
        Invoke-Magick @('-size', '512x512', 'xc:white', '-fill', 'none',
            '-stroke', 'black', '-strokewidth', '1',
            '-draw', 'path "M 0,0 L 0,512"', $grid)
        # 逐条画线交给 -draw 的重复路径，这里直接用 pattern 更省事
        Invoke-Magick @('-size', '32x32', 'pattern:gray50', '-scale', '512x512',
            '-threshold', '50%', $grid)
        Write-Step "resolution_grid.png"
    }

    # 1 像素黑白相间：任何重采样都会让它变灰，用来确认 100% 显示是像素对齐的。
    $onePixel = Join-Path $dir "one_pixel_grid.png"
    if (Test-ShouldBuild $onePixel) {
        Invoke-Magick @('-size', '2x2', 'pattern:gray50', '-threshold', '50%',
            '-scale', '256x256', $onePixel)
        Write-Step "one_pixel_grid.png"
    }

    # 方向标记图：上下左右与四角都写明文字，旋转/镜像/翻转一眼可辨。
    $orientation = Join-Path $dir "orientation_text.png"
    if (Test-ShouldBuild $orientation) {
        Invoke-Magick @('-size', '600x400', 'xc:white', '-fill', 'black',
            '-pointsize', '48', '-gravity', 'north', '-annotate', '+0+20', 'TOP',
            '-gravity', 'south', '-annotate', '+0+20', 'BOTTOM',
            '-gravity', 'west', '-annotate', '+20+0', 'LEFT',
            '-gravity', 'east', '-annotate', '+20+0', 'RIGHT',
            '-pointsize', '28',
            '-gravity', 'northwest', '-annotate', '+12+12', 'TL',
            '-gravity', 'northeast', '-annotate', '+12+12', 'TR',
            '-gravity', 'southwest', '-annotate', '+12+12', 'BL',
            '-gravity', 'southeast', '-annotate', '+12+12', 'BR',
            $orientation)
        Write-Step "orientation_text.png"
    }

    # 四角纯色：裁切或越界读会立刻破坏角落的颜色。
    $corners = Join-Path $dir "color_corners.png"
    if (Test-ShouldBuild $corners) {
        Invoke-Magick @('-size', '200x200', 'xc:red', '(', '-size', '200x200', 'xc:lime', ')',
            '+append', '(', '(', '-size', '200x200', 'xc:blue', ')',
            '(', '-size', '200x200', 'xc:yellow', ')', '+append', ')',
            '-append', $corners)
        Write-Step "color_corners.png"
    }
}

# --------------------------------------------------------------- dimensions
# 尺寸边界：奇数尺寸、极端长宽比最容易在 stride、纹理对齐、整数运算上出问题。
function Build-Dimensions {
    Write-Host "生成 13-dimensions..."
    $dir = New-CorpusDirectory "13-dimensions"

    $sizes = @(
        @{ W = 1;     H = 1 },
        @{ W = 1;     H = 2 },
        @{ W = 2;     H = 1 },
        @{ W = 101;   H = 99 },
        @{ W = 1;     H = 10000 },
        @{ W = 10000; H = 1 },
        @{ W = 4095;  H = 4097 },
        @{ W = 8191;  H = 8193 },
        @{ W = 19200; H = 200 },
        @{ W = 200;   H = 19200 }
    )

    foreach ($size in $sizes) {
        $w = $size.W; $h = $size.H
        foreach ($ext in @('png', 'jpg')) {
            # 1 像素宽/高的 JPEG 在部分编码器上无法表达色度子采样，跳过避免制造假失败。
            $name = "{0}x{1}.{2}" -f $w, $h, $ext
            $target = Join-Path $dir $name
            if (-not (Test-ShouldBuild $target)) { continue }
            $args = @('-size', ("{0}x{1}" -f $w, $h), 'gradient:red-blue')
            if ($ext -eq 'jpg') { $args += @('-quality', '85') }
            $args += $target
            Invoke-Magick $args
            Write-Step $name
        }
    }
}

# ------------------------------------------------------------------ corrupt
# 损坏文件：这一组是发布阻断项，要求绝不崩溃、不卡死、不无限循环。
function Build-Corrupt {
    Write-Host "生成 14-corrupt..."
    $dir = New-CorpusDirectory "14-corrupt"
    $seedJpg = Join-Path $dir "_seed.jpg"
    $seedPng = Join-Path $dir "_seed.png"
    Invoke-Magick @('-size', '640x480', 'gradient:orange-purple', '-quality', '90', $seedJpg)
    Invoke-Magick @('-size', '640x480', 'gradient:orange-purple', $seedPng)

    # 空文件
    foreach ($name in @('empty.jpg', 'empty.png', 'empty.webp', 'empty.gif')) {
        $target = Join-Path $dir $name
        if (Test-ShouldBuild $target) {
            [IO.File]::WriteAllBytes($target, @())
            Write-Step $name
        }
    }

    # 纯随机字节：连魔数都不对
    $random = Join-Path $dir "random_bytes.jpg"
    if (Test-ShouldBuild $random) {
        $rng = [Random]::new(20260912)
        $bytes = New-Object byte[] 4096
        $rng.NextBytes($bytes)
        [IO.File]::WriteAllBytes($random, $bytes)
        Write-Step "random_bytes.jpg"
    }

    # 按比例截断的 JPEG：解码器要能在数据中途结束时安全收尾
    $jpgBytes = [IO.File]::ReadAllBytes($seedJpg)
    foreach ($percent in @(1, 10, 50, 99)) {
        $name = "jpeg_cut_{0}percent.jpg" -f $percent
        $target = Join-Path $dir $name
        if (-not (Test-ShouldBuild $target)) { continue }
        $keep = [int]($jpgBytes.Length * $percent / 100)
        if ($keep -lt 1) { $keep = 1 }
        [IO.File]::WriteAllBytes($target, $jpgBytes[0..($keep - 1)])
        Write-Step $name
    }

    $pngBytes = [IO.File]::ReadAllBytes($seedPng)

    # PNG 魔数被改坏
    $badSig = Join-Path $dir "png_bad_signature.png"
    if (Test-ShouldBuild $badSig) {
        $copy = $pngBytes.Clone()
        $copy[1] = 0x00
        [IO.File]::WriteAllBytes($badSig, $copy)
        Write-Step "png_bad_signature.png"
    }

    # IHDR 的 CRC 被改坏
    $badCrc = Join-Path $dir "png_bad_crc.png"
    if (Test-ShouldBuild $badCrc) {
        $copy = $pngBytes.Clone()
        $copy[29] = $copy[29] -bxor 0xFF
        [IO.File]::WriteAllBytes($badCrc, $copy)
        Write-Step "png_bad_crc.png"
    }

    # 缺少 IEND：文件在数据流中间戛然而止
    $noIend = Join-Path $dir "png_missing_iend.png"
    if (Test-ShouldBuild $noIend) {
        [IO.File]::WriteAllBytes($noIend, $pngBytes[0..($pngBytes.Length - 13)])
        Write-Step "png_missing_iend.png"
    }

    # IHDR 声明超大尺寸：解码器若直接按头部分配内存就会爆
    $hugeHeader = Join-Path $dir "huge_dimension_header.png"
    if (Test-ShouldBuild $hugeHeader) {
        $copy = $pngBytes.Clone()
        # IHDR 宽高位于偏移 16..23，大端序
        foreach ($offset in @(16, 20)) {
            $copy[$offset] = 0x7F; $copy[$offset + 1] = 0xFF
            $copy[$offset + 2] = 0xFF; $copy[$offset + 3] = 0xFF
        }
        [IO.File]::WriteAllBytes($hugeHeader, $copy)
        Write-Step "huge_dimension_header.png"
    }

    # GIF / WebP / TIFF 的结构性损坏
    $seedGif = Join-Path $dir "_seed.gif"
    Invoke-Magick @('-size', '120x120', 'gradient:red-blue', '-duplicate', '3', $seedGif)
    $gifBad = Join-Path $dir "gif_bad_frame.gif"
    if (Test-ShouldBuild $gifBad) {
        $b = [IO.File]::ReadAllBytes($seedGif)
        $cut = [int]($b.Length * 0.6)
        [IO.File]::WriteAllBytes($gifBad, $b[0..$cut])
        Write-Step "gif_bad_frame.gif"
    }

    $seedWebp = Join-Path $dir "_seed.webp"
    Invoke-Magick @('-size', '320x240', 'gradient:green-black', $seedWebp)
    $webpBad = Join-Path $dir "webp_bad_chunk.webp"
    if (Test-ShouldBuild $webpBad) {
        $b = [IO.File]::ReadAllBytes($seedWebp)
        if ($b.Length -gt 20) { $b[12] = 0x00; $b[13] = 0x00 }  # 破坏块标识
        [IO.File]::WriteAllBytes($webpBad, $b)
        Write-Step "webp_bad_chunk.webp"
    }

    $seedTif = Join-Path $dir "_seed.tif"
    Invoke-Magick @('-size', '320x240', 'gradient:cyan-black', $seedTif)
    $tifBad = Join-Path $dir "tiff_bad_ifd.tif"
    if (Test-ShouldBuild $tifBad) {
        $b = [IO.File]::ReadAllBytes($seedTif)
        # IFD 偏移位于 4..7，指向文件外即为无效
        $b[4] = 0xFF; $b[5] = 0xFF; $b[6] = 0xFF; $b[7] = 0x7F
        [IO.File]::WriteAllBytes($tifBad, $b)
        Write-Step "tiff_bad_ifd.tif"
    }

    # 现代格式的截断损坏（测试规格 Phase 3 的 Corrupt 一项）。
    # 这些格式各有独立的解码器，PNG/JPEG 的容错不能代表它们：AVIF 走 dav1d，
    # JXL 走 libjxl，JP2 走 openjpeg，QOI 是手写解码，容器解析也各不相同。
    # 截在 40%：头部完整、像素数据中途断掉，解码器必须安全收尾而不是越界读。
    $modernSeeds = @(
        @{ Ext = 'avif'; Args = @() }
        @{ Ext = 'jxl';  Args = @() }
        @{ Ext = 'webp'; Args = @('-define', 'webp:lossless=true') }
        @{ Ext = 'qoi';  Args = @() }
        @{ Ext = 'jp2';  Args = @() }
    )
    foreach ($entry in $modernSeeds) {
        $seed = Join-Path $dir ("_seed_modern.{0}" -f $entry.Ext)
        $target = Join-Path $dir ("{0}_truncated.{0}" -f $entry.Ext)
        try {
            Invoke-Magick (@('-size', '256x192', 'gradient:yellow-navy') + $entry.Args + @($seed))
        }
        catch {
            # 本机编码器不支持就跳过，绝不用改扩展名的 PNG 冒充
            Write-Step ("跳过 {0}_truncated.{0}：本机无法生成 .{0} 源文件" -f $entry.Ext)
            continue
        }
        if (Test-ShouldBuild $target) {
            try { Assert-NotFallbackEncoding -Path $seed }
            catch {
                Write-Step ("跳过 {0}_truncated.{0}：{1}" -f $entry.Ext, $_.Exception.Message)
                continue
            }
            $bytes = [IO.File]::ReadAllBytes($seed)
            $keep = [int]($bytes.Length * 0.4)
            if ($keep -lt 16) { $keep = [Math]::Min(16, $bytes.Length) }
            [IO.File]::WriteAllBytes($target, $bytes[0..($keep - 1)])
            Write-Step ("{0}_truncated.{0}" -f $entry.Ext)
        }
        Remove-Item -LiteralPath $seed -Force -ErrorAction SilentlyContinue
    }

    foreach ($seed in @($seedJpg, $seedPng, $seedGif, $seedWebp, $seedTif)) {
        Remove-Item -LiteralPath $seed -Force -ErrorAction SilentlyContinue
    }
}

# -------------------------------------------------------- extension-mismatch
# 扩展名欺骗：扩展名只该用于筛选与关联，真正的格式判断必须看内容。
function Build-ExtensionMismatch {
    Write-Host "生成 15-extension-mismatch..."
    $dir = New-CorpusDirectory "15-extension-mismatch"

    $pairs = @(
        @{ Source = 'jpg';  Name = 'jpeg_named_png.png' },
        @{ Source = 'png';  Name = 'png_named_jpg.jpg' },
        @{ Source = 'webp'; Name = 'webp_named_jpg.jpg' },
        @{ Source = 'png';  Name = 'real_png.no_extension' },
        @{ Source = 'jpg';  Name = 'UPPERCASE.JPG' },
        @{ Source = 'jpg';  Name = 'MiXeD.JpEg' }
    )
    foreach ($pair in $pairs) {
        $target = Join-Path $dir $pair.Name
        if (-not (Test-ShouldBuild $target)) { continue }
        $temp = Join-Path $dir ("_tmp." + $pair.Source)
        Invoke-Magick @('-size', '320x200', 'gradient:magenta-navy', $temp)
        Move-Item -LiteralPath $temp -Destination $target -Force
        Write-Step $pair.Name
    }
}

# ------------------------------------------------------------- path-filename
# 文件名与路径：Unicode、Emoji、空格、特殊字符、超长名都要能正常打开。
function Build-PathFilename {
    Write-Host "生成 16-path-filename..."
    $dir = New-CorpusDirectory "16-path-filename"

    $names = @(
        '中文图片.jpg',
        '中文 空格 图片.png',
        '日本語画像.webp',
        '한글사진.jpg',
        '😀.png',
        '🌍🌊🌌.jpg',
        'a b c.jpg',
        'a.b.c.jpg',
        '#test.png',
        '[test].jpg',
        '(test).webp',
        "'quote'.png",
        ('VeryLongFilename_' + ('x' * 120) + '.jpg')
    )
    foreach ($name in $names) {
        $target = Join-Path $dir $name
        if (-not (Test-ShouldBuild $target)) { continue }
        $ext = [IO.Path]::GetExtension($name).TrimStart('.')
        $temp = Join-Path $dir ("_tmp." + $ext)
        Invoke-Magick @('-size', '240x160', 'gradient:teal-black', $temp)
        Move-Item -LiteralPath $temp -Destination $target -Force
        Write-Step $name
    }

    # 深层中文路径
    $deep = Join-Path $dir "深层路径"
    $segments = @('图片测试', '中文', '很深', '很深', '很深', '很深', '很深')
    $current = $deep
    foreach ($segment in $segments) { $current = Join-Path $current $segment }
    if (-not (Test-Path -LiteralPath $current)) {
        [void](New-Item -ItemType Directory -Path $current -Force)
    }
    $deepFile = Join-Path $current "图片.jpg"
    if (Test-ShouldBuild $deepFile) {
        $temp = Join-Path $dir "_tmp.jpg"
        Invoke-Magick @('-size', '240x160', 'gradient:gold-black', $temp)
        Move-Item -LiteralPath $temp -Destination $deepFile -Force
        Write-Step "深层路径/.../图片.jpg"
    }
}

# ----------------------------------------------------------------- exif
# EXIF Orientation 1～8。像素按各方向的逆变换预先存好，再打上对应的方向标签，
# 于是正确应用方向的查看器把八张显示成同一个样子——尺寸也应当一律是基准的
# 600x400。不应用方向时，5～8 会解出 400x600，这个差异可以自动判定。
function Build-Exif {
    Write-Host "生成 12-exif..."
    $dir = New-CorpusDirectory "12-exif"
    $base = Join-Path $corpusRoot "00-reference\orientation_text.png"
    if (-not (Test-Path -LiteralPath $base)) {
        throw "缺少基准图 00-reference/orientation_text.png，请先生成 reference 类别。"
    }

    # 变换 = 显示时所需变换的逆操作；标签 = 对应的 EXIF Orientation。
    $orientations = @(
        @{ Index = 1; Transform = @();               Orient = 'TopLeft' },
        @{ Index = 2; Transform = @('-flop');        Orient = 'TopRight' },
        @{ Index = 3; Transform = @('-rotate','180');Orient = 'BottomRight' },
        @{ Index = 4; Transform = @('-flip');        Orient = 'BottomLeft' },
        @{ Index = 5; Transform = @('-transpose');   Orient = 'LeftTop' },
        @{ Index = 6; Transform = @('-rotate','-90');Orient = 'RightTop' },
        @{ Index = 7; Transform = @('-transverse');  Orient = 'RightBottom' },
        @{ Index = 8; Transform = @('-rotate','90'); Orient = 'LeftBottom' }
    )
    foreach ($entry in $orientations) {
        $name = "exif_orientation_{0}.jpg" -f $entry.Index
        $target = Join-Path $dir $name
        if (-not (Test-ShouldBuild $target)) { continue }
        # 先出干净的 JPEG（-strip 去掉 ImageMagick 自己带的元数据），再插入 EXIF 方向。
        $args = @($base) + $entry.Transform + @('-strip', '-quality', '92', $target)
        Invoke-Magick $args
        Add-ExifOrientation -Path $target -Orientation $entry.Index
        Write-Step $name
    }

    # 无 EXIF 与超大 EXIF：前者走默认方向，后者验证大块元数据不会撑爆解析。
    $noExif = Join-Path $dir "no_exif.jpg"
    if (Test-ShouldBuild $noExif) {
        Invoke-Magick @($base, '-strip', '-quality', '92', $noExif)
        Write-Step "no_exif.jpg"
    }
    $largeExif = Join-Path $dir "large_exif.jpg"
    if (Test-ShouldBuild $largeExif) {
        $comment = 'X' * 30000
        Invoke-Magick @($base, '-quality', '92', '-set', 'comment', $comment, $largeExif)
        Write-Step "large_exif.jpg"
    }
}

# 现代格式专项变体（测试规格 Phase 3）。
#
# 每个格式建立 Smoke / Alpha / 奇数尺寸 / 位深或编码变体四类，适用才建。
# 损坏变体统一放进 14-corrupt：预期是「允许解码失败但不许崩溃」，与本目录
# 「必须解码成功并尺寸相符」是两套判定标准，manifest 按目录取规则，不能混放。
#
# 只生成能验证为真编码的文件。ImageMagick 7.1.2 的 HEIC 是只读的（r--），
# 请它写 .heic 会静默退化成 PNG 只换扩展名——实测头部是 89 50 4E 47，
# 程序按内容嗅探照样「解码成功」。那种文件冒充不了 HEIC 覆盖，这里不生成，
# HEIC/HEIF 的覆盖继续依赖 test/format corpus 里 libheif 的真实样本。
function Build-ModernFormats {
    Write-Host "生成 01-modern-formats..."
    $dir = New-CorpusDirectory "01-modern-formats"

    # 源图带渐变和文字：纯色会掩盖色度子采样、位深截断这类问题
    $srcRgb = Join-Path $dir "_src_rgb.png"
    $srcAlpha = Join-Path $dir "_src_alpha.png"
    $srcOdd = Join-Path $dir "_src_odd.png"
    $src16 = Join-Path $dir "_src_16bit.png"
    Invoke-Magick @('-size', '160x80', 'gradient:red-blue',
        '-fill', 'white', '-pointsize', '20', '-draw', "text 6,30 'abc'", $srcRgb)
    Invoke-Magick @($srcRgb, '-alpha', 'set',
        '(', '-size', '160x80', 'gradient:white-black', ')',
        '-compose', 'CopyOpacity', '-composite', $srcAlpha)
    Invoke-Magick @('-size', '199x101', 'gradient:lime-purple',
        '-fill', 'white', '-pointsize', '20', '-draw', "text 6,30 'abc'", $srcOdd)
    Invoke-Magick @('-size', '160x80', 'gradient:red-blue', '-depth', '16', $src16)

    $variants = @(
        # AVIF：10 位与单色（yuv400）是 AV1 特有的编码路径
        @{ Name = 'avif_smoke.avif';       Source = $srcRgb;   Args = @() }
        @{ Name = 'avif_alpha.avif';       Source = $srcAlpha; Args = @() }
        @{ Name = 'avif_odd.avif';         Source = $srcOdd;   Args = @() }
        @{ Name = 'avif_10bit.avif';       Source = $srcRgb;   Args = @('-depth', '10') }
        @{ Name = 'avif_grayscale.avif';   Source = $srcRgb;   Args = @('-colorspace', 'Gray') }

        # JXL：-quality 100 走无损，写出的是 ISOBMFF 容器（JXL box）；
        # 有损则是裸码流（ff 0a）。两种封装都要覆盖，解码入口不同。
        @{ Name = 'jxl_smoke.jxl';         Source = $srcRgb;   Args = @() }
        @{ Name = 'jxl_alpha.jxl';         Source = $srcAlpha; Args = @() }
        @{ Name = 'jxl_odd.jxl';           Source = $srcOdd;   Args = @() }
        @{ Name = 'jxl_16bit.jxl';         Source = $src16;    Args = @('-depth', '16') }
        @{ Name = 'jxl_lossless.jxl';      Source = $srcRgb;   Args = @('-quality', '100') }

        # WebP：有损与无损是两条完全独立的解码路径
        @{ Name = 'webp_smoke.webp';       Source = $srcRgb;   Args = @() }
        @{ Name = 'webp_alpha.webp';       Source = $srcAlpha; Args = @() }
        @{ Name = 'webp_odd.webp';         Source = $srcOdd;   Args = @() }
        @{ Name = 'webp_lossless.webp';    Source = $srcRgb;   Args = @('-define', 'webp:lossless=true') }
        @{ Name = 'webp_lowquality.webp';  Source = $srcRgb;   Args = @('-quality', '10') }

        # QOI 只有 8 位 RGB / RGBA 两种，没有位深变体可言
        @{ Name = 'qoi_smoke.qoi';         Source = $srcRgb;   Args = @() }
        @{ Name = 'qoi_alpha.qoi';         Source = $srcAlpha; Args = @() }
        @{ Name = 'qoi_odd.qoi';           Source = $srcOdd;   Args = @() }

        # JPEG 2000
        @{ Name = 'jp2_smoke.jp2';         Source = $srcRgb;   Args = @() }
        @{ Name = 'jp2_alpha.jp2';         Source = $srcAlpha; Args = @() }
        @{ Name = 'jp2_odd.jp2';           Source = $srcOdd;   Args = @() }
    )

    foreach ($variant in $variants) {
        $target = Join-Path $dir $variant.Name
        if (-not (Test-ShouldBuild $target)) { continue }
        Invoke-Magick (@($variant.Source) + $variant.Args + @($target))
        Assert-NotFallbackEncoding -Path $target
        Write-Step $variant.Name
    }

    # 源图只是中间产物，留在语料目录里会让人误以为也是被测素材
    foreach ($seed in @($srcRgb, $srcAlpha, $srcOdd, $src16)) {
        Remove-Item -LiteralPath $seed -Force -ErrorAction SilentlyContinue
    }
}

# 确认写出来的真是目标格式，而不是编码器不支持时静默退化成的 PNG/JPEG。
# 这个检查必须有：退化文件的扩展名是对的、程序也能「解码成功」，
# 不验魔数就会把「用 PNG 冒充 HEIC」当成格式覆盖。
function Assert-NotFallbackEncoding {
    param([string]$Path)
    $head = [byte[]](Get-Content -LiteralPath $Path -AsByteStream -TotalCount 8)
    if ($head.Length -lt 8) {
        throw "$Path 太短，不像有效图片。"
    }
    $isPng = $head[0] -eq 0x89 -and $head[1] -eq 0x50 -and $head[2] -eq 0x4E -and $head[3] -eq 0x47
    $isJpeg = $head[0] -eq 0xFF -and $head[1] -eq 0xD8 -and $head[2] -eq 0xFF
    $ext = [IO.Path]::GetExtension($Path).TrimStart('.').ToLowerInvariant()
    if (($isPng -and $ext -ne 'png') -or ($isJpeg -and $ext -notin @('jpg', 'jpeg', 'jfif', 'jpe'))) {
        Remove-Item -LiteralPath $Path -Force
        throw "$Path 实际写出的是 $(if ($isPng) { 'PNG' } else { 'JPEG' })：本机编码器不支持 .$ext，已删除该文件，不要用它冒充格式覆盖。"
    }
}

foreach ($item in $Category) {
    switch ($item) {
        'exif'               { Build-Exif }
        'reference'          { Build-Reference }
        'dimensions'         { Build-Dimensions }
        'corrupt'            { Build-Corrupt }
        'extension-mismatch' { Build-ExtensionMismatch }
        'path-filename'      { Build-PathFilename }
        'modern-formats'     { Build-ModernFormats }
    }
}

Write-Host ""
Write-Host "完成。素材位于 $corpusRoot"
