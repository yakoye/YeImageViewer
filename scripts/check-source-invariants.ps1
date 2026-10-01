<#
.SYNOPSIS
跑起来也看不出来的源码约定，在这里一次性查完。

.DESCRIPTION
这里查的四件事都有一个共同点：违反了程序照样编译、照样运行，错误要么只在某一种
语言/某一种退出时机下才露出来，要么压根不在程序里露出来（README 漏格式）。
所以只能从源码上盯：

1. 三语字符串表三列齐全，`// N` 索引标注没错位（中间插一条会让所有硬编码 stringID 整体错位）。
2. 没有谁把「界面是不是中文」写成 `UI_LANG == 0`（繁體是 2，会掉进英文分支）。
3. 继承 `LRU<>` 的类都在自己的析构函数里先调了 `stopPreloadWorker()`。
4. 格式清单在解码表、默认关联、两份 README 之间对得上。

`runTests.ps1` 会调用本脚本；提交前想单独自检也可以直接跑，只读源码，几秒钟出结果。
自检本身对不对，用 `scripts/verify-source-invariant-checks.ps1` 反向验证。
#>
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $RepoRoot

# 默认关联的格式清单直接从程序的常量里读，避免脚本另抄一份、两边走偏
$defaultExtSource = [IO.File]::ReadAllText((Join-Path $repoRoot "YeImageViewer\include\jarkUtils.h"))
if ($defaultExtSource -notmatch 'defaultExtList\{\s*\r?\n?\s*"([^"]+)"') {
    throw "Could not read SettingParameter::defaultExtList from jarkUtils.h."
}
$expectedOpenWithExt = $Matches[1] -split ","

Write-Host "Checking source-level invariants that no runtime test can catch..."

# 一、三语字符串表。
# 这两张表的索引是硬编码的，中间插一条就把后面全错位；而漏填一列会让界面
# 当场显示空白。两样都不会让程序崩，所以只能在源码上盯。
$stringResPath = Join-Path $repoRoot "YeImageViewer\src\stringRes.cpp"
$stringResLines = Get-Content -LiteralPath $stringResPath -Encoding UTF8
$stringLiteral = [regex]'"(?:[^"\\]|\\.)*"'
$indexMarker = [regex]'//\s*(\d+)\s*$'
$maxEntries = 0
foreach ($line in $stringResLines) {
    if ($line -match 'constexpr uint32_t STRING_MAX_NUM\s*=\s*(\d+)') {
        $maxEntries = [int]$Matches[1]
        break
    }
}
if ($maxEntries -le 0) {
    throw "String table regression failed: STRING_MAX_NUM was not found in stringRes.cpp."
}

$currentTable = ""
$entryIndex = 0
$tableCounts = @{}
for ($lineNo = 0; $lineNo -lt $stringResLines.Count; $lineNo++) {
    $line = $stringResLines[$lineNo]
    $humanLine = $lineNo + 1

    if ($line -match '^std::(?:w)?string_view (UIStringTable\w*)\[') {
        $currentTable = $Matches[1]
        $entryIndex = 0
        continue
    }
    if ($currentTable -ne "" -and $line.StartsWith("};")) {
        $tableCounts[$currentTable] = $entryIndex
        $currentTable = ""
        continue
    }
    if ($currentTable -eq "" -or -not $line.StartsWith("    {")) {
        continue
    }

    $columns = @($stringLiteral.Matches($line) | ForEach-Object { $_.Value })
    if ($columns.Count -ne 3) {
        throw ("String table regression failed: ${currentTable} entry ${entryIndex} " +
            "(stringRes.cpp:${humanLine}) has $($columns.Count) columns, expected 3 " +
            "(0 简体中文 / 1 English / 2 繁體中文).")
    }

    $texts = @($columns | ForEach-Object {
        $raw = $_
        if ($raw.StartsWith("L")) { $raw = $raw.Substring(1) }
        $raw.Trim('"')
    })
    for ($column = 0; $column -lt 3; $column++) {
        if ($texts[$column] -eq "") {
            throw ("String table regression failed: ${currentTable} entry ${entryIndex} " +
                "(stringRes.cpp:${humanLine}) leaves column ${column} empty; the UI would " +
                "show nothing there.")
        }
    }

    # 繁體列照抄英文是最常见的漏填方式：界面不空、不崩，但繁體用户看到英文。
    # 三列全同是正常的（YeImageViewer、English 这类专有名词），所以只在
    # 简体和英文确实不同时才判。
    if ($texts[0] -ne $texts[1] -and $texts[2] -eq $texts[1]) {
        throw ("String table regression failed: ${currentTable} entry ${entryIndex} " +
            "(stringRes.cpp:${humanLine}) fills the 繁體中文 column with the English text " +
            "'$($texts[1])'.")
    }

    # 表里每 10 条有一个 `// N` 索引标注。它和实际序号不符，说明有人在中间
    # 插/删了条目——那会把所有硬编码的 stringID 整体错位。
    $markerMatch = $indexMarker.Match($line)
    if ($markerMatch.Success) {
        $declared = [int]$markerMatch.Groups[1].Value
        if ($declared -ne $entryIndex) {
            throw ("String table regression failed: ${currentTable} marks " +
                "stringRes.cpp:${humanLine} as index ${declared} but it is actually " +
                "${entryIndex}; an entry was inserted or removed mid-table and every " +
                "hard-coded stringID after it now points at the wrong text.")
        }
    }

    $entryIndex++
}

