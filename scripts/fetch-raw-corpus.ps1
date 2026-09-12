<#
.SYNOPSIS
    下载相机 RAW 测试样本到本地（不进仓库），并把预期登记进 test/corpus/raw-expectations.json。

.DESCRIPTION
    素材来源是 raw.pixls.us 的 CC0（公有领域）样本库，每个扩展名取体积最小的那一个，
    覆盖面最大、下载量最小。当前 supportRaw 声明 38 个扩展，其中 28 个有 CC0 样本，
    合计约 356 MB。

    为什么文件不进仓库、而预期要进仓库：
      RAW 动辄十几兆，几十个就是几百兆，塞进 Git 不合适（测试规格明令禁止）。
      但预期不能跟着文件一起消失——否则没下过素材的人重建 manifest 时，
      RAW 用例会凭空不见，而报告上看不出少测了东西。
      所以预期（sha256、体积、尺寸）写进 raw-expectations.json 并提交，
      本地没有文件时 runner 记 SKIPPED，报告里如实显示"素材不存在于本地"。

    尺寸由 ImageMagick 的 raw 委托独立读出，不取自本程序的解码结果——
    用被测程序自己的输出当预期是循环论证。

.EXAMPLE
    .\fetch-raw-corpus.ps1                    # 下载全部（约 356 MB）
    .\fetch-raw-corpus.ps1 -MaxFileMB 12      # 只要 12 MB 以下的，跳过大文件
    .\fetch-raw-corpus.ps1 -Extension cr2,nef # 只下指定扩展名
    .\fetch-raw-corpus.ps1 -Verify            # 不下载，只校验已有文件的 sha256