if ($currentTable -ne "") {
    throw "String table regression failed: ${currentTable} was never closed in stringRes.cpp."
}
foreach ($expectedTable in @("UIStringTable", "UIStringTableW")) {
    if (-not $tableCounts.ContainsKey($expectedTable)) {
        throw "String table regression failed: ${expectedTable} was not found in stringRes.cpp."
    }
    if ($tableCounts[$expectedTable] -gt $maxEntries) {
        throw ("String table regression failed: ${expectedTable} holds " +
            "$($tableCounts[$expectedTable]) entries but STRING_MAX_NUM is ${maxEntries}.")
    }
}
Write-Host ("PASS both string tables are complete in all three languages " +
    "($($tableCounts['UIStringTable']) + $($tableCounts['UIStringTableW']) entries).")

# 二、「当前界面是不是中文」不能写成 UI_LANG == 0。
# 繁體是 2，写成 == 0 会让繁體界面掉进英文分支。判繁體专属文案用 == 2 是对的，
# 所以只拦 0。
$languageOffenders = @()
foreach ($source in Get-ChildItem -Path (Join-Path $repoRoot "YeImageViewer") -Recurse `
        -Include *.cpp, *.h -File) {
    # 第三方库的头文件目录不参与
    if ($source.FullName -match '\\include\\(opencv2|exiv2|ffmpeg|libraw|libheif|libwebp2|jxl|aom|dav1d|libde265|libyuv|minizip|psdsdk)\\') {
        continue
    }
    $languageHits = Select-String -LiteralPath $source.FullName -Pattern 'UI_LANG\s*(==|!=)\s*0' -AllMatches
    foreach ($hit in $languageHits) {
        # 注释里写出这个反例是为了解释为什么不能这么写，不算违规
        $trimmed = $hit.Line.Trim()
        if ($trimmed.StartsWith("//") -or $trimmed.StartsWith("*") -or $trimmed.StartsWith("/*")) {
            continue
        }
        $languageOffenders += "$($source.Name):$($hit.LineNumber): $trimmed"
    }
}
if ($languageOffenders.Count -gt 0) {
    throw ("Language regression failed: compare against UI_LANG == 0 drops 繁體中文 into the " +
        "English branch. Use isChineseUI() / tr() / UiLanguage::pick() instead.`n" +
        ($languageOffenders -join "`n"))
}
Write-Host "PASS no code decides 'is the UI Chinese' by comparing UI_LANG against 0."

# 三、继承 LRU 的类必须在自己的析构函数里先停预读线程。
# 预读线程跑的是派生类的 loader()，用的是派生类的成员。等基类析构才停就晚了：
# 派生部分已经销毁，在途那次解码正访问已释放的内存。打开大图后立刻退出曾这样崩过。
$lruDerived = @()
foreach ($source in Get-ChildItem -Path (Join-Path $repoRoot "YeImageViewer") -Recurse `
        -Include *.cpp, *.h -File) {
    $text = Get-Content -LiteralPath $source.FullName -Raw -Encoding UTF8
    foreach ($hit in [regex]::Matches($text, 'class\s+(\w+)\s*:\s*(?:public\s+)?LRU\s*<')) {
        $lruDerived += [pscustomobject]@{ Name = $hit.Groups[1].Value; File = $source; Text = $text }
    }
}
if ($lruDerived.Count -eq 0) {
    throw "LRU shutdown regression failed: no class deriving from LRU<> was found; the check went stale."
}
foreach ($derived in $lruDerived) {
    $destructor = [regex]::Match($derived.Text, "~$($derived.Name)\s*\(\s*\)\s*(?:override\s*)?\{")
    if (-not $destructor.Success) {
        throw ("LRU shutdown regression failed: $($derived.Name) derives from LRU<> but has no " +
            "destructor of its own, so the preload worker only stops in ~LRU() — after the " +
            "derived members are already gone.")
    }
    $bodyStart = $destructor.Index + $destructor.Length
    $body = $derived.Text.Substring($bodyStart, [Math]::Min(600, $derived.Text.Length - $bodyStart))
    if ($body -notmatch 'stopPreloadWorker\s*\(\s*\)') {
        throw ("LRU shutdown regression failed: ~$($derived.Name) does not call " +
            "stopPreloadWorker(). The preload worker runs $($derived.Name)::loader() against " +
            "$($derived.Name) members; stopping it only in ~LRU() is a use-after-free when a " +
            "decode is still in flight at exit.")
    }
}
Write-Host ("PASS every LRU-derived cache stops its preload worker before its own members die " +
    "($($lruDerived.Count) class(es)).")

# 四、格式清单必须对得上。
# 加一个格式要同时改好几处：解码分派、supportExt/supportRaw、默认关联列表、
# 两份 README。少改一处不会报错，只会让某个格式「程序能开但没人告诉用户」或者
# 「关联了却打不开」。这里从源码读真值，逐项核对。
$imageDatabaseSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot "YeImageViewer\include\ImageDatabase.h") -Raw -Encoding UTF8

function Get-WideExtensionSet([string]$Source, [string]$Name) {
    $block = [regex]::Match($Source, "$Name\s*\{(.*?)\};", "Singleline")
    if (-not $block.Success) {
        throw "Format list regression failed: ${Name} was not found in ImageDatabase.h."
    }
    $found = [regex]::Matches($block.Groups[1].Value, 'L"([^"]+)"') |
        ForEach-Object { $_.Groups[1].Value }
    if (@($found).Count -eq 0) {
        throw "Format list regression failed: ${Name} is empty."
    }
    return , @($found)
}

$supportExtList = Get-WideExtensionSet $imageDatabaseSource "supportExt"
$supportRawList = Get-WideExtensionSet $imageDatabaseSource "supportRaw"
$videoExtList = Get-WideExtensionSet $imageDatabaseSource "videoExt"
$animationExtList = Get-WideExtensionSet $imageDatabaseSource "opencvAnimationExt"

foreach ($listPair in @(
        @{ Name = "supportExt"; Items = $supportExtList },
        @{ Name = "supportRaw"; Items = $supportRawList },
        @{ Name = "videoExt"; Items = $videoExtList })) {
    $duplicates = @($listPair.Items | Group-Object | Where-Object { $_.Count -gt 1 })
    if ($duplicates.Count -gt 0) {
        throw ("Format list regression failed: $($listPair.Name) lists " +
            "$($duplicates.Name -join ', ') more than once.")
    }
    $lowercase = @($listPair.Items | Where-Object { $_ -cne $_.ToLowerInvariant() })
    if ($lowercase.Count -gt 0) {
        throw ("Format list regression failed: $($listPair.Name) holds non-lowercase entries " +
            "($($lowercase -join ', ')); extensions are compared lowercase.")
    }
}

$decodableExt = @($supportExtList + $supportRawList + $videoExtList) | Sort-Object -Unique
foreach ($ext in $animationExtList) {
    if ($decodableExt -notcontains $ext) {
        throw ("Format list regression failed: opencvAnimationExt lists .${ext} but no decode " +
            "list claims it, so the animation path can never be reached.")
    }
}

# 默认关联的每个格式都必须真能解码，否则双击打开的是一个报错窗口
foreach ($ext in $expectedOpenWithExt) {
    if ($decodableExt -notcontains $ext) {
        throw ("Format list regression failed: SettingParameter::defaultExtList associates " +
            ".${ext} but ImageDatabase cannot decode it.")
    }
}
$defaultSorted = @($expectedOpenWithExt | Sort-Object)
for ($i = 0; $i -lt $expectedOpenWithExt.Count; $i++) {
    if ($expectedOpenWithExt[$i] -ne $defaultSorted[$i]) {
        throw ("Format list regression failed: SettingParameter::defaultExtList is not sorted; " +
            "the settings page shows it in file order and an unsorted list reads as a mistake.")
    }
}

# 两份 README 的格式行要和源码逐字一致。README 是用户唯一能看到的「支持什么」，
# 漏一个格式等于这个格式白做了。
$readmeExpectations = @(
    @{ File = "README.md"; Prefix = "- 静态："; Items = $supportExtList },
    @{ File = "README.md";
        Prefix = "- 视频（只在直接打开时解码前若干帧当动图，不出现在同目录的翻页列表里）：";
        Items = $videoExtList },
    @{ File = "README.md"; Prefix = "- RAW："; Items = $supportRawList },
    @{ File = "README_EN.md"; Prefix = "- Still: "; Items = $supportExtList },
    @{ File = "README_EN.md";
        Prefix = "- Video (decoded only when opened directly, as an animation of the first frames; never listed while paging through a folder): ";
        Items = $videoExtList },
    @{ File = "README_EN.md"; Prefix = "- RAW: "; Items = $supportRawList }
)
foreach ($expectation in $readmeExpectations) {
    $readmePath = Join-Path $repoRoot $expectation.File
    $expectedLine = $expectation.Prefix + "``" + (@($expectation.Items | Sort-Object) -join " ") + "``"
    $readmeLines = Get-Content -LiteralPath $readmePath -Encoding UTF8
    $actualLine = @($readmeLines | Where-Object { $_.StartsWith($expectation.Prefix) })
    if ($actualLine.Count -ne 1) {
        throw ("Format list regression failed: $($expectation.File) has $($actualLine.Count) " +
            "lines starting with '$($expectation.Prefix)', expected exactly 1.")
    }
    if ($actualLine[0] -ne $expectedLine) {
        throw ("Format list regression failed: $($expectation.File) is out of sync with " +
            "ImageDatabase.h.`nexpected: ${expectedLine}`nactual:   $($actualLine[0])")
    }
}

# 「动态」那一行是人工挑的子集，只要求它挑的每个格式都真能解码
foreach ($readmeAnimated in @(
        @{ File = "README.md"; Prefix = "- 动态：" },
        @{ File = "README_EN.md"; Prefix = "- Animated: " })) {
    $readmePath = Join-Path $repoRoot $readmeAnimated.File
    $line = @(Get-Content -LiteralPath $readmePath -Encoding UTF8 |
        Where-Object { $_.StartsWith($readmeAnimated.Prefix) })
    if ($line.Count -ne 1) {
        throw "Format list regression failed: $($readmeAnimated.File) has no single animated-format line."
    }
    $listed = [regex]::Match($line[0], '`([^`]+)`').Groups[1].Value -split '\s+'
    foreach ($ext in $listed) {
        if ($decodableExt -notcontains $ext) {
            throw ("Format list regression failed: $($readmeAnimated.File) advertises animated " +
                ".${ext} but no decode list claims it.")
        }
    }
}
Write-Host ("PASS the format lists agree across ImageDatabase, the default associations, and " +
    "both READMEs ($($supportExtList.Count) still, $($videoExtList.Count) video, " +
    "$($supportRawList.Count) RAW).")

# 五、PSD 必须 psd_sdk 优先、stb 兜底，顺序不能反。
# stb_image 不支持「16 位 + RLE 压缩」的 PSD：它那条 RLE 分支按每通道 pixelCount
# 个字节解，而 16 位每通道是 pixelCount*2 个字节，从第二个通道起全部错位，
# alpha 读成整层零，图在界面上完全不可见。要命的是它**返回成功**，所以兜底的
# loadPSD 永远轮不到，也不会有任何报错。整条诊断过程见 test/corpus/README.md。
$loaderSource = Get-Content -LiteralPath `
    (Join-Path $repoRoot "YeImageViewer\src\ImageDatabase.cpp") -Raw -Encoding UTF8
$psdBranch = [regex]::Match($loaderSource,
    'ext\s*==\s*L"psd".*?\r?\n\s*\}', 'Singleline')
if (-not $psdBranch.Success) {
    throw ("PSD loader regression failed: the psd/psdt branch was not found in " +
        "ImageDatabase.cpp; this check went stale.")
}
$psdBody = $psdBranch.Value
$psdSdkAt = $psdBody.IndexOf("loadPSD(")
$stbAt = $psdBody.IndexOf("loadSTB(")
if ($psdSdkAt -lt 0 -or $stbAt -lt 0) {
    throw ("PSD loader regression failed: the psd/psdt branch no longer calls both " +
        "loadPSD and loadSTB.")
}
if ($psdSdkAt -gt $stbAt) {
    throw ("PSD loader regression failed: loadSTB runs before loadPSD. stb_image cannot " +
        "decode 16-bit RLE PSD — it misaligns every channel after the first and returns " +
        "a fully transparent image, while still reporting success, so loadPSD never gets " +
        "a turn. Put loadPSD first (see test/corpus/README.md).")
}
Write-Host "PASS PSD decoding tries psd_sdk before stb, so 16-bit RLE files stay visible."