#>
param(
    [double]$MaxFileMB = 0,          # 0 表示不限制
    [string[]]$Extension,            # 留空表示全部
    [switch]$Verify,                 # 只校验，不下载
    [switch]$Relabel,                # 不联网，只用本地文件重算尺寸/方向/不支持标记
    [switch]$Force                   # 已存在且哈希正确时也重新下载
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$localDir = Join-Path $repoRoot "test\corpus\_local\03-raw"
$expectPath = Join-Path $repoRoot "test\corpus\raw-expectations.json"
$listingUrl = "https://raw.pixls.us/json/getrepository.php?set=all"

# PowerShell 的 Invoke-WebRequest 在本机握不上 raw.pixls.us 的 TLS，curl 可以（走系统代理）
$curl = Join-Path $env:SystemRoot "System32\curl.exe"
if (-not (Test-Path -LiteralPath $curl)) { throw "找不到 curl.exe：$curl" }

# 程序声明支持的 RAW 扩展名。与 ImageDatabase.h 的 supportRaw 保持一致；
# 新增扩展名时这里要同步，否则新格式不会被纳入下载范围。
$supportRaw = @(
    '3fr', 'ari', 'arw', 'bay', 'cap', 'cr2', 'cr3', 'crw', 'dcr', 'dcs', 'dng', 'drf',
    'eip', 'erf', 'fff', 'gpr', 'iiq', 'k25', 'kdc', 'mdc', 'mef', 'mos', 'mrw', 'nef',
    'nrw', 'orf', 'pef', 'ptx', 'r3d', 'raf', 'raw', 'rw2', 'rwl', 'rwz', 'sr2', 'srf',
    'srw', 'x3f'
)

New-Item -ItemType Directory -Force -Path $localDir | Out-Null

function Get-Sha256 { param([string]$Path) (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

# 清单里的链接带原样空格和括号，例如
#   .../Nikon - D3200 - 12bit compressed (Lossy (type 2)) (3:2).NEF
# 不做百分号编码 curl 会直接失败。只编码路径段，保留 / 分隔符。
function Get-EncodedUrl {
    param([string]$Url)
    $u = [Uri]$Url
    $segments = $u.AbsolutePath.Split('/') | ForEach-Object { [Uri]::EscapeDataString($_) }
    return ("{0}://{1}{2}" -f $u.Scheme, $u.Authority, ($segments -join '/'))
}

# 上游文件名含 Windows 不允许的字符（冒号最常见，来自 "(3:2)" 这类宽高比标注），
# 直接落盘会失败。净化后仍保留原名记录在预期文件里，便于回溯到上游。
function Get-SafeFileName {
    param([string]$Name)
    $safe = $Name
    foreach ($c in [IO.Path]::GetInvalidFileNameChars()) {
        $safe = $safe.Replace([string]$c, '_')
    }
    return $safe
}

# 本机解码器确实打不开的格式。登记在这里是为了如实记 KNOWN_UNSUPPORTED——
# 规格明令不许把它当 PASS，也不许因为打不开就把素材删掉。
# 每条都附独立工具的判定依据；哪天支持上了，runner 会因「标记过期」而失败，
# 提醒把这条删掉，不会让过期标记长期掩盖回归。
$knownUnsupported = @{
    'ari' = 'ARRIRAW。LibRaw 不支持；ImageMagick 连 ARI 解码模块都没有（缺 IM_MOD_RL_ARI_.dll）。文件头 "ARRI" 正常，不是坏素材。'
    'gpr' = 'GoPro GPR。用 VC-5 压缩，需要 GoPro 的 GPR SDK，LibRaw 默认构建不含；ImageMagick 报 "Nonstandard tile length"。文件头 "II*." 正常。'
    'x3f' = 'Sigma Foveon X3F。LibRaw 需带 X3F 支持才能解；ImageMagick 报 "Unsupported file format or not RAW file"。文件头 "FOVb" 正常。'
}

# ---------------------------------------------------------------- 只校验
if ($Verify) {
    if (-not (Test-Path -LiteralPath $expectPath)) { throw "还没有 $expectPath，请先不带 -Verify 运行一次。" }
    $expect = Get-Content -LiteralPath $expectPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $ok = 0; $bad = 0; $missing = 0
    foreach ($e in $expect.samples) {
        $file = Join-Path $localDir $e.fileName
        if (-not (Test-Path -LiteralPath $file)) { $missing++; continue }
        if ((Get-Sha256 $file) -eq $e.sha256.ToLowerInvariant()) { $ok++ }
        else { $bad++; Write-Host ("  哈希不符: {0}" -f $e.fileName) -ForegroundColor Red }
    }
    Write-Host ("校验完成：正确 {0}，损坏 {1}，本地缺失 {2}" -f $ok, $bad, $missing)
    if ($bad -gt 0) { exit 1 }
    exit 0
}

# ---------------------------------------------------------------- 只重算标签
# 重新推导预期本不该依赖网络：素材已经在本地，尺寸、方向、是否支持都能就地算出来。
# 上游元数据（url、sha256、厂商型号）沿用已有登记。
if ($Relabel) {
    if (-not (Test-Path -LiteralPath $expectPath)) { throw "还没有 $expectPath，请先联网运行一次。" }
    $doc = Get-Content -LiteralPath $expectPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $relabelled = @()
    foreach ($s in $doc.samples) {
        $file = Join-Path $localDir $s.fileName
        if (-not (Test-Path -LiteralPath $file)) {
            Write-Host ("  本地缺失，保留原登记: {0}" -f $s.fileName) -ForegroundColor Yellow
            $relabelled += $s
            continue
        }
        $geometry = & magick identify -quiet -format "%w %h %[orientation]" -- "$file[0]" 2>$null
        $w = 0; $h = 0; $orient = ''
        if ($LASTEXITCODE -eq 0 -and $geometry) {
            $parts = $geometry.Trim() -split '\s+'
            if ($parts.Count -ge 2) { $w = [int]$parts[0]; $h = [int]$parts[1] }
            if ($parts.Count -ge 3) { $orient = $parts[2] }
        }
        if ($w -gt 0 -and $orient -in @('RightTop', 'RightBottom', 'LeftTop', 'LeftBottom')) {
            $swap = $w; $w = $h; $h = $swap
            Write-Host ("  .{0,-4} 方向 {1}：转正后 {2}x{3}" -f $s.extension, $orient, $w, $h)
        }
        $unsupported = $knownUnsupported.ContainsKey([string]$s.extension)
        $relabelled += [ordered]@{
            extension = $s.extension; fileName = $s.fileName; upstreamName = $s.upstreamName
            make = $s.make; model = $s.model; variant = $s.variant
            url = $s.url; sha256 = $s.sha256
            bytes = (Get-Item -LiteralPath $file).Length
            width = $w; height = $h; orientation = $orient
            knownUnsupported = $unsupported
            unsupportedReason = if ($unsupported) { $knownUnsupported[[string]$s.extension] } else { '' }
        }
    }
    $doc.samples = @($relabelled | Sort-Object extension)
    $doc.fetchedAt = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
    [IO.File]::WriteAllText($expectPath, ($doc | ConvertTo-Json -Depth 6),
        (New-Object Text.UTF8Encoding $false))
    Write-Host ("已重算 {0} 个样本的预期" -f $doc.samples.Count)
    exit 0
}

# ---------------------------------------------------------------- 取清单
Write-Host "获取 raw.pixls.us 清单..."
$listing = Join-Path $env:TEMP ("pixls-repo-{0}.json" -f $PID)
& $curl -sS -L --max-time 180 $listingUrl --output $listing | Out-Null
if (-not (Test-Path -LiteralPath $listing)) { throw "清单下载失败。" }
$raw = Get-Content -LiteralPath $listing -Raw -Encoding UTF8 | ConvertFrom-Json
Remove-Item -LiteralPath $listing -Force -ErrorAction SilentlyContinue

# 清单每行是数组：0 厂商 1 型号 2 变体 3 像素 4 - 5 许可HTML 6 日期 7 下载HTML(含链接/sha256/体积)
$candidates = @()
foreach ($row in $raw.data) {
    if ($row[5] -notlike '*publicdomain/zero*') { continue }   # 只要 CC0
    $cell = [string]$row[7]
    $link = [regex]::Match($cell, "href='([^']+)'")
    $sha = [regex]::Match($cell, "Checksum'>([0-9a-f]{64})<")
    $size = [regex]::Match($cell, "&nbsp;\((\d+(?:\.\d+)?)(MB|KB|GB)\)")
    if (-not ($link.Success -and $sha.Success -and $size.Success)) { continue }

    $url = $link.Groups[1].Value
    $fileName = ($url -split '/')[-1]
    if ($fileName -notmatch '\.([A-Za-z0-9]+)$') { continue }
    $ext = $Matches[1].ToLowerInvariant()
    if ($supportRaw -notcontains $ext) { continue }

    $value = [double]$size.Groups[1].Value
    $mb = switch ($size.Groups[2].Value) { 'KB' { $value / 1024 } 'GB' { $value * 1024 } default { $value } }

    $candidates += [pscustomobject]@{
        extension = $ext; make = [string]$row[0]; model = [string]$row[1]
        variant = [string]$row[2]; url = $url
        fileName = (Get-SafeFileName ([Uri]::UnescapeDataString($fileName)))
        upstreamName = [Uri]::UnescapeDataString($fileName)
        sha256 = $sha.Groups[1].Value; megabytes = [math]::Round($mb, 2)
    }
}
Write-Host ("CC0 候选 {0} 个，覆盖 {1} 个扩展名" -f $candidates.Count,
    ($candidates | Select-Object -Expand extension -Unique).Count)

# 每个扩展名取体积最小的一个：覆盖面最大、下载量最小
$picks = $candidates | Group-Object extension | ForEach-Object {
    $_.Group | Sort-Object megabytes | Select-Object -First 1
} | Sort-Object extension

if ($Extension) {
    $wanted = $Extension | ForEach-Object { $_.ToLowerInvariant() }
    $picks = $picks | Where-Object { $wanted -contains $_.extension }
}
if ($MaxFileMB -gt 0) {
    $tooBig = $picks | Where-Object { $_.megabytes -gt $MaxFileMB }
    foreach ($t in $tooBig) {
        Write-Host ("  跳过 .{0}（{1} MB 超过上限 {2} MB）" -f $t.extension, $t.megabytes, $MaxFileMB)
    }
    $picks = $picks | Where-Object { $_.megabytes -le $MaxFileMB }
}

$missingExt = $supportRaw | Where-Object { ($candidates | Select-Object -Expand extension -Unique) -notcontains $_ }
Write-Host ""
Write-Host ("将下载 {0} 个文件，合计约 {1:N0} MB" -f $picks.Count,
    (($picks | Measure-Object -Sum megabytes).Sum))
if ($missingExt) {
    Write-Host ("样本库里没有 CC0 样本的扩展名（如实记为缺口，不用改后缀冒充）：{0}" -f ($missingExt -join ' '))
}
Write-Host ""

# ---------------------------------------------------------------- 下载并校验
$samples = @()
$failures = @()
foreach ($p in $picks) {
    $target = Join-Path $localDir $p.fileName

    $needDownload = $true
    if ((-not $Force) -and (Test-Path -LiteralPath $target)) {
        if ((Get-Sha256 $target) -eq $p.sha256.ToLowerInvariant()) {
            Write-Host ("  已有 .{0,-4} {1}" -f $p.extension, $p.fileName)
            $needDownload = $false
        }
    }

    if ($needDownload) {
        Write-Host ("  下载 .{0,-4} {1,7:N1} MB  {2}" -f $p.extension, $p.megabytes, $p.fileName)

        # 只有本地已有残包时才续传。对不存在的文件也加 -C - 会让 curl 发出
        # 无意义的 Range 请求，某些响应下直接失败。
        $args = @('-sS', '-L', '--max-time', '900', '-w', 'http=%{http_code}')
        if (Test-Path -LiteralPath $target) { $args += @('-C', '-') }
        $args += @((Get-EncodedUrl $p.url), '--output', $target)

        # 失败原因必须留下来。之前这里 2>&1 | Out-Null 把 curl 的报错全吞了，
        # 只剩一句「下载失败」，根本没法排查。
        $curlOut = (& $curl @args 2>&1) -join ' '
        $curlExit = $LASTEXITCODE
        if ($curlExit -ne 0 -or -not (Test-Path -LiteralPath $target)) {
            Write-Host ("    下载失败（curl 退出码 {0}）：{1}" -f $curlExit, $curlOut.Trim()) -ForegroundColor Yellow
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            $failures += ("{0}（curl {1}）" -f $p.fileName, $curlExit)
            continue
        }
        if ($curlOut -notmatch 'http=(200|206)') {
            Write-Host ("    服务器返回 {0}，跳过" -f $curlOut.Trim()) -ForegroundColor Yellow
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            $failures += ("{0}（{1}）" -f $p.fileName, $curlOut.Trim())
            continue
        }
        $actual = Get-Sha256 $target
        if ($actual -ne $p.sha256.ToLowerInvariant()) {
            # 哈希不符的文件绝不留下：留着会让后续测试拿坏素材当真样本
            Remove-Item -LiteralPath $target -Force
            Write-Host ("    sha256 不符，已删除（期望 {0}，实际 {1}）" -f $p.sha256, $actual) -ForegroundColor Red
            $failures += ("{0}（sha256 不符）" -f $p.fileName)
            continue
        }
    }

    # 尺寸由 ImageMagick 的 raw 委托独立读出。用被测程序自己的输出当预期是循环论证。
    #
    # 必须自己应用方向：identify 给的是**存储**尺寸，方向单独放在 Orientation 字段里。
    # LibRaw 默认 user_flip=-1，会按元数据把图转正，所以本程序输出的是**已转正**的尺寸。
    # 不补这一步，竖拍的 RAW 会被判成宽高颠倒——Leaf Aptus 22 那张就是这么一次误判：
    # identify 报 4008x5344 + Orientation RightTop，程序报 5344x4008，程序是对的。
    # 这和 12-exif 那一类是同一个坑，README 里记过，RAW 这条路上又踩了一次。
    $geometry = & magick identify -quiet -format "%w %h %[orientation]" -- "$target[0]" 2>$null
    $width = 0; $height = 0; $orientation = ''
    if ($LASTEXITCODE -eq 0 -and $geometry) {
        $parts = $geometry.Trim() -split '\s+'
        if ($parts.Count -ge 2) { $width = [int]$parts[0]; $height = [int]$parts[1] }
        if ($parts.Count -ge 3) { $orientation = $parts[2] }
    }
    # 这四种方向含 90° 旋转，转正后宽高互换
    if ($width -gt 0 -and $orientation -in @('RightTop', 'RightBottom', 'LeftTop', 'LeftBottom')) {
        $swap = $width; $width = $height; $height = $swap
        Write-Host ("    方向 {0}：转正后尺寸为 {1}x{2}" -f $orientation, $width, $height)
    }
    if ($width -le 0) {
        Write-Host ("    ImageMagick 读不出尺寸，本条不登记尺寸期望") -ForegroundColor Yellow
    }

    $samples += [ordered]@{
        extension = $p.extension
        fileName  = $p.fileName
        upstreamName = $p.upstreamName
        make      = $p.make
        model     = $p.model
        variant   = $p.variant
        url       = $p.url
        sha256    = $p.sha256.ToLowerInvariant()
        bytes     = (Get-Item -LiteralPath $target).Length
        width     = $width
        height    = $height
        orientation = $orientation
        knownUnsupported = [bool]$knownUnsupported.ContainsKey($p.extension)
        unsupportedReason = if ($knownUnsupported.ContainsKey($p.extension)) { $knownUnsupported[$p.extension] } else { '' }
    }
}

# ---------------------------------------------------------------- 写预期
# 合并而不是覆盖：分批下载（例如先 -MaxFileMB 12，之后再补大文件）时，
# 覆盖会把上一批的登记冲掉。
$merged = @{}
if (Test-Path -LiteralPath $expectPath) {
    $old = Get-Content -LiteralPath $expectPath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($s in $old.samples) { $merged[$s.fileName] = $s }
}
foreach ($s in $samples) { $merged[$s.fileName] = $s }

$document = [ordered]@{
    version    = 1
    note       = '由 scripts/fetch-raw-corpus.ps1 生成。素材体积大，只放在本地 test/corpus/_local/03-raw/（已被 .gitignore 挡住），预期登记在此文件并提交，本地缺文件时 runner 记 SKIPPED。'
    source     = 'raw.pixls.us CC0（公有领域）样本库'
    sourceUrl  = 'https://raw.pixls.us/'
    license    = 'CC0 1.0 Universal（Public Domain Dedication）'
    localDir   = 'test/corpus/_local/03-raw'
    fetchedAt  = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
    dimensionSource = 'ImageMagick raw 委托独立读出，不取自被测程序'
    missingExtensions = @($missingExt)
    samples    = @($merged.Values | Sort-Object extension)
}
[IO.File]::WriteAllText($expectPath, ($document | ConvertTo-Json -Depth 6),
    (New-Object Text.UTF8Encoding $false))

Write-Host ""
Write-Host ("已登记 {0} 个样本到 {1}" -f $document.samples.Count, $expectPath)
Write-Host ("本地素材目录 {0}" -f $localDir)
Write-Host "接着运行 scripts/build-corpus-manifest.ps1 把 RAW 用例并入 manifest。"

# 显式给退出码。之前末尾没有 exit，最后一次 curl 的退出码会泄漏成脚本的退出码，
# 明明大部分文件下好了却报失败。
if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host ("有 {0} 个文件没下到（多为瞬时网络/TLS 失败，重跑本脚本即可补齐，已下好的会跳过）：" -f $failures.Count) -ForegroundColor Yellow
    foreach ($f in $failures) { Write-Host ("  {0}" -f $f) }
    exit 1
}
exit 0
