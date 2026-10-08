param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Keep visible real-window regressions on the primary (small) monitor so they
# do not interrupt work on a larger secondary display. Normal application
# launches do not inherit this test-only process environment variable.
$env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR = "1"

$repoRoot = $PSScriptRoot
$releaseDir = Join-Path $repoRoot "x64\Release"
$viewer = Join-Path $releaseDir "YeImageViewer.exe"
$unitTests = Join-Path $releaseDir "YeImageViewerTests.exe"
$crashFixture = Join-Path $repoRoot "test\Image crash\dji_export_photo_20260809221510044.jpg"
$hdrFixture = Join-Path $repoRoot "test\HDR color error\HDR.hdr"
$sharpSvgFixture = Join-Path $repoRoot "test\SVG Blurring\SittingHuman.svg"
$textSvgFixture = Join-Path $repoRoot "test\severely jagged\cachetest.drawio.svg"
$jaggedFixture = Join-Path $repoRoot "test\severely jagged\cachetest.drawio.png"
$commonPngFixture = Join-Path $repoRoot "test\format corpus\files\common.png"
$formatCorpusRoot = Join-Path $repoRoot "test\format corpus"
$formatManifest = Join-Path $formatCorpusRoot "manifest.tsv"
$currentRestoreFixtures = @(
    (Join-Path $repoRoot "test\current image restore\01-small.svg"),
    (Join-Path $repoRoot "test\current image restore\02-landscape.svg"),
    (Join-Path $repoRoot "test\current image restore\03-square.svg"),
    (Join-Path $repoRoot "test\current image restore\04-wide.svg"),
    (Join-Path $repoRoot "test\current image restore\05-tall-capped.svg")
)
$toolbarIcons = @(
    (Join-Path $repoRoot "YeImageViewer\file\icons\previous.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\next.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\rotate-left.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\rotate-right.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\flip-horizontal.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\flip-vertical.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\fit-window.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\fit-image.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\actual-size.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\fullscreen.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\favorite.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\copy.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\delete.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\settings.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\zoom-out.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\zoom-in.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\play.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\pause.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\close.svg")
)
$appIcons = @(
    (Join-Path $repoRoot "ico.ico"),
    (Join-Path $repoRoot "YeImageViewer\YeImageViewer.ico"),
    (Join-Path $repoRoot "YeImageViewer\small.ico")
)
$appIconPreview = Join-Path $repoRoot "ico.png"

if (-not $SkipBuild) {
    $shell = Join-Path $PSHOME "pwsh.exe"
    if (-not (Test-Path -LiteralPath $shell)) {
        $shell = Join-Path $PSHOME "powershell.exe"
    }

    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "buildRelease.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed with exit code $LASTEXITCODE."
    }
}

foreach ($requiredFile in @($viewer, $unitTests, $crashFixture, $hdrFixture, $sharpSvgFixture, $textSvgFixture, $jaggedFixture, $commonPngFixture, $appIconPreview, $formatManifest) + $toolbarIcons + $appIcons + $currentRestoreFixtures) {
    if (-not (Test-Path -LiteralPath $requiredFile)) {
        throw "Required regression-test file is missing: $requiredFile"
    }
}

$expectedFileVersion = "1.37.4.0"
$actualFileVersion = (Get-Item -LiteralPath $viewer).VersionInfo.FileVersion
if ($actualFileVersion -ne $expectedFileVersion) {
    throw "Viewer file version mismatch: expected $expectedFileVersion, got $actualFileVersion."
}
Write-Host "PASS viewer file version is $expectedFileVersion."

# 预发布版的后缀（-rc1 这类）写在 ProductVersion 字符串里，打包脚本据此给安装包命名
$expectedProductVersion = "1.37.4"
$actualProductVersion = (Get-Item -LiteralPath $viewer).VersionInfo.ProductVersion
if ($actualProductVersion -ne $expectedProductVersion) {
    throw "Viewer product version mismatch: expected $expectedProductVersion, got $actualProductVersion."
}
Write-Host "PASS viewer product version is $expectedProductVersion."

# 体积上限：编码器、OpenCV 的 IPP/contrib、FFmpeg 的全量编解码器都去掉后实测 26.4 MiB，上限收到 28 MiB。
# 减重每推进一步就把上限往下收一档，避免又被新的第三方库悄悄顶回去。
$maximumViewerBytes = 28MB
$viewerBytes = (Get-Item -LiteralPath $viewer).Length
if ($viewerBytes -gt $maximumViewerBytes) {
    throw "Viewer is $([math]::Round($viewerBytes / 1MB, 2)) MiB; the size budget is 28 MiB."
}
Write-Host "PASS viewer stays within the 28 MiB size budget ($([math]::Round($viewerBytes / 1MB, 2)) MiB)."

Write-Host "Checking local installer copy, shortcut, prompt, and launch contract..."
$installerScript = Join-Path $repoRoot "installLocal.ps1"
$installerBytes = [IO.File]::ReadAllBytes($installerScript)
if ($installerBytes.Length -lt 3 -or
    $installerBytes[0] -ne 0xEF -or
    $installerBytes[1] -ne 0xBB -or
    $installerBytes[2] -ne 0xBF) {
    throw "Installer regression failed: installLocal.ps1 must use a UTF-8 BOM so Windows PowerShell 5.1 does not parse Chinese text with the active ANSI code page."
}
$installerSource = [IO.File]::ReadAllText($installerScript)
[void][scriptblock]::Create($installerSource)
foreach ($requiredInstallerBehavior in @(
    "DesktopDirectory",
    "CreateShortcut",
    '.Popup($message',
    'Start-Process -FilePath $targetExe',
    "NoDesktopShortcut",
    "NoStartMenuShortcut",
    "NoPrompt",
    "NoLaunch",
    "SkipRegistration"
)) {
    if (-not $installerSource.Contains($requiredInstallerBehavior)) {
        throw "Installer regression failed: missing behavior marker $requiredInstallerBehavior."
    }
}
$installerTestDirectory = Join-Path ([IO.Path]::GetTempPath()) (
    "YeImageViewer-InstallTest-" + [Guid]::NewGuid().ToString("N"))
try {
    $installResult = & $installerScript -InstallDir $installerTestDirectory `
        -NoDesktopShortcut -NoStartMenuShortcut -NoPrompt -NoLaunch -SkipRegistration
    $installedViewer = Join-Path $installerTestDirectory "YeImageViewer.exe"
    $installedProvider = Join-Path $installerTestDirectory "YeThumbnailProvider.dll"
    if (-not (Test-Path -LiteralPath $installedViewer -PathType Leaf) -or
        -not (Test-Path -LiteralPath $installedProvider -PathType Leaf) -or
        $installResult.Launched -or $installResult.Registered -or
        $null -ne $installResult.DesktopShortcut -or
        $null -ne $installResult.StartMenuShortcut) {
        throw "Installer regression failed: safe test installation did not copy exactly the runtime without side effects."
    }
    if ((Get-FileHash -LiteralPath $viewer -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $installedViewer -Algorithm SHA256).Hash) {
        throw "Installer regression failed: installed executable differs from the Release build."
    }
}
finally {
    if (Test-Path -LiteralPath $installerTestDirectory) {
        $resolvedInstallerTestDirectory = [IO.Path]::GetFullPath($installerTestDirectory)
        $resolvedTempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $resolvedInstallerTestDirectory.StartsWith($resolvedTempRoot,
                [StringComparison]::OrdinalIgnoreCase) -or
            -not (Split-Path -Leaf $resolvedInstallerTestDirectory).StartsWith(
                "YeImageViewer-InstallTest-", [StringComparison]::Ordinal)) {
            throw "Refusing to remove unexpected installer test path: $resolvedInstallerTestDirectory"
        }
        Remove-Item -LiteralPath $resolvedInstallerTestDirectory -Recurse -Force
    }
}
Write-Host "PASS installer copies the runtime and defaults to desktop/Start-menu shortcuts, completion prompt, and launch."

Write-Host "Checking open-with registration covers every default format..."
if (-not $installerSource.Contains("--register-open-with")) {
    throw "Installer regression failed: installLocal.ps1 no longer registers the open-with entries."
}
# 期望的扩展名直接从程序的常量里读，避免脚本另抄一份、两边走偏
$defaultExtSource = [IO.File]::ReadAllText((Join-Path $repoRoot "YeImageViewer\include\jarkUtils.h"))
if ($defaultExtSource -notmatch 'defaultExtList\{\s*\r?\n?\s*"([^"]+)"') {
    throw "Could not read SettingParameter::defaultExtList from jarkUtils.h."
}
$expectedOpenWithExt = $Matches[1] -split ","
$openWithAppKey = "HKCU:\Software\Classes\Applications\YeImageViewer.exe"
# 取键的默认值。Get-ItemProperty -Name "(default)" 在值不存在时会抛，严格模式下直接中断，
# 而「这个值还没设过」恰恰是要区分的正常情况。
function Get-RegistryDefaultValue([string]$Path) {
    $key = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    if ($null -eq $key) { return $null }
    return $key.GetValue("")
}
# 「打开方式」只往候选列表里加一项，绝不能顺手改掉默认打开程序，先记下来后面对比
$defaultHandlerBefore = Get-RegistryDefaultValue "HKCU:\Software\Classes\.png"
# 有上限地等：这一步只写注册表，正常 0.1 秒就退出。真卡住多半是通知 shell 时被拖住，
# 那就是产品缺陷（安装脚本同样是同步等它），不能让测试无限期挂在这儿。
$registerProcess = Start-Process -FilePath $viewer -ArgumentList @("--register-open-with") -PassThru
if (-not $registerProcess.WaitForExit(30000)) {
    Stop-Process -Id $registerProcess.Id -Force -ErrorAction SilentlyContinue
    throw "Open-with registration did not finish within 30 seconds."
}
if ($registerProcess.ExitCode -ne 0) {
    throw "Open-with registration exited with code $($registerProcess.ExitCode)."
}
$supportedTypes = (Get-Item -LiteralPath "$openWithAppKey\SupportedTypes" -ErrorAction SilentlyContinue).Property
$openCommand = Get-RegistryDefaultValue "$openWithAppKey\shell\open\command"
if ($openCommand -ne ('"' + $viewer + '" "%1"')) {
    throw "Open-with regression failed: shell\open\command is '$openCommand'."
}
foreach ($ext in $expectedOpenWithExt) {
    if ($supportedTypes -notcontains ".$ext") {
        throw "Open-with regression failed: .$ext missing from SupportedTypes."
    }
    $progIds = (Get-Item -LiteralPath "HKCU:\Software\Classes\.$ext\OpenWithProgids" `
        -ErrorAction SilentlyContinue).Property
    if ($progIds -notcontains "YeImageViewer.ImageFile.$ext") {
        throw "Open-with regression failed: .$ext is not offered in the Open with list."
    }
}
$defaultHandlerAfter = Get-RegistryDefaultValue "HKCU:\Software\Classes\.png"
if ($defaultHandlerBefore -ne $defaultHandlerAfter) {
    throw "Open-with regression failed: registration changed the default handler for .png."
}
# 测试用的是构建目录里的 exe，跑完把注册指回已安装的那份，免得「打开方式」指向构建产物
$installedViewer = Join-Path $env:LOCALAPPDATA "Programs\YeImageViewer\YeImageViewer.exe"
if (Test-Path -LiteralPath $installedViewer -PathType Leaf) {
    $restoreProcess = Start-Process -FilePath $installedViewer `
        -ArgumentList @("--register-open-with") -PassThru
    if (-not $restoreProcess.WaitForExit(30000)) {
        Stop-Process -Id $restoreProcess.Id -Force -ErrorAction SilentlyContinue
    }
}
Write-Host "PASS open-with registration lists all $($expectedOpenWithExt.Count) default formats without changing the default handler."

$packageScript = Join-Path $repoRoot "packageRelease.ps1"
$packageSource = [IO.File]::ReadAllText($packageScript)
[void][scriptblock]::Create($packageSource)
# 发布产物必须是绿色版：一个裸 exe，外加一个根目录就放着 exe 的压缩包。
# 原先还做一个 7z SFX 一键安装器，已经去掉——它每次运行都会弹 Windows
# 「程序兼容性助手 / 可能未正确安装此程序」。那个 SFX 的版本信息写着
# "7z Setup SFX small"，PCA 据此判定它是安装程序，而它退出时又不写卸载项，
# 于是每装一次就吓人一次。本程序是绿色单文件，装不装都一样。
foreach ($forbiddenInstallerMarker in @("7zS2.sfx", "installer-smoke-install", "-setup.exe")) {
    if ($packageSource.Contains($forbiddenInstallerMarker)) {
        throw ("Packaging regression failed: the one-click SFX installer is back ($forbiddenInstallerMarker). " +
            "It trips the Windows Program Compatibility Assistant on every run; ship the portable " +
            "executable instead.")
    }
}
foreach ($requiredPortableMarker in @(
    '$standaloneExe = Join-Path $OutputDirectory "YeImageViewer.exe"',
    'Copy-Item -LiteralPath $viewer -Destination (Join-Path $stagingRoot "YeImageViewer.exe")',
    "使用说明.txt"
)) {
    if (-not $packageSource.Contains($requiredPortableMarker)) {
        throw "Packaging regression failed: the portable package no longer ships $requiredPortableMarker."
    }
}
# 包里不能再把 exe 埋进 x64\Release\：解压出来要能直接看到并双击
if ($packageSource.Contains('Join-Path $stagingRoot "x64\Release"')) {
    throw ("Packaging regression failed: the portable package buries the executable under " +
        "x64\Release again; it must sit at the archive root.")
}
if (Test-Path -LiteralPath (Join-Path $repoRoot "tools\installer\7zS2.sfx")) {
    throw ("Packaging regression failed: the SFX installer module is back in tools\installer; " +
        "it is what triggers the compatibility-assistant dialog.")
}
Write-Host "PASS the release ships a portable executable, with no SFX installer to trip the compatibility assistant."

$embeddedIcon = [Drawing.Icon]::ExtractAssociatedIcon($viewer)
if ($null -eq $embeddedIcon) {
    throw "Viewer executable does not expose an embedded application icon."
}
try {
    $embeddedIconBitmap = $embeddedIcon.ToBitmap()
    try {
        $visibleIconPixels = 0
        $warmIconPixels = 0
        $greenIconPixels = 0
        for ($iconY = 0; $iconY -lt $embeddedIconBitmap.Height; $iconY++) {
            for ($iconX = 0; $iconX -lt $embeddedIconBitmap.Width; $iconX++) {
                $iconPixel = $embeddedIconBitmap.GetPixel($iconX, $iconY)
                if ($iconPixel.A -gt 16) {
                    $visibleIconPixels++
                    if ($iconPixel.R -gt 180 -and $iconPixel.G -gt 90 -and $iconPixel.B -lt 120) {
                        $warmIconPixels++
                    }
                    if ($iconPixel.G -gt $iconPixel.R -and $iconPixel.G -gt $iconPixel.B) {
                        $greenIconPixels++
                    }
                }
            }
        }
        if ($visibleIconPixels -lt 100 -or $warmIconPixels -lt 15 -or $greenIconPixels -lt 15) {
            throw "Viewer embedded icon does not contain the expected transparent flower-frame artwork."
        }
    }
    finally {
        $embeddedIconBitmap.Dispose()
    }
}
finally {
    $embeddedIcon.Dispose()
}
Write-Host "PASS viewer embeds the new transparent flower-frame application icon."

$expectedCommitId = (& git -C $repoRoot rev-parse --short=12 HEAD).Trim()
$viewerAscii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($viewer))
if (-not $viewerAscii.Contains($expectedCommitId)) {
    throw "Viewer build metadata does not contain current commit ID $expectedCommitId."
}
Write-Host "PASS viewer embeds current commit ID $expectedCommitId."

# 跑起来也看不出来的源码约定（三语字符串表、界面语言判断、LRU 关停顺序、格式清单）
# 抽成了独立脚本：提交前可以单独跑它自检，只读源码，几秒钟出结果。
# 它自己 throw，这里直接调，不必再转一手退出码。
& (Join-Path $repoRoot "scripts\check-source-invariants.ps1") -RepoRoot $repoRoot

Write-Host "Running unit regression tests..."
$expectedHdrHash = "1A1A661E0A22BECBE019B6C095004315351F28600D9BD7600BD933BEB351E5D5"
$actualHdrHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $hdrFixture).Hash
if ($actualHdrHash -ne $expectedHdrHash) {
    throw "HDR regression fixture hash mismatch: expected $expectedHdrHash, got $actualHdrHash."
}

$expectedSharpSvgHash = "86F5955DB6C420148EE317189D40E394DAC9999F54BB038EF744F012BFFB3759"
$actualSharpSvgHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sharpSvgFixture).Hash
if ($actualSharpSvgHash -ne $expectedSharpSvgHash) {
    throw "Sharp SVG regression fixture hash mismatch: expected $expectedSharpSvgHash, got $actualSharpSvgHash."
}

$expectedTextSvgHash = "52DFB4983923CA525D8B92A78122D3ADB75F72DBAFFBAA40DB2CCBD6B7E5FE08"
$actualTextSvgHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $textSvgFixture).Hash
if ($actualTextSvgHash -ne $expectedTextSvgHash) {
    throw "Text SVG regression fixture hash mismatch: expected $expectedTextSvgHash, got $actualTextSvgHash."
}

$expectedJaggedHash = "14FD50F84BCD0576FB55D3C34848B840C808F141C9009BF01A2FED372742BF10"
$actualJaggedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $jaggedFixture).Hash
if ($actualJaggedHash -ne $expectedJaggedHash) {
    throw "Jagged-image regression fixture hash mismatch: expected $expectedJaggedHash, got $actualJaggedHash."
}

$expectedAppIconPreviewHash = "303A1B987DD5B41628F0A4747C6F381F089FDF79E6CD212F52D539A06B73871F"
$actualAppIconPreviewHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $appIconPreview).Hash
if ($actualAppIconPreviewHash -ne $expectedAppIconPreviewHash) {
    throw "Application icon preview hash mismatch: expected $expectedAppIconPreviewHash, got $actualAppIconPreviewHash."
}

$expectedAppIconHash = "5F34314E4902D2972588F75823AFE26D244A4CE816E48A00BA40C8B9ED460E43"
foreach ($appIcon in $appIcons) {
    $actualAppIconHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $appIcon).Hash
    if ($actualAppIconHash -ne $expectedAppIconHash) {
        throw "Application icon hash mismatch for ${appIcon}: expected $expectedAppIconHash, got $actualAppIconHash."
    }
}

# 缩略图组件（随程序交付的三个文件之一）直接 LoadLibrary 构建产物里那份 DLL 来测，
# 不依赖注册表也不受 shell 缩略图缓存干扰。素材挑的是「Windows 自己不认、只能靠我们」
# 的那些格式——这些才是装了本程序才看得到缩略图的。
$thumbnailProviderDll = Join-Path $releaseDir "YeThumbnailProvider.dll"
if (-not (Test-Path -LiteralPath $thumbnailProviderDll -PathType Leaf)) {
    throw "Thumbnail provider DLL is missing: $thumbnailProviderDll"
}
$thumbnailFixtures = @(
    # 和 common.png 同源的 160x80 参考图，用来逐像素比对（通道顺序搞反这种错，
    # 只看「有没有出图」是看不出来的）
    (Join-Path $formatCorpusRoot "files\common.png"),
    (Join-Path $formatCorpusRoot "files\common.tga"),
    (Join-Path $formatCorpusRoot "files\common.ras"),
    (Join-Path $formatCorpusRoot "files\common.sr"),
    (Join-Path $formatCorpusRoot "files\common.pcx"),
    # 其余有仓库内素材的注册扩展，一个不落
    (Join-Path $formatCorpusRoot "files\common.qoi"),
    (Join-Path $formatCorpusRoot "files\common.pbm"),
    (Join-Path $formatCorpusRoot "files\common.pgm"),
    (Join-Path $formatCorpusRoot "files\common.ppm"),
    (Join-Path $formatCorpusRoot "files\common.pnm"),
    (Join-Path $formatCorpusRoot "files\common.pxm"),
    (Join-Path $formatCorpusRoot "files\common.pfm"),
    (Join-Path $formatCorpusRoot "files\common.pic"),
    (Join-Path $formatCorpusRoot "files\common.hdr"),
    (Join-Path $formatCorpusRoot "files\common.dds"),
    (Join-Path $formatCorpusRoot "files\common.jxr"),
    (Join-Path $formatCorpusRoot "files\common.psdt"),
    (Join-Path $formatCorpusRoot "files\libavif-static.avif"),
    (Join-Path $formatCorpusRoot "files\libavif-animated.avifs"),
    (Join-Path $formatCorpusRoot "files\libheif-rainbow.heic"),
    (Join-Path $formatCorpusRoot "files\libheif-rainbow.heif"),
    (Join-Path $formatCorpusRoot "files\libjxl-static.jxl"),
    (Join-Path $formatCorpusRoot "files\libjxl-animated.jxl"),
    (Join-Path $formatCorpusRoot "files\bundled-codec.wp2"),
    (Join-Path $formatCorpusRoot "files\blp-dxt1.blp"),
    (Join-Path $formatCorpusRoot "files\animated.apng"),
    (Join-Path $formatCorpusRoot "files\generated-live-photo.livp"),
    (Join-Path $repoRoot "test\corpus\02-professional\psd_8bit.psd"),
    (Join-Path $repoRoot "test\corpus\02-professional\psd_alpha.psd"),
    (Join-Path $repoRoot "test\corpus\02-professional\psd_16bit.psd"),
    (Join-Path $repoRoot "test\corpus\02-professional\pfm_gray.pfm"),
    # 已知解不出来的也要留在清单里：测试会确认它们仍然「按文档那样不出图」，
    # 哪天补上了解码器，测试会提醒把记录删掉（见 main.cpp 的 thumbnailKnownGaps）
    (Join-Path $formatCorpusRoot "files\common.jp2"),
    (Join-Path $formatCorpusRoot "files\opencv-float.exr"),
    (Join-Path $repoRoot "test\corpus\02-professional\exr_color.exr"),
    (Join-Path $repoRoot "test\corpus\02-professional\exr_alpha.exr"),
    $sharpSvgFixture
)
foreach ($thumbnailFixture in $thumbnailFixtures) {
    if (-not (Test-Path -LiteralPath $thumbnailFixture -PathType Leaf)) {
        throw "Thumbnail regression fixture is missing: $thumbnailFixture"
    }
}

& $unitTests $hdrFixture $sharpSvgFixture $textSvgFixture @toolbarIcons @appIcons `
    $thumbnailProviderDll @thumbnailFixtures
if ($LASTEXITCODE -ne 0) {
    throw "Unit regression tests failed with exit code $LASTEXITCODE."
}

Write-Host "Running real format decode corpus..."
$formatCases = @(Import-Csv -LiteralPath $formatManifest -Delimiter "`t")
if ($formatCases.Count -lt 40) {
    throw "Format corpus is unexpectedly small: only $($formatCases.Count) registered cases."
}

$declaredStaticExtensions = @(
    "apng", "avif", "avifs", "blp", "bmp", "dds", "dib", "exr", "gif", "hdr",
    "heic", "heif", "ico", "icon", "jfif", "jp2", "jpe", "jpeg", "jpg", "jxl",
    "jxr", "lep", "livp", "pbm", "pcx", "pfm", "pgm", "pic", "png", "pnm", "ppm",
    "psd", "psdt", "pxm", "qoi", "ras", "sr", "svg", "tga", "tif", "tiff", "webm",
    "webp", "wp2"
)
$temporarilyExemptExtensions = @("lep")
$coveredExtensions = @($formatCases | ForEach-Object {
    [IO.Path]::GetExtension($_.File).TrimStart(".").ToLowerInvariant()
} | Sort-Object -Unique)
$missingExtensions = @($declaredStaticExtensions | Where-Object {
    $_ -notin $coveredExtensions -and $_ -notin $temporarilyExemptExtensions
})
if ($missingExtensions.Count -ne 0) {
    throw "Format corpus does not cover declared extensions: $($missingExtensions -join ', ')."
}

$formatProbeDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Format-Probes-" + [Guid]::NewGuid().ToString("N"))
[void](New-Item -ItemType Directory -Path $formatProbeDirectory)
try {
    foreach ($case in $formatCases) {
        $fixture = [IO.Path]::GetFullPath((Join-Path $formatCorpusRoot $case.File))
        $repoBoundary = [IO.Path]::GetFullPath($repoRoot) + [IO.Path]::DirectorySeparatorChar
        if (-not $fixture.StartsWith($repoBoundary, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Format fixture escapes the repository: $($case.File)"
        }
        if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) {
            throw "Format fixture is missing: $fixture"
        }

        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $fixture).Hash
        if ($actualHash -ne $case.SHA256) {
            throw "Format fixture hash mismatch for $($case.File): expected $($case.SHA256), got $actualHash."
        }

        $probeResult = Join-Path $formatProbeDirectory (([Guid]::NewGuid().ToString("N")) + ".tsv")
        $probeProcess = Start-Process -FilePath $viewer -ArgumentList @(
            "--decode-probe", ('"' + $fixture + '"'), ('"' + $probeResult + '"')
        ) -WindowStyle Hidden -Wait -PassThru
        if ($probeProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $probeResult)) {
            throw "Real decoder rejected $($case.File) with exit code $($probeProcess.ExitCode)."
        }

        # 前五列（状态 宽 高 帧 类型）是稳定契约，后面追加的诊断列（通道数、最小/平均
        # alpha、Mat 深度、各通道均值）按需增补，这里只要求"至少五列"。
        # 原先写死 -ne 5，新增一列就会把这整套回归判成失败，而失败原因跟被测行为无关。
        $probeFields = @((Get-Content -LiteralPath $probeResult -Raw).Trim() -split "`t")
        if ($probeFields.Count -lt 5 -or $probeFields[0] -ne "OK") {
            throw "Invalid decoder-probe result for $($case.File): $($probeFields -join '|')"
        }
        $actualWidth = [int]$probeFields[1]
        $actualHeight = [int]$probeFields[2]
        $actualFrames = [int]$probeFields[3]
        $actualKind = $probeFields[4]
        if ($actualWidth -ne [int]$case.Width -or
            $actualHeight -ne [int]$case.Height -or
            $actualFrames -lt [int]$case.MinimumFrames -or
            $actualKind -ne $case.Kind) {
            throw "Unexpected decode result for $($case.File): $actualWidth x $actualHeight, $actualFrames frames, $actualKind."
        }
    }
}
finally {
    if (Test-Path -LiteralPath $formatProbeDirectory) {
        [IO.Directory]::Delete($formatProbeDirectory, $true)
    }
}
Write-Host "PASS $($formatCases.Count) real format fixtures decode through the production loader."
Write-Host "PASS format corpus covers every declared static extension except documented LEP."

# 实况照片：谷歌 MicroVideo、苹果 .livp、同名 .mov 配对三种封装走三条不同的加载路径，
# 都要解出完整画面、按时间戳算出的每帧时长和声音，且声音与画面对齐。素材由
# scripts/generate-live-photo-fixtures.ps1 生成：t = 1.000 s 那一帧整帧纯白，同一时刻
# 有一声 1 kHz 响声，其余时间是轻微的 440 Hz 底音。
$livePhotoRoot = Join-Path $repoRoot "test\live-photo"
$livePhotoFixtures = @("live-microvideo.jpg", "live-photo.livp", "live-sidecar.jpg")
$liveProbeDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Live-Probes-" + [Guid]::NewGuid().ToString("N"))
[void](New-Item -ItemType Directory -Path $liveProbeDirectory)
try {
    foreach ($liveFixtureName in $livePhotoFixtures) {
        $liveFixture = Join-Path $livePhotoRoot $liveFixtureName
        if (-not (Test-Path -LiteralPath $liveFixture -PathType Leaf)) {
            throw "Live photo fixture is missing: $liveFixture"
        }
        $liveResult = Join-Path $liveProbeDirectory ($liveFixtureName + ".tsv")
        $liveDump = Join-Path $liveProbeDirectory ($liveFixtureName + ".dump")
        $liveProbe = Start-Process -FilePath $viewer -ArgumentList @(
            "--decode-probe", ('"' + $liveFixture + '"'), ('"' + $liveResult + '"'), ('"' + $liveDump + '"')
        ) -WindowStyle Hidden -Wait -PassThru
        if ($liveProbe.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $liveResult)) {
            throw "Live photo regression failed: $liveFixtureName was rejected with exit code $($liveProbe.ExitCode)."
        }

        # 第 4 列帧数、第 5 列类型；第 11~14 列：实况总时长、采样率、声道数、音频时长。
        # 90 帧而不是八十几帧：解码器结尾要冲刷，否则多线程解码压住的最后几帧会丢。
        $liveFields = @((Get-Content -LiteralPath $liveResult -Raw).Trim() -split "`t")
        if ($liveFields.Count -lt 14 -or $liveFields[0] -ne "OK" -or $liveFields[4] -ne "animated" -or
            [int]$liveFields[3] -ne 90) {
            throw "Live photo regression failed: $liveFixtureName decoded as $($liveFields -join '|'), expected 90 animated frames."
        }
        $motionMs = [int]$liveFields[10]
        $audioRate = [int]$liveFields[11]
        $audioChannels = [int]$liveFields[12]
        $audioMs = [int]$liveFields[13]
        if ([Math]::Abs($motionMs - 3000) -gt 5 -or $audioRate -ne 48000 -or $audioChannels -ne 2 -or
            [Math]::Abs($audioMs - 3000) -gt 5) {
            throw "Live photo regression failed: $liveFixtureName has motion $motionMs ms, audio $audioRate Hz x $audioChannels, $audioMs ms."
        }

        # 音画对齐：最亮那一帧的起点，与响声（幅度超过满量程 40%）的起点比较
        $liveFrames = @(Import-Csv -LiteralPath (Join-Path $liveDump "frames.csv"))
        $flashFrame = $liveFrames | Sort-Object { [double]$_.meanLuma } -Descending | Select-Object -First 1
        $flashStartMs = [int]$flashFrame.startMs
        if ([int]$flashFrame.index -ne 30 -or [Math]::Abs($flashStartMs - 1000) -gt 5) {
            throw "Live photo regression failed: $liveFixtureName flash frame is #$($flashFrame.index) at $flashStartMs ms, expected #30 at 1000 ms."
        }

        $wav = [IO.File]::ReadAllBytes((Join-Path $liveDump "audio.wav"))
        $wavChannels = [BitConverter]::ToUInt16($wav, 22)
        $wavRate = [BitConverter]::ToUInt32($wav, 24)
        $chunkOffset = 12
        $dataOffset = -1
        $dataLength = 0
        while ($chunkOffset + 8 -le $wav.Length) {
            $chunkId = [Text.Encoding]::ASCII.GetString($wav, $chunkOffset, 4)
            $chunkSize = [BitConverter]::ToUInt32($wav, $chunkOffset + 4)
            if ($chunkId -eq "data") {
                $dataOffset = $chunkOffset + 8
                $dataLength = [Math]::Min([int64]$chunkSize, [int64]($wav.Length - $dataOffset))
                break
            }
            $chunkOffset += 8 + $chunkSize
        }
        if ($dataOffset -lt 0) {
            throw "Live photo regression failed: the audio dump of $liveFixtureName has no data chunk."
        }
        $beepThreshold = 0.4 * 32767
        $frameBytes = 2 * $wavChannels
        $beepOnsetMs = -1.0
        for ($sampleOffset = 0; $sampleOffset + 1 -lt $dataLength; $sampleOffset += $frameBytes) {
            if ([Math]::Abs([BitConverter]::ToInt16($wav, $dataOffset + $sampleOffset)) -gt $beepThreshold) {
                $beepOnsetMs = ($sampleOffset / $frameBytes) * 1000.0 / $wavRate
                break
            }
        }
        if ($beepOnsetMs -lt 0 -or [Math]::Abs($beepOnsetMs - $flashStartMs) -gt 15) {
            throw "Live photo regression failed: $liveFixtureName sound starts at $beepOnsetMs ms but the flash frame at $flashStartMs ms."
        }
    }
}
finally {
    if (Test-Path -LiteralPath $liveProbeDirectory) {
        [IO.Directory]::Delete($liveProbeDirectory, $true)
    }
}
Write-Host "PASS live photos decode every frame with real timing and in-sync sound from .livp, MicroVideo and sidecar packaging."

if (-not ("YeImageViewerTestNativeV1365" -as [type])) {
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class YeImageViewerTestNativeV1365
{
    public delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct MONITORINFO
    {
        public int Size;
        public RECT Monitor;
        public RECT Work;
        public uint Flags;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct GUITHREADINFO
    {
        public int Size;
        public uint Flags;
        public IntPtr Active;
        public IntPtr Focus;
        public IntPtr Capture;
        public IntPtr MenuOwner;
        public IntPtr MoveSize;
        public IntPtr Caret;
        public RECT CaretRect;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

    // 用来探「窗口现在能不能回消息」：句柄出现不等于消息泵已经跑起来了
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessageTimeout(IntPtr window, uint message,
        UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);

    [DllImport("user32.dll")]
    public static extern bool IsZoomed(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr window, int controlId);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, IntPtr processId);

    [DllImport("user32.dll", EntryPoint = "GetWindowThreadProcessId")]
    public static extern uint GetWindowThreadProcessIdForEnum(IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    public static extern bool GetGUIThreadInfo(uint threadId, ref GUITHREADINFO info);

    [DllImport("imm32.dll")]
    public static extern IntPtr ImmGetDefaultIMEWnd(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr window, StringBuilder className, int maximumLength);

    public static IntPtr FindProcessWindow(uint processId, string expectedClassName)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr parameter) {
            uint ownerProcessId;
            GetWindowThreadProcessIdForEnum(window, out ownerProcessId);
            if (ownerProcessId == processId) {
                StringBuilder className = new StringBuilder(256);
                GetClassName(window, className, className.Capacity);
                if (className.ToString() == expectedClassName && IsWindowVisible(window)) {
                    found = window;
                    return false;
                }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr window, out RECT rect);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr dpiContext);

    [DllImport("user32.dll")]
    public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

    [DllImport("user32.dll")]
    public static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr window, StringBuilder text, int maximumLength);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool SetDlgItemText(IntPtr window, int controlId, string text);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern bool SetWindowText(IntPtr window, string text);

    [DllImport("user32.dll")]
    public static extern int GetDlgCtrlID(IntPtr window);

    public static IntPtr FindDescendant(uint processId, IntPtr parent,
        string expectedClassName, int expectedControlId)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindowsCallback callback = null;
        callback = delegate(IntPtr window, IntPtr parameter) {
            StringBuilder className = new StringBuilder(256);
            GetClassName(window, className, className.Capacity);
            if (className.ToString() == expectedClassName &&
                GetDlgCtrlID(window) == expectedControlId) {
                found = window;
                return false;
            }
            return true;
        };
        EnumChildWindows(parent, callback, IntPtr.Zero);
        return found;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent,
        EnumWindowsCallback callback, IntPtr parameter);

    // 窗口的深色模式标记。切主题时程序会给主窗口设 DWMWA_USE_IMMERSIVE_DARK_MODE(20)，
    // 读回来就知道主题到底切过去了没有——这是少有的「界面主题」能在窗口外部验证的地方。
    [DllImport("dwmapi.dll")]
    public static extern int DwmGetWindowAttribute(IntPtr window, int attribute,
        out int value, int size);

    public static int DarkModeFlag(IntPtr window)
    {
        int value = -1;
        if (DwmGetWindowAttribute(window, 20, out value, sizeof(int)) != 0)
            return -1;
        return value;
    }

    // 剪贴板里那张图的尺寸。直接读 CF_DIB 的 BITMAPINFOHEADER，不碰 WinForms：
    // PowerShell 7 没有 Get-Clipboard -Format Image，而 WinForms 的 Clipboard
    // 还要求调用线程是 STA，换个宿主就可能拿不到。
    [DllImport("user32.dll")]
    public static extern bool OpenClipboard(IntPtr owner);

    [DllImport("user32.dll")]
    public static extern bool CloseClipboard();

    [DllImport("user32.dll")]
    public static extern bool IsClipboardFormatAvailable(uint format);

    [DllImport("user32.dll")]
    public static extern IntPtr GetClipboardData(uint format);

    [DllImport("kernel32.dll")]
    public static extern IntPtr GlobalLock(IntPtr handle);

    [DllImport("kernel32.dll")]
    public static extern bool GlobalUnlock(IntPtr handle);

    // 剪贴板的「版本号」。每次有人写剪贴板它就加一，用它判断程序到底写没写，
    // 比「内容变了没」可靠：内容可能正好和上次一样。
    [DllImport("user32.dll")]
    public static extern uint GetClipboardSequenceNumber();

    public static string ClipboardText()
    {
        const uint CF_UNICODETEXT = 13;
        // 剪贴板是全机器共享的，别人正占着就等一会儿再试
        for (int attempt = 0; attempt < 20; attempt++)
        {
            if (OpenClipboard(IntPtr.Zero))
            {
                try
                {
                    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
                        return "";

                    IntPtr handle = GetClipboardData(CF_UNICODETEXT);
                    if (handle == IntPtr.Zero)
                        return "";

                    IntPtr text = GlobalLock(handle);
                    if (text == IntPtr.Zero)
                        return "";
                    try
                    {
                        return Marshal.PtrToStringUni(text);
                    }
                    finally
                    {
                        GlobalUnlock(handle);
                    }
                }
                finally
                {
                    CloseClipboard();
                }
            }
            System.Threading.Thread.Sleep(50);
        }
        return "";
    }

    public static string ClipboardImageSize()
    {
        const uint CF_DIB = 8;
        // 剪贴板是全机器共享的，别人正占着就等一会儿再试
        for (int attempt = 0; attempt < 20; attempt++)
        {
            if (OpenClipboard(IntPtr.Zero))
            {
                try
                {
                    if (!IsClipboardFormatAvailable(CF_DIB))
                        return "";

                    IntPtr handle = GetClipboardData(CF_DIB);
                    if (handle == IntPtr.Zero)
                        return "";

                    IntPtr bits = GlobalLock(handle);
                    if (bits == IntPtr.Zero)
                        return "";
                    try
                    {
                        // BITMAPINFOHEADER: biSize(0) biWidth(4) biHeight(8)
                        int width = Marshal.ReadInt32(bits, 4);
                        int height = Marshal.ReadInt32(bits, 8);
                        // 自下而上的 DIB 高度是负数
                        return width.ToString() + "x" + Math.Abs(height).ToString();
                    }
                    finally
                    {
                        GlobalUnlock(handle);
                    }
                }
                finally
                {
                    CloseClipboard();
                }
            }
            System.Threading.Thread.Sleep(50);
        }
        return "";
    }


}
"@
}

# 「点图片外的空白处」用的坐标不能写死。窗口是满工作区的、图片居中缩放，左右留白
# 和上下留白通常只有一边非零——写死 x=4 时，一张足够宽的图会让那个点正好落在画面上，
# 点下去不退出沉浸，于是等到超时假失败。这件事在三个环节里都要做，所以抽出来。
#
# 标题形如：[5/5] 名字.svg 1200x1600(1.2MB) 45%
function Get-ViewerBackgroundPoint {
    param(
        [Parameter(Mandatory = $true)] [IntPtr]$Window,
        [Parameter(Mandatory = $true)] [string]$Title
    )

    $sizeMatch = [regex]::Match($Title, '(\d+)x(\d+)')
    $zoomMatch = [regex]::Match($Title, '(\d+)%')
    if (-not $sizeMatch.Success -or -not $zoomMatch.Success) {
        throw "Cannot find background: the title does not report a pixel size and zoom ('$Title')."
    }

    $clientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($Window, [ref]$clientRect)
    $clientWidth = $clientRect.Right - $clientRect.Left
    $clientHeight = $clientRect.Bottom - $clientRect.Top

    # 标题里的百分比是取整过的，画面尺寸按它算会差一两个像素，所以留白要留余量
    $drawnWidth = [int][Math]::Ceiling([int]$sizeMatch.Groups[1].Value * [double]$zoomMatch.Groups[1].Value / 100.0)
    $drawnHeight = [int][Math]::Ceiling([int]$sizeMatch.Groups[2].Value * [double]$zoomMatch.Groups[1].Value / 100.0)
    $sideGutter = [int](($clientWidth - $drawnWidth) / 2)
    $topGutter = [int](($clientHeight - $drawnHeight) / 2)

    if ($sideGutter -ge 12) {
        return [IntPtr](([int]($clientHeight / 2) -shl 16) -bor 4)
    }
    if ($topGutter -ge 12) {
        return [IntPtr]((4 -shl 16) -bor ([int]($clientWidth / 2) -band 0xFFFF))
    }
    throw ("Cannot find background: the image fills the whole work area " +
        "(${drawnWidth}x${drawnHeight} in ${clientWidth}x${clientHeight}). " +
        "Pick a fixture whose aspect ratio differs from the monitor's.")
}

# Match the viewer's per-monitor-v2 coordinate space before reading client
# rectangles or synthesizing mouse messages on scaled displays.
[void][YeImageViewerTestNativeV1365]::SetThreadDpiAwarenessContext([IntPtr](-4))

# Mirrors OverlayLayout::toolbarScale / OverlayLayout::scaled. The viewer is
# PerMonitorHighDPIAware, so the overlay sizes itself by DPI and only shrinks
# when the window is too narrow; these helpers keep the synthetic click
# coordinates on the same geometry the viewer actually draws.
function Get-ToolbarScale {
    param([int]$CanvasWidth, [int]$Dpi)
    if ($Dpi -le 0) { $Dpi = 96 }
    $target = [int][Math]::Floor($Dpi * 1000 / 96)
    $minimum = [int][Math]::Floor(400 * $target / 1000)
    if ($CanvasWidth -le 16) {
        return [int][Math]::Floor(600 * $target / 1000)
    }
    $widthScale = [int][Math]::Floor(($CanvasWidth - 16) * 1000 / 615)
    $value = [Math]::Min($widthScale, $target)
    return [Math]::Min([Math]::Max($value, $minimum), $target)
}

function Get-ScaledValue {
    param([int]$Value, [int]$Scale)
    return [Math]::Max(1, [int][Math]::Floor(($Value * $Scale + 500) / 1000))
}

Write-Host "Checking functional startup open button..."
$homeDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Home-" + [Guid]::NewGuid().ToString("N"))
$homeViewer = Join-Path $homeDirectory "YeImageViewer.exe"
$homeProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $homeDirectory)
    Copy-Item -LiteralPath $viewer -Destination $homeViewer
    $homeProcess = Start-Process -FilePath $homeViewer -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 100
        $homeProcess.Refresh()
    } while (-not $homeProcess.HasExited -and $homeProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $deadline)
    if ($homeProcess.HasExited -or $homeProcess.MainWindowHandle -eq 0) {
        throw "Startup-action regression failed: viewer did not open without an image argument."
    }

    $homeWindow = [IntPtr]$homeProcess.MainWindowHandle
    $homeClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($homeWindow, [ref]$homeClientRect)
    $homeClientWidth = $homeClientRect.Right - $homeClientRect.Left
    $homeClientHeight = $homeClientRect.Bottom - $homeClientRect.Top
    $homeScale = [Math]::Min($homeClientWidth / 500.0, $homeClientHeight / 350.0)
    $homeRenderedWidth = [int][Math]::Round(500 * $homeScale)
    $homeRenderedHeight = [int][Math]::Round(350 * $homeScale)
    $homeLeft = [int][Math]::Round(($homeClientWidth - $homeRenderedWidth) / 2.0)
    $homeTop = [int][Math]::Round(($homeClientHeight - $homeRenderedHeight) / 2.0)
    # OPEN_BUTTON is {36,92,428,66}; click its source-space center.
    $homeOpenX = $homeLeft + [int][Math]::Round(250 * $homeScale)
    $homeOpenY = $homeTop + [int][Math]::Round(125 * $homeScale)
    $homeOpenPosition = [IntPtr](($homeOpenY -shl 16) -bor ($homeOpenX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $homeWindow, 0x0201, [UIntPtr]1, $homeOpenPosition)

    $dialogDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $homeDialog = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$homeProcess.Id, "#32770")
    } while ($homeDialog -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $dialogDeadline)
    if ($homeDialog -eq [IntPtr]::Zero -or
        [YeImageViewerTestNativeV1365]::IsWindowEnabled($homeWindow)) {
        throw "Startup-action regression failed: clicking Open image did not show a modal file picker."
    }
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $homeDialog, 0x0111, [UIntPtr]2, [IntPtr]::Zero)
    $restoreDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $homeDialog = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$homeProcess.Id, "#32770")
    } while (($homeDialog -ne [IntPtr]::Zero -or
        -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($homeWindow)) -and
        [DateTime]::UtcNow -lt $restoreDeadline)
    if ($homeDialog -ne [IntPtr]::Zero -or
        -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($homeWindow)) {
        throw "Startup-action regression failed: closing the file picker did not restore viewer interaction."
    }
    Write-Host "PASS startup uses a real Open image button and restores interaction after picker dismissal."
}
finally {
    if ($homeProcess -and -not $homeProcess.HasExited) {
        [void]$homeProcess.CloseMainWindow()
        if (-not $homeProcess.WaitForExit(3000)) {
            Stop-Process -Id $homeProcess.Id -Force
            $homeProcess.WaitForExit()
        }
    }
    if (Test-Path -LiteralPath $homeDirectory) {
        [IO.Directory]::Delete($homeDirectory, $true)
    }
}

Write-Host "Checking the zoom and window commands in a real window..."
# 这四个命令（适应窗口 / 实际大小 / 适应图片 / 沉浸显示）的规则在 ZoomPolicy.h 里
# 有单元测试，但「窗口和画面对不对得上」只能在真窗口里量。踩过的坑：
#   适应图片只改了窗口没改缩放，窗口放大了画面没跟上，四周露出一圈棋盘格；
#   预览图按缩略图尺寸算缩放，换成真图那一刻画面突然缩小三倍（SVG 上差三倍）。
$zoomTestDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Zoom-" + [Guid]::NewGuid().ToString("N"))
$zoomProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $zoomTestDirectory)
    $zoomViewer = Join-Path $zoomTestDirectory "YeImageViewer.exe"
    Copy-Item -LiteralPath $viewer -Destination $zoomViewer

    # 用一张比屏幕还大的图：「适应图片」要把窗口按图片的宽高比撑到工作区上限，
    # 缩放同时定成同一个比例，客户区正好等于画面，一个空白像素都不该有。
    #
    # 不能拿小图验这一条：窗口有 400x300 的最小尺寸（见 D3D11App 的
    # WM_GETMINMAXINFO），比它还小的图无论如何也贴合不到那么小，剩下的地方
    # 必然是背景——那是约束的正确表现，不是留白缺陷。
    $zoomImage = Join-Path $zoomTestDirectory "fit.png"
    Copy-Item -LiteralPath (Join-Path $repoRoot "test\corpus\13-dimensions\4095x4097.png") `
        -Destination $zoomImage

    $zoomProcess = Start-Process -FilePath $zoomViewer -ArgumentList ('"' + $zoomImage + '"') -PassThru
    $zoomDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 150
        $zoomProcess.Refresh()
    } while (-not $zoomProcess.HasExited -and $zoomProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $zoomDeadline)
    if ($zoomProcess.HasExited -or $zoomProcess.MainWindowHandle -eq 0) {
        throw "Zoom-command regression failed: the viewer did not open a window."
    }
    $zoomWindow = [IntPtr]$zoomProcess.MainWindowHandle
    Start-Sleep -Milliseconds 1200

    # 先退回带边框窗口（启动默认是沉浸预览），工具栏才量得到
    $zoomClient = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($zoomWindow, [ref]$zoomClient)
    $zoomBackground = [IntPtr](((($zoomClient.Bottom - $zoomClient.Top) / 2) -shl 16) -bor 4)
    foreach ($message in @(0x0200, 0x0201, 0x0202)) {
        [void][YeImageViewerTestNativeV1365]::SendMessage($zoomWindow, $message,
            [UIntPtr]([int]($message -eq 0x0201)), $zoomBackground)
    }
    $framedDeadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 120
        $zoomStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($zoomWindow, -16).ToInt64()
    } while (($zoomStyle -band 0x00C00000) -eq 0 -and [DateTime]::UtcNow -lt $framedDeadline)
    if (($zoomStyle -band 0x00C00000) -eq 0) {
        throw "Zoom-command regression failed: could not get back to a framed window."
    }

    # 点工具栏上的「适应图片」。坐标用和别处一样的换算：工具栏宽 615、高 50、
    # 底边距 20，适应图片的按钮在基准偏移 385、宽 34（见 OverlayLayout.h）。
    # 工具栏缩放不只看 DPI 还看窗口宽度，所以必须用 Get-ToolbarScale，
    # 自己按 DPI 乘一下在窄窗口上会算偏。
    [void][YeImageViewerTestNativeV1365]::GetClientRect($zoomWindow, [ref]$zoomClient)
    $zoomClientWidth = $zoomClient.Right - $zoomClient.Left
    $zoomClientHeight = $zoomClient.Bottom - $zoomClient.Top
    $zoomDpi = [YeImageViewerTestNativeV1365]::GetDpiForWindow($zoomWindow)
    $zoomScale = Get-ToolbarScale -CanvasWidth $zoomClientWidth -Dpi $zoomDpi
    $zoomToolbarWidth = Get-ScaledValue -Value 615 -Scale $zoomScale
    $zoomToolbarHeight = Get-ScaledValue -Value 50 -Scale $zoomScale
    $zoomToolbarBottom = Get-ScaledValue -Value 20 -Scale $zoomScale
    $zoomToolbarLeft = [int][Math]::Floor(($zoomClientWidth - $zoomToolbarWidth) / 2.0)
    $zoomPadding = Get-ScaledValue -Value 8 -Scale $zoomScale
    $zoomButton = Get-ScaledValue -Value 34 -Scale $zoomScale
    $fitImageX = $zoomToolbarLeft + $zoomPadding +
        (Get-ScaledValue -Value 385 -Scale $zoomScale) + [int]($zoomButton / 2)
    $fitImageY = $zoomClientHeight - $zoomToolbarBottom - [int]($zoomToolbarHeight / 2)

    # 工具栏是悬停才显示的，先把鼠标移上去，再点
    $fitImagePosition = [IntPtr](($fitImageY -shl 16) -bor ($fitImageX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($zoomWindow, 0x0200, [UIntPtr]::Zero, $fitImagePosition)
    Start-Sleep -Milliseconds 250
    [void][YeImageViewerTestNativeV1365]::SendMessage($zoomWindow, 0x0201, [UIntPtr]1, $fitImagePosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($zoomWindow, 0x0202, [UIntPtr]::Zero, $fitImagePosition)

    # 点击进的是操作队列，等窗口真的变过去（尺寸不再变化）为止
    $fitDeadline = [DateTime]::UtcNow.AddSeconds(6)
    $fitLastSize = ""
    $fitStableCount = 0
    do {
        Start-Sleep -Milliseconds 150
        [void][YeImageViewerTestNativeV1365]::GetClientRect($zoomWindow, [ref]$zoomClient)
        $fitNowSize = "$($zoomClient.Right - $zoomClient.Left)x$($zoomClient.Bottom - $zoomClient.Top)"
        if ($fitNowSize -eq $fitLastSize) { $fitStableCount++ } else { $fitStableCount = 0 }
        $fitLastSize = $fitNowSize
    } while ($fitStableCount -lt 3 -and [DateTime]::UtcNow -lt $fitDeadline)

    # 客户区必须正好等于「图片尺寸 × 缩放」，差出来的就是四周的留白
    $fitTitle = New-Object Text.StringBuilder 1024
    [void][YeImageViewerTestNativeV1365]::GetWindowText($zoomWindow, $fitTitle, $fitTitle.Capacity)
    $fitTitleText = $fitTitle.ToString()
    if ($fitTitleText -notmatch "(\d+)x(\d+)\([^)]*\)\s+(\d+)%") {
        throw "Zoom-command regression failed: could not read size and zoom from the title '$fitTitleText'."
    }
    $fitImageWidth = [int]$matches[1]
    $fitImageHeight = [int]$matches[2]
    $fitPercent = [int]$matches[3]
    [void][YeImageViewerTestNativeV1365]::GetClientRect($zoomWindow, [ref]$zoomClient)
    $fitClientWidth = $zoomClient.Right - $zoomClient.Left
    $fitClientHeight = $zoomClient.Bottom - $zoomClient.Top
    # 标题里的百分比是取整后的（真实缩放可能是 19.795%，显示成 20%），
    # 所以按「取整前的区间」来判：真实缩放落在 [p-0.5, p+0.5] 之间，
    # 画面尺寸就该落在对应的区间里。留白的 bug 差的是几十上百像素，照样抓得住。
    $fitLowWidth = [Math]::Floor($fitImageWidth * ($fitPercent - 0.5) / 100.0) - 1
    $fitHighWidth = [Math]::Ceiling($fitImageWidth * ($fitPercent + 0.5) / 100.0) + 1
    $fitLowHeight = [Math]::Floor($fitImageHeight * ($fitPercent - 0.5) / 100.0) - 1
    $fitHighHeight = [Math]::Ceiling($fitImageHeight * ($fitPercent + 0.5) / 100.0) + 1
    if ($fitClientWidth -lt $fitLowWidth -or $fitClientWidth -gt $fitHighWidth -or
        $fitClientHeight -lt $fitLowHeight -or $fitClientHeight -gt $fitHighHeight) {
        throw ("Zoom-command regression failed: fit-window-to-image left a border. " +
            "client=${fitClientWidth}x${fitClientHeight} image=${fitImageWidth}x${fitImageHeight} " +
            "zoom=${fitPercent}% expected width ${fitLowWidth}..${fitHighWidth} " +
            "height ${fitLowHeight}..${fitHighHeight}")
    }
    # 这张图比屏幕大，贴合之后必然小于 100%
    if ($fitPercent -ge 100) {
        throw "Zoom-command regression failed: an image larger than the screen should shrink, got ${fitPercent}%."
    }
    # 窗口要保持图片的宽高比，不能被工作区的宽高比带偏
    $fitSourceAspect = $fitImageWidth / $fitImageHeight
    $fitWindowAspect = $fitClientWidth / $fitClientHeight
    if ([Math]::Abs($fitSourceAspect - $fitWindowAspect) / $fitSourceAspect -gt 0.03) {
        throw ("Zoom-command regression failed: the fitted window lost the image aspect ratio. " +
            "image=${fitImageWidth}x${fitImageHeight} client=${fitClientWidth}x${fitClientHeight}")
    }
    Write-Host "PASS fit-window-to-image leaves no border (client ${fitClientWidth}x${fitClientHeight} at ${fitPercent}%)."

    [void]$zoomProcess.CloseMainWindow()
    if (-not $zoomProcess.WaitForExit(4000)) { Stop-Process -Id $zoomProcess.Id -Force }
    $zoomProcess = $null

    # ── 预览换真图时画面不能跳 ───────────────────────────────────────────
    # SVG 是最容易暴露的：系统给的缩略图是 995x1024，而 SVG 自己是 280x288。
    # 按缩略图尺寸算缩放的话，换成真图那一刻画面会突然缩小三倍。
    if (Test-Path -LiteralPath $sharpSvgFixture -PathType Leaf) {
        $flashProcess = Start-Process -FilePath $zoomViewer -ArgumentList ('"' + $sharpSvgFixture + '"') -PassThru
        $flashTitles = New-Object Collections.ArrayList
        $flashWatch = [Diagnostics.Stopwatch]::StartNew()
        $flashLast = ""
        while ($flashWatch.Elapsed.TotalSeconds -lt 5) {
            $flashProcess.Refresh()
            if (-not $flashProcess.HasExited -and $flashProcess.MainWindowHandle -ne 0) {
                $flashTitle = New-Object Text.StringBuilder 1024
                [void][YeImageViewerTestNativeV1365]::GetWindowText(
                    [IntPtr]$flashProcess.MainWindowHandle, $flashTitle, $flashTitle.Capacity)
                $flashText = $flashTitle.ToString()
                if ($flashText -ne "" -and $flashText -ne $flashLast) {
                    [void]$flashTitles.Add($flashText)
                    $flashLast = $flashText
                }
            }
            Start-Sleep -Milliseconds 15
        }
        if (-not $flashProcess.HasExited) { Stop-Process -Id $flashProcess.Id -Force }

        # 1x1 是还没拿到任何画面时的占位，不参与比较
        $flashPercents = [System.Collections.ArrayList]::new()
        foreach ($entry in $flashTitles) {
            if ($entry -match "(\d+)x(\d+)" -and $matches[1] -ne "1" -and
                $entry -match "(\d+)%") {
                [void]$flashPercents.Add([int]$matches[1])
            }
        }
        if ($flashPercents.Count -lt 1) {
            throw "Zoom-command regression failed: the SVG never reported a zoom percentage. titles=$($flashTitles -join ' | ')"
        }
        # 用 @() 包起来：Sort-Object -Unique 只剩一项时返回的是标量，没有 Count
        $distinctPercents = @($flashPercents | Sort-Object -Unique)
        if ($distinctPercents.Count -gt 1) {
            throw ("Zoom-command regression failed: the picture jumped while loading " +
                "(zoom went $($distinctPercents -join ' -> ')). titles=$($flashTitles -join ' | ')")
        }
        Write-Host "PASS the picture does not jump when the preview is replaced by the real image."
    }

    # ── 加载期间标题报的是真图尺寸，不是缩略图的 ─────────────────────────
    $bigImage = Join-Path $repoRoot "test\bigimage\moon_81M.png"
    if (Test-Path -LiteralPath $bigImage -PathType Leaf) {
        $sizeProcess = Start-Process -FilePath $zoomViewer -ArgumentList ('"' + $bigImage + '"') -PassThru
        $sizeWatch = [Diagnostics.Stopwatch]::StartNew()
        $firstRealTitle = ""
        while ($sizeWatch.Elapsed.TotalSeconds -lt 6 -and $firstRealTitle -eq "") {
            $sizeProcess.Refresh()
            if (-not $sizeProcess.HasExited -and $sizeProcess.MainWindowHandle -ne 0) {
                $sizeTitle = New-Object Text.StringBuilder 1024
                [void][YeImageViewerTestNativeV1365]::GetWindowText(
                    [IntPtr]$sizeProcess.MainWindowHandle, $sizeTitle, $sizeTitle.Capacity)
                $sizeText = $sizeTitle.ToString()
                if ($sizeText -match "(\d+)x(\d+)" -and $matches[1] -ne "1") {
                    $firstRealTitle = $sizeText
                }
            }
            Start-Sleep -Milliseconds 15
        }
        if (-not $sizeProcess.HasExited) { Stop-Process -Id $sizeProcess.Id -Force }
        if ($firstRealTitle -notmatch "9000x9000") {
            throw ("Zoom-command regression failed: while loading, the title reported the " +
                "thumbnail size instead of the real one. title=$firstRealTitle")
        }
        Write-Host "PASS the title reports the real pixel size from the very first frame."
    }
}
finally {
    if ($null -ne $zoomProcess -and -not $zoomProcess.HasExited) {
        Stop-Process -Id $zoomProcess.Id -Force -ErrorAction SilentlyContinue
    }
    Get-Process -Name "YeImageViewer" -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $zoomViewer } | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    Remove-Item -LiteralPath $zoomTestDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Checking the PNG fast path against OpenCV..."
# 大 PNG 走的是自己写的快路径（整块解压 + 一趟去滤波并写进 Mat），
# 解错了不会崩也不会报错，只会把图画歪。所以拿同一批文件两条路都解一遍，
# 逐字节比对；自检会绕过文件大小门槛，好让语料里的小 PNG 也进来一起比。
$pngSelfTestResult = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-PngFast-" + [Guid]::NewGuid().ToString("N") + ".txt")
try {
    $pngCompared = 0
    foreach ($pngDirectory in @(
        (Join-Path $repoRoot "test\format corpus\files"),
        (Join-Path $repoRoot "test\bigimage"),
        (Join-Path $repoRoot "test\corpus\_local\07-extreme-png"))) {
        if (-not (Test-Path -LiteralPath $pngDirectory -PathType Container)) { continue }
        $pngProcess = Start-Process -FilePath $viewer `
            -ArgumentList @("--png-decode-selftest", ('"' + $pngDirectory + '"'), ('"' + $pngSelfTestResult + '"')) `
            -Wait -PassThru -WindowStyle Hidden
        if ($pngProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $pngSelfTestResult)) {
            throw "PNG fast-path regression failed in $pngDirectory (exit code $($pngProcess.ExitCode))."
        }
        $pngLine = (Get-Content -LiteralPath $pngSelfTestResult -Raw).Trim()
        if (-not ($pngLine.StartsWith("OK") -or $pngLine.StartsWith("SKIPPED"))) {
            throw "PNG fast-path regression failed in $($pngDirectory): $pngLine"
        }
        if ($pngLine -match "compared=(\d+)") { $pngCompared += [int]$matches[1] }
    }
    if ($pngCompared -lt 1) {
        throw "PNG fast-path regression failed: no file exercised the fast path, so nothing was verified."
    }
    Write-Host "PASS the PNG fast path matches OpenCV byte for byte on $pngCompared files."
}
finally {
    Remove-Item -LiteralPath $pngSelfTestResult -Force -ErrorAction SilentlyContinue
}

Write-Host "Checking the color-management fast paths..."
# 色彩管理这一步排在解码之后，直接顶在出图时间上，所以做了两件提速：
# 源和目标同色彩空间时整步跳过，大图按行分块并行。两件事都可能改坏画面，
# 让程序用生产代码自检一遍：恒等要真跳过且一个像素都不动，并行结果要和串行逐字节相同。
$colorSelfTestResult = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Color-" + [Guid]::NewGuid().ToString("N") + ".txt")
try {
    $colorProcess = Start-Process -FilePath $viewer -ArgumentList @("--color-selftest", ('"' + $colorSelfTestResult + '"')) -Wait -PassThru -WindowStyle Hidden
    if ($colorProcess.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $colorSelfTestResult)) {
        throw "Color-management regression failed: the self test did not complete (exit code $($colorProcess.ExitCode))."
    }
    $colorLine = (Get-Content -LiteralPath $colorSelfTestResult -Raw).Trim()
    if (-not $colorLine.StartsWith("OK")) {
        throw "Color-management regression failed: $colorLine"
    }
    foreach ($flag in @("identitySkipped=1", "identityUntouched=1", "transformApplied=1", "parallelMatchesSerial=1")) {
        if ($colorLine -notmatch [regex]::Escape($flag)) {
            throw "Color-management regression failed: expected $flag in '$colorLine'."
        }
    }
    $colorParallelUs = if ($colorLine -match "parallelUs=(\d+)") { [int]$matches[1] } else { 0 }
    $colorSerialUs = if ($colorLine -match "serialUs=(\d+)") { [int]$matches[1] } else { 0 }
    Write-Host ("PASS identity color transforms are skipped and the parallel transform matches the serial one ({0:N1} ms vs {1:N1} ms on 2.3 MP)." -f ($colorParallelUs / 1000.0), ($colorSerialUs / 1000.0))
}
finally {
    Remove-Item -LiteralPath $colorSelfTestResult -Force -ErrorAction SilentlyContinue
}

Write-Host "Checking that settings, editors, targets, and rotations live in one file..."
# 绿色版落地只应该有三个文件：本体、缩略图 DLL、一个配置。
# 外部编辑器、复制/移动目标、图片旋转记录都写在 YeImageViewer.db 的文本区里
# （前 4096 字节仍是固定大小的设置结构体）。旧版本留下的两份独立配置要能迁进来。
$oneConfigDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-OneConfig-" + [Guid]::NewGuid().ToString("N"))
$oneConfigProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $oneConfigDirectory)
    $oneConfigViewer = Join-Path $oneConfigDirectory "YeImageViewer.exe"
    Copy-Item -LiteralPath $viewer -Destination $oneConfigViewer
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $viewer) "YeThumbnailProvider.dll") -Destination (Join-Path $oneConfigDirectory "YeThumbnailProvider.dll")

    $oneConfigImages = Join-Path $oneConfigDirectory "pics"
    [void](New-Item -ItemType Directory -Path $oneConfigImages)
    $oneConfigImage = Join-Path $oneConfigImages "rotated.png"
    Copy-Item -LiteralPath $commonPngFixture -Destination $oneConfigImage

    # 旧版本的两份配置：编辑器 INI 与二进制旋转记录
    $legacyEditors = Join-Path $oneConfigDirectory "YeImageViewer.editors.ini"
    [IO.File]::WriteAllText($legacyEditors,
        "Count=1`r`nName0=LegacyProbe`r`nPath0=$oneConfigViewer`r`n",
        [Text.UTF8Encoding]::new($true))

    $legacyRotations = Join-Path $oneConfigDirectory "YeImageViewer.rotations.db"
    $rotationKey = $oneConfigImage.ToLowerInvariant()
    $rotationStream = [IO.File]::Create($legacyRotations)
    $rotationWriter = New-Object IO.BinaryWriter($rotationStream)
    $rotationWriter.Write([Text.Encoding]::ASCII.GetBytes("YEROT1`r`n"))
    $rotationWriter.Write([uint32]1)
    $rotationWriter.Write([uint32]$rotationKey.Length)
    $rotationWriter.Write([byte]1)
    $rotationWriter.Write([Text.Encoding]::Unicode.GetBytes($rotationKey))
    $rotationWriter.Close()
    $rotationStream.Close()

    $oneConfigProcess = Start-Process -FilePath $oneConfigViewer -ArgumentList ('"' + $oneConfigImage + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $oneConfigProcess.Refresh()
    } while (-not $oneConfigProcess.HasExited -and $oneConfigProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($oneConfigProcess.HasExited -or $oneConfigProcess.MainWindowHandle -eq 0) {
        throw "One-config regression failed: the viewer did not open a window."
    }
    Start-Sleep -Milliseconds 1500

    # 旋转记录要从旧文件迁过来并且真的生效：标题里会带旋转角度
    $oneConfigTitle = New-Object Text.StringBuilder 512
    [void][YeImageViewerTestNativeV1365]::GetWindowText([IntPtr]$oneConfigProcess.MainWindowHandle, $oneConfigTitle, 512)
    if ($oneConfigTitle.ToString() -notmatch "90") {
        throw "One-config regression failed: the migrated rotation was not applied ($($oneConfigTitle.ToString()))."
    }

    [void]$oneConfigProcess.CloseMainWindow()
    if (-not $oneConfigProcess.WaitForExit(5000)) {
        Stop-Process -Id $oneConfigProcess.Id -Force
        [void]$oneConfigProcess.WaitForExit(3000)
    }
    Start-Sleep -Milliseconds 400

    if (Test-Path -LiteralPath $legacyEditors) {
        throw "One-config regression failed: the legacy editors file was not migrated away."
    }
    if (Test-Path -LiteralPath $legacyRotations) {
        throw "One-config regression failed: the legacy rotation database was not migrated away."
    }

    $landed = @(Get-ChildItem -LiteralPath $oneConfigDirectory -File | Sort-Object Name)
    $landedNames = ($landed | ForEach-Object { $_.Name }) -join ", "
    if ($landed.Count -ne 3) {
        throw "One-config regression failed: expected exactly three files, found $($landed.Count) ($landedNames)."
    }

    $configBytes = [IO.File]::ReadAllBytes((Join-Path $oneConfigDirectory "YeImageViewer.db"))
    if ($configBytes.Length -le 4096) {
        throw "One-config regression failed: the settings file has no text section."
    }
    if ([Text.Encoding]::ASCII.GetString($configBytes, 0, 20) -ne "YeImageViewerSetting") {
        throw "One-config regression failed: the fixed settings block was overwritten by the text section."
    }
    $configTail = [Text.Encoding]::UTF8.GetString($configBytes, 4096, $configBytes.Length - 4096)
    if ($configTail -notmatch "Path0=" -or $configTail -notmatch "Rot0=") {
        throw "One-config regression failed: editors or rotations are missing from the shared text section."
    }
    Write-Host "PASS settings, editors, and rotations share one file and legacy configs migrate into it ($landedNames)."
}
finally {
    if ($null -ne $oneConfigProcess -and -not $oneConfigProcess.HasExited) {
        Stop-Process -Id $oneConfigProcess.Id -Force -ErrorAction SilentlyContinue
        [void]$oneConfigProcess.WaitForExit(3000)
    }
    Remove-Item -LiteralPath $oneConfigDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "Checking fresh-install window defaults..."
$freshDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Fresh-" + [Guid]::NewGuid().ToString("N"))
$freshViewer = Join-Path $freshDirectory "YeImageViewer.exe"
$editorProbe = Join-Path $freshDirectory "ExternalEditorProbe.exe"
$freshProcess = $null
$editorProbeProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $freshDirectory)
    Copy-Item -LiteralPath $viewer -Destination $freshViewer
    Copy-Item -LiteralPath $viewer -Destination $editorProbe
    $externalEditorSettings = Join-Path $freshDirectory "YeImageViewer.editors.ini"
    $externalEditorProbeConfig = "[Editors]`r`nCount=1`r`nName0=ExternalEditorProbe`r`nPath0=$editorProbe`r`n"
    [IO.File]::WriteAllText($externalEditorSettings, $externalEditorProbeConfig,
        [Text.UTF8Encoding]::new($true))
    $freshProcess = Start-Process -FilePath $freshViewer -ArgumentList ('"' + $sharpSvgFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $freshProcess.Refresh()
    } while (-not $freshProcess.HasExited -and $freshProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($freshProcess.HasExited -or $freshProcess.MainWindowHandle -eq 0 -or -not $freshProcess.Responding) {
        throw "Fresh-install regression failed: viewer did not open a responsive window."
    }
    $freshWindow = [IntPtr]$freshProcess.MainWindowHandle
    if ([YeImageViewerTestNativeV1365]::IsZoomed($freshWindow)) {
        throw "Fresh-install regression failed: a new installation opened maximized."
    }
    $freshClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($freshWindow, [ref]$freshClientRect)
    $freshMonitor = [YeImageViewerTestNativeV1365]::MonitorFromWindow($freshWindow, 2)
    $freshMonitorInfo = New-Object YeImageViewerTestNativeV1365+MONITORINFO
    $freshMonitorInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($freshMonitorInfo)
    [void][YeImageViewerTestNativeV1365]::GetMonitorInfo($freshMonitor, [ref]$freshMonitorInfo)
    if (($freshMonitorInfo.Flags -band 1) -eq 0) {
        throw "Fresh-install regression failed: visible UI tests did not stay on the primary monitor."
    }
    $freshWorkWidth = $freshMonitorInfo.Work.Right - $freshMonitorInfo.Work.Left
    $freshWorkHeight = $freshMonitorInfo.Work.Bottom - $freshMonitorInfo.Work.Top
    $freshStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -16).ToInt64()
    $freshExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -20).ToInt64()
    if (($freshStyle -band 0x00C00000) -ne 0 -or
        ($freshExtendedStyle -band 0x00200000) -eq 0 -or
        [Math]::Abs(($freshClientRect.Right - $freshClientRect.Left) - $freshWorkWidth) -gt 1 -or
        [Math]::Abs(($freshClientRect.Bottom - $freshClientRect.Top) - $freshWorkHeight) -gt 1) {
        throw "Fresh-install regression failed: image did not open in the borderless monitor work area."
    }
    Write-Host "PASS fresh install opens in the borderless immersive work area."
    Write-Host "PASS visible UI regressions stay on the primary monitor."
    Start-Sleep -Milliseconds 300
    $freshInitialTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $freshInitialTitle, $freshInitialTitle.Capacity)
    $freshInitialZoomMatch = [regex]::Match($freshInitialTitle.ToString(), '(\d+)%')
    if (-not $freshInitialZoomMatch.Success) {
        throw "Fresh-install regression failed: presentation title did not report its zoom percentage."
    }

    $freshCenterX = [int](($freshClientRect.Right - $freshClientRect.Left) / 2)
    $freshCenterY = [int](($freshClientRect.Bottom - $freshClientRect.Top) / 2)
    $freshCenterPosition = [IntPtr](($freshCenterY -shl 16) -bor ($freshCenterX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0200, [UIntPtr]::Zero, $freshCenterPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0201, [UIntPtr]1, $freshCenterPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0202, [UIntPtr]0, $freshCenterPosition)
    Start-Sleep -Milliseconds 250
    $freshImageClickStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -16).ToInt64()
    if (($freshImageClickStyle -band 0x00C00000) -ne 0) {
        throw "Fresh-install regression failed: clicking the image unexpectedly left presentation mode."
    }
    Write-Host "PASS clicking the image keeps presentation mode available for dragging."

    # 「图片外的空白处」不能写死一个坐标，按当前这张图的实际留白算（见 Get-ViewerBackgroundPoint）
    $freshBackgroundPosition = Get-ViewerBackgroundPoint -Window $freshWindow -Title $freshInitialTitle.ToString()
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0200, [UIntPtr]::Zero, $freshBackgroundPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0201, [UIntPtr]1, $freshBackgroundPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0202, [UIntPtr]0, $freshBackgroundPosition)

    # 点击进的是操作队列，要等绘制循环消费掉才会退回带边框窗口。
    # 原来固定等 500 毫秒，机器一忙（比如刚跑完性能测量）就会假失败，
    # 改成轮询到窗口真的变回来为止。
    $freshFramedDeadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 100
        $freshFramedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -16).ToInt64()
        $freshFramedExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -20).ToInt64()
        $freshFramedRect = New-Object YeImageViewerTestNativeV1365+RECT
        [void][YeImageViewerTestNativeV1365]::GetClientRect($freshWindow, [ref]$freshFramedRect)
        $freshFramedWidth = $freshFramedRect.Right - $freshFramedRect.Left
        $freshFramedHeight = $freshFramedRect.Bottom - $freshFramedRect.Top
        $freshFramedReady = ($freshFramedStyle -band 0x00C00000) -ne 0 -and
            ($freshFramedExtendedStyle -band 0x00200000) -ne 0 -and
            $freshFramedWidth -lt $freshWorkWidth -and $freshFramedHeight -lt $freshWorkHeight
    } while (-not $freshFramedReady -and [DateTime]::UtcNow -lt $freshFramedDeadline)

    if (-not $freshFramedReady) {
        throw "Fresh-install regression failed: clicking the background did not return to an image-sized framed window."
    }
    if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($freshWindow)) {
        throw "Fresh-install regression failed: framed window remained disabled after leaving presentation mode."
    }
    $freshThreadId = [YeImageViewerTestNativeV1365]::GetWindowThreadProcessId($freshWindow, [IntPtr]::Zero)
    $interactionDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $freshThreadInfo = New-Object YeImageViewerTestNativeV1365+GUITHREADINFO
        $freshThreadInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($freshThreadInfo)
        $freshThreadInfoAvailable = [YeImageViewerTestNativeV1365]::GetGUIThreadInfo(
            $freshThreadId, [ref]$freshThreadInfo)
        if ($freshThreadInfoAvailable -and $freshThreadInfo.Active -eq $freshWindow -and
            $freshThreadInfo.Focus -eq $freshWindow -and
            $freshThreadInfo.Capture -eq [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $interactionDeadline)
    if (-not $freshThreadInfoAvailable -or $freshThreadInfo.Active -ne $freshWindow -or
        $freshThreadInfo.Focus -ne $freshWindow -or
        $freshThreadInfo.Capture -ne [IntPtr]::Zero) {
        throw "Fresh-install regression failed: framed window did not restore active mouse and keyboard interaction."
    }
    Write-Host "PASS background click restores an active, enabled framed window."

    $freshFramedTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $freshFramedTitle, $freshFramedTitle.Capacity)
    $freshFramedTitleText = $freshFramedTitle.ToString()
    # 标题格式：[08/22] 名称.png 671x477(108.0KB) 110%
    # 序号在最前且补零对齐（标题栏是比例字体，补空格照样跳），像素紧跟文件名，
    # 体积写在括号里，缩放收尾。标题里不能出现完整路径。
    if ($freshFramedTitleText -notmatch '^\[\d+/\d+\]\s' -or
        $freshFramedTitleText -notmatch '\s[^\\/:]+\.[A-Za-z0-9]+\s\d+x\d+\([^)]+\)\s\d+%' -or
        $freshFramedTitleText.Contains([IO.Path]::GetDirectoryName($sharpSvgFixture))) {
        throw "Title regression failed: expected [n/total] name WxH(size) zoom%. Actual: $freshFramedTitleText"
    }
    Write-Host "PASS framed title reads position, name, pixels with size, then zoom."

    # WM_MOUSEWHEEL packs modifier flags in the low word and the signed wheel
    # delta in the high word. The requested defaults are Ctrl=zoom,
    # Shift=horizontal pan, and plain wheel=vertical pan.
    $freshFramedCenterX = [int]($freshFramedWidth / 2)
    $freshFramedCenterY = [int]($freshFramedHeight / 2)
    $freshFramedCenterPosition = [IntPtr](($freshFramedCenterY -shl 16) -bor ($freshFramedCenterX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x0200, [UIntPtr]::Zero, $freshFramedCenterPosition)

    $wheelStartTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $wheelStartTitle, $wheelStartTitle.Capacity)
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]0x00780008, $freshFramedCenterPosition)
    Start-Sleep -Milliseconds 500
    $wheelZoomTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $wheelZoomTitle, $wheelZoomTitle.Capacity)
    $wheelInitialZoom = [regex]::Match($wheelStartTitle.ToString(), '(\d+)%')
    $wheelChangedZoom = [regex]::Match($wheelZoomTitle.ToString(), '(\d+)%')
    if (-not $wheelInitialZoom.Success -or -not $wheelChangedZoom.Success -or
        $wheelChangedZoom.Groups[1].Value -eq $wheelInitialZoom.Groups[1].Value) {
        throw "Wheel regression failed: Ctrl + wheel did not zoom the current image."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]4287102984, $freshFramedCenterPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]0x00780000, $freshFramedCenterPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]0x00780004, $freshFramedCenterPosition)
    Start-Sleep -Milliseconds 500
    $freshProcess.Refresh()
    if ($freshProcess.HasExited -or -not $freshProcess.Responding) {
        throw "Wheel regression failed: vertical or horizontal pan stopped the viewer."
    }
    Write-Host "PASS Ctrl+wheel zooms, plain wheel pans vertically, and Shift+wheel pans horizontally."

    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
    $browseDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $freshSwitchedRect = New-Object YeImageViewerTestNativeV1365+RECT
        [void][YeImageViewerTestNativeV1365]::GetClientRect($freshWindow, [ref]$freshSwitchedRect)
    } while ((($freshSwitchedRect.Right - $freshSwitchedRect.Left) -ne $freshFramedWidth -or
        ($freshSwitchedRect.Bottom - $freshSwitchedRect.Top) -ne $freshFramedHeight) -and
        [DateTime]::UtcNow -lt $browseDeadline)
    if (($freshSwitchedRect.Right - $freshSwitchedRect.Left) -ne $freshFramedWidth -or
        ($freshSwitchedRect.Bottom - $freshSwitchedRect.Top) -ne $freshFramedHeight) {
        throw "Fresh-install regression failed: browsing images changed the anchored framed window size."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0100, [UIntPtr]0x25, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0101, [UIntPtr]0x25, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $freshReturnedTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $freshReturnedTitle, $freshReturnedTitle.Capacity)
    $freshReturnedZoomMatch = [regex]::Match($freshReturnedTitle.ToString(), '(\d+)%')
    if (-not $freshReturnedZoomMatch.Success -or
        $freshReturnedZoomMatch.Groups[1].Value -ne $freshInitialZoomMatch.Groups[1].Value) {
        throw "Fresh-install regression failed: returning to the first image changed its immersive zoom percentage."
    }
    Write-Host "PASS background exit anchors the frame while browsing preserves per-image zoom."

    [void][YeImageViewerTestNativeV1365]::PostMessage($freshWindow, 0x0100, [UIntPtr]0x71, [IntPtr]::Zero)
    $renameDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $renameWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$freshProcess.Id, "YeImageViewerRenameWnd")
    } while ($renameWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $renameDeadline)
    if ($renameWindow -eq [IntPtr]::Zero) {
        throw "Rename-shortcut regression failed: F2 did not open the rename window."
    }
    $unexpectedF2Window = [YeImageViewerTestNativeV1365]::FindProcessWindow(
        [uint32]$freshProcess.Id, "YeImageViewerSettingWnd")
    if ($unexpectedF2Window -ne [IntPtr]::Zero) {
        throw "Rename-shortcut regression failed: F2 also opened Settings."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($renameWindow, 0x0111, [UIntPtr]2, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($freshWindow)) {
        throw "Rename-shortcut regression failed: cancelling rename left the viewer disabled."
    }
    Write-Host "PASS F2 opens Rename, not Settings, and cancel restores viewer interaction."

    # The chooser must remain a real modal executable picker. Cancel it and
    # verify that the viewer is usable again; the configured command below then
    # exercises the complete ShellExecute image-path launch contract.
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $freshWindow, 0x0111, [UIntPtr]1025, [IntPtr]::Zero)
    $editorDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $editorDialog = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$freshProcess.Id, "#32770")
    } while ($editorDialog -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $editorDeadline)
    $editorNameEdit = if ($editorDialog -ne [IntPtr]::Zero) {
        [YeImageViewerTestNativeV1365]::FindDescendant(
            [uint32]$freshProcess.Id, $editorDialog, "Edit", 1148)
    } else { [IntPtr]::Zero }
    if ($editorDialog -eq [IntPtr]::Zero -or $editorNameEdit -eq [IntPtr]::Zero) {
        throw "External-editor regression failed: Choose application did not expose a usable executable picker."
    }
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $editorDialog, 0x0111, [UIntPtr]2, [IntPtr]::Zero)
    $editorDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $editorDialog = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$freshProcess.Id, "#32770")
    } while ($editorDialog -ne [IntPtr]::Zero -and [DateTime]::UtcNow -lt $editorDeadline)
    if ($editorDialog -ne [IntPtr]::Zero -or
        -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($freshWindow)) {
        throw "External-editor regression failed: cancelling the executable picker did not restore the viewer."
    }

    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $freshWindow, 0x0111, [UIntPtr]1015, [IntPtr]::Zero)
    $editorDeadline = [DateTime]::UtcNow.AddSeconds(7)
    do {
        Start-Sleep -Milliseconds 150
        $editorProbeProcess = Get-Process -Name "ExternalEditorProbe" -ErrorAction SilentlyContinue |
            Sort-Object StartTime -Descending | Select-Object -First 1
    } while (-not $editorProbeProcess -and [DateTime]::UtcNow -lt $editorDeadline)
    if (-not $editorProbeProcess) {
        throw "External-editor regression failed: the persisted default application did not launch."
    }
    $editorProbeDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $editorProbeProcess.Refresh()
    } while (-not $editorProbeProcess.HasExited -and
        ($editorProbeProcess.MainWindowHandle -eq 0 -or
            -not $editorProbeProcess.MainWindowTitle.Contains(
                [IO.Path]::GetFileName($sharpSvgFixture))) -and
        [DateTime]::UtcNow -lt $editorProbeDeadline)
    if ($editorProbeProcess.HasExited -or $editorProbeProcess.MainWindowHandle -eq 0 -or
        -not $editorProbeProcess.MainWindowTitle.Contains(
            [IO.Path]::GetFileName($sharpSvgFixture))) {
        throw "External-editor regression failed: the configured application did not receive the current image path."
    }
    [void]$editorProbeProcess.CloseMainWindow()
    if (-not $editorProbeProcess.WaitForExit(3000)) {
        Stop-Process -Id $editorProbeProcess.Id -Force
        $editorProbeProcess.WaitForExit()
    }
    $editorProbeProcess = $null
    # 编辑器配置已经并进 YeImageViewer.db 的文本区（前 4096 字节是设置结构体），
    # 启动时旧的 .editors.ini 会被迁移后删除，所以这里查的是合并后的那个文件。
    if (Test-Path -LiteralPath $externalEditorSettings -PathType Leaf) {
        throw "External-editor regression failed: the legacy editors file was left behind instead of migrated."
    }
    $mergedConfigPath = Join-Path $freshDirectory "YeImageViewer.db"
    if (-not (Test-Path -LiteralPath $mergedConfigPath -PathType Leaf)) {
        throw "External-editor regression failed: the shared settings file was not created."
    }
    $mergedConfigBytes = [IO.File]::ReadAllBytes($mergedConfigPath)
    if ($mergedConfigBytes.Length -le 4096 -or
        -not [Text.Encoding]::UTF8.GetString($mergedConfigBytes, 4096,
            $mergedConfigBytes.Length - 4096).Contains("ExternalEditorProbe")) {
        throw "External-editor regression failed: the named editor was not persisted in the shared settings file."
    }
    Write-Host "PASS external-editor picker restores interaction and the submenu opens the current image in its configured application."

    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0111, [UIntPtr]1010, [IntPtr]::Zero)
    $settingDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $settingWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$freshProcess.Id, "YeImageViewerSettingWnd")
    } while ($settingWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $settingDeadline)
    if ($settingWindow -eq [IntPtr]::Zero) {
        throw "Settings-layout regression failed: Settings window did not open."
    }
    $settingRect = New-Object YeImageViewerTestNativeV1365+RECT
    $settingLayoutDeadline = [DateTime]::UtcNow.AddSeconds(2)
    do {
        [void][YeImageViewerTestNativeV1365]::GetClientRect($settingWindow, [ref]$settingRect)
        $settingDpi = [YeImageViewerTestNativeV1365]::GetDpiForWindow($settingWindow)
        # The drawing canvas stays a logical 620x620; the window scales itself up by
        # DPI because the process is PerMonitorHighDPIAware and Windows does no
        # stretching of its own. The viewer caps that scaling so the window still
        # fits the work area, so the client is a square between 620 and the full
        # DPI-scaled size.
        $expectedSettingWidth = [int][Math]::Round(620 * $settingDpi / 96.0)
        $expectedSettingHeight = $expectedSettingWidth
        $actualSettingWidth = $settingRect.Right - $settingRect.Left
        $actualSettingHeight = $settingRect.Bottom - $settingRect.Top
        $settingSizeMatches = ($actualSettingWidth -eq $actualSettingHeight) -and
            ($actualSettingWidth -ge 620) -and ($actualSettingWidth -le $expectedSettingWidth)
        $settingLayoutReady = $settingSizeMatches -and
            [YeImageViewerTestNativeV1365]::IsWindowEnabled($settingWindow)
        if (-not $settingLayoutReady) {
            Start-Sleep -Milliseconds 100
        }
    } while (-not $settingLayoutReady -and [DateTime]::UtcNow -lt $settingLayoutDeadline)
    if (-not $settingLayoutReady) {
        $actualSettingSize = "$(($settingRect.Right - $settingRect.Left))x$(($settingRect.Bottom - $settingRect.Top))"
        throw "Settings-layout regression failed: expected a logical 620x620 canvas (${expectedSettingWidth}x${expectedSettingHeight} physical at ${settingDpi} DPI), got $actualSettingSize."
    }
    $initialSettingWidth = $settingRect.Right - $settingRect.Left
    $initialSettingHeight = $settingRect.Bottom - $settingRect.Top
    foreach ($tabStep in 1..4) {
        [void][YeImageViewerTestNativeV1365]::SendMessage($settingWindow, 0x0100, [UIntPtr]0x09, [IntPtr]::Zero)
        [void][YeImageViewerTestNativeV1365]::SendMessage($settingWindow, 0x0101, [UIntPtr]0x09, [IntPtr]::Zero)
        if ($tabStep -eq 2) {
            [void][YeImageViewerTestNativeV1365]::SendMessage(
                $settingWindow, 0x020A, [UIntPtr][uint64]4287102976, [IntPtr]::Zero)
        }
        Start-Sleep -Milliseconds 100
        [void][YeImageViewerTestNativeV1365]::GetClientRect($settingWindow, [ref]$settingRect)
        if (($settingRect.Right - $settingRect.Left) -ne $initialSettingWidth -or
            ($settingRect.Bottom - $settingRect.Top) -ne $initialSettingHeight -or
            -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($settingWindow)) {
            throw "Settings-layout regression failed: switching tabs changed the fixed client size or interaction state."
        }
    }
    Write-Host "PASS Settings keeps a fixed logical 620x620 client area, DPI-scaled, across all tabs and scrolls Shortcuts content."

    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0112, [UIntPtr]0xF060, [IntPtr]::Zero)
    if (-not $freshProcess.WaitForExit(3000)) {
        throw "Window-close regression failed: restored title-bar close left a residual process."
    }
    Write-Host "PASS restored title-bar close exits without a residual process."
    $freshProcess = Start-Process -FilePath $freshViewer -ArgumentList ('"' + $hdrFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $freshProcess.Refresh()
    } while (-not $freshProcess.HasExited -and $freshProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($freshProcess.HasExited -or $freshProcess.MainWindowHandle -eq 0 -or -not $freshProcess.Responding) {
        throw "Rotation restart regression failed: landscape HDR did not open."
    }

    $rotationWindow = [IntPtr]$freshProcess.MainWindowHandle
    $landscapeRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($rotationWindow, [ref]$landscapeRect)
    $landscapeWidth = $landscapeRect.Right - $landscapeRect.Left
    $landscapeHeight = $landscapeRect.Bottom - $landscapeRect.Top
    if ($landscapeWidth -le $landscapeHeight) {
        throw "Rotation restart regression failed: HDR did not start in landscape orientation."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0100, [UIntPtr]0x51, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0101, [UIntPtr]0x51, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    [void]$freshProcess.CloseMainWindow()
    if (-not $freshProcess.WaitForExit(3000)) {
        throw "Rotation restart regression failed: rotated viewer did not close."
    }

    $freshProcess = Start-Process -FilePath $freshViewer -ArgumentList ('"' + $hdrFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $freshProcess.Refresh()
    } while (-not $freshProcess.HasExited -and $freshProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($freshProcess.HasExited -or $freshProcess.MainWindowHandle -eq 0 -or -not $freshProcess.Responding) {
        throw "Rotation restart regression failed: persisted portrait HDR did not reopen."
    }
    $rotationWindow = [IntPtr]$freshProcess.MainWindowHandle
    $rotationTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText($rotationWindow, $rotationTitle, $rotationTitle.Capacity)
    if (-not $rotationTitle.ToString().Contains("90")) {
        throw "Rotation restart regression failed: reopened HDR title did not report the saved quarter-turn."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0100, [UIntPtr]0x45, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0101, [UIntPtr]0x45, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    Write-Host "PASS image rotation persists across a real process restart."
}
finally {
    if ($editorProbeProcess -and -not $editorProbeProcess.HasExited) {
        Stop-Process -Id $editorProbeProcess.Id -Force
        $editorProbeProcess.WaitForExit()
    }
    if ($freshProcess -and -not $freshProcess.HasExited) {
        [void]$freshProcess.CloseMainWindow()
        if (-not $freshProcess.WaitForExit(3000)) {
            Stop-Process -Id $freshProcess.Id -Force
            $freshProcess.WaitForExit()
        }
    }
    foreach ($freshFile in @(
        $freshViewer,
        $editorProbe,
        (Join-Path $freshDirectory "YeImageViewer.db"),
        (Join-Path $freshDirectory "YeImageViewer.editors.ini")
    )) {
        if (Test-Path -LiteralPath $freshFile) {
            # 进程刚退出时，它的 exe 可能还被杀毒扫描或程序兼容性助手短暂占用，
            # 删除会报拒绝访问。稍候重试，而不是让清理失败把后面的窗口回归整段中断。
            $removeWatch = [Diagnostics.Stopwatch]::StartNew()
            while ($true) {
                try {
                    Remove-Item -LiteralPath $freshFile -Force -ErrorAction Stop
                    break
                }
                catch {
                    if ($removeWatch.Elapsed.TotalSeconds -ge 10) {
                        throw
                    }
                    Start-Sleep -Milliseconds 250
                }
            }
            if ($removeWatch.Elapsed.TotalMilliseconds -ge 250) {
                Write-Host ("NOTE {0} was locked after exit; removed after {1:N0} ms." -f
                    (Split-Path -Leaf $freshFile), $removeWatch.Elapsed.TotalMilliseconds)
            }
        }
    }
    if (Test-Path -LiteralPath $freshDirectory) {
        Remove-Item -LiteralPath $freshDirectory
    }
}

Write-Host "Checking installed Paint and RIOT through configured editor commands..."
$installedEditorDefinitions = @(
    @{
        Name = "画图"
        Path = "C:\Program Files\WindowsApps\Microsoft.Paint_11.2605.81.0_x64__8wekyb3d8bbwe\PaintApp\mspaint.exe"
        ProcessName = "mspaint"
    },
    @{
        Name = "RIOT 压缩"
        Path = "C:\Program Files\Riot\Riot.exe"
        ProcessName = "Riot"
    }
)
$availableInstalledEditors = @($installedEditorDefinitions | Where-Object {
    Test-Path -LiteralPath $_.Path -PathType Leaf
})
if ($availableInstalledEditors.Count -eq 0) {
    Write-Host "SKIP Paint/RIOT real-editor smoke test: neither optional application is installed."
}
else {
    $installedEditorDirectory = Join-Path ([IO.Path]::GetTempPath()) (
        "YeImageViewer-InstalledEditors-" + [Guid]::NewGuid().ToString("N"))
    $installedEditorViewer = Join-Path $installedEditorDirectory "YeImageViewer.exe"
    $installedEditorProcess = $null
    $launchedInstalledEditor = $null
    try {
        [void](New-Item -ItemType Directory -Path $installedEditorDirectory)
        Copy-Item -LiteralPath $viewer -Destination $installedEditorViewer
        $configLines = [Collections.Generic.List[string]]::new()
        $configLines.Add("[Editors]")
        $configLines.Add("Count=$($availableInstalledEditors.Count)")
        for ($index = 0; $index -lt $availableInstalledEditors.Count; $index++) {
            $configLines.Add("Name$index=$($availableInstalledEditors[$index].Name)")
            $configLines.Add("Path$index=$($availableInstalledEditors[$index].Path)")
        }
        [IO.File]::WriteAllText(
            (Join-Path $installedEditorDirectory "YeImageViewer.editors.ini"),
            ($configLines -join "`r`n") + "`r`n", [Text.UTF8Encoding]::new($true))

        $installedEditorProcess = Start-Process -FilePath $installedEditorViewer -ArgumentList ('"' + $commonPngFixture + '"') -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds(6)
        do {
            Start-Sleep -Milliseconds 150
            $installedEditorProcess.Refresh()
        } while (-not $installedEditorProcess.HasExited -and
            $installedEditorProcess.MainWindowHandle -eq 0 -and
            [DateTime]::UtcNow -lt $deadline)
        if ($installedEditorProcess.HasExited -or
            $installedEditorProcess.MainWindowHandle -eq 0) {
            throw "Installed-editor regression failed: viewer did not open."
        }
        $installedEditorWindow = [IntPtr]$installedEditorProcess.MainWindowHandle

        for ($index = 0; $index -lt $availableInstalledEditors.Count; $index++) {
            $definition = $availableInstalledEditors[$index]
            $existingIds = @(Get-Process -Name $definition.ProcessName -ErrorAction SilentlyContinue |
                ForEach-Object Id)
            if ($existingIds.Count -gt 0) {
                Write-Host "SKIP $($definition.Name) launch: an existing user process is already open."
                continue
            }
            [void][YeImageViewerTestNativeV1365]::PostMessage(
                $installedEditorWindow, 0x0111, [UIntPtr](1015 + $index), [IntPtr]::Zero)
            $deadline = [DateTime]::UtcNow.AddSeconds(10)
            do {
                Start-Sleep -Milliseconds 200
                $launchedInstalledEditor = Get-Process -Name $definition.ProcessName -ErrorAction SilentlyContinue |
                    Where-Object { $existingIds -notcontains $_.Id } |
                    Sort-Object StartTime -Descending | Select-Object -First 1
                if ($launchedInstalledEditor) {
                    $launchedInstalledEditor.Refresh()
                }
            } while ((-not $launchedInstalledEditor -or
                $launchedInstalledEditor.MainWindowHandle -eq 0 -or
                -not $launchedInstalledEditor.MainWindowTitle.Contains(
                    [IO.Path]::GetFileName($commonPngFixture))) -and
                [DateTime]::UtcNow -lt $deadline)
            if (-not $launchedInstalledEditor -or $launchedInstalledEditor.HasExited -or
                $launchedInstalledEditor.MainWindowHandle -eq 0 -or
                -not $launchedInstalledEditor.MainWindowTitle.Contains(
                    [IO.Path]::GetFileName($commonPngFixture))) {
                throw "Installed-editor regression failed: $($definition.Name) did not open the configured image."
            }
            Write-Host "PASS YeImageViewer opened common.png in $($definition.Name)."
            [void]$launchedInstalledEditor.CloseMainWindow()
            if (-not $launchedInstalledEditor.WaitForExit(3000)) {
                Stop-Process -Id $launchedInstalledEditor.Id -Force
                $launchedInstalledEditor.WaitForExit()
            }
            $launchedInstalledEditor = $null
        }
    }
    finally {
        if ($launchedInstalledEditor -and -not $launchedInstalledEditor.HasExited) {
            Stop-Process -Id $launchedInstalledEditor.Id -Force
            $launchedInstalledEditor.WaitForExit()
        }
        if ($installedEditorProcess -and -not $installedEditorProcess.HasExited) {
            [void]$installedEditorProcess.CloseMainWindow()
            if (-not $installedEditorProcess.WaitForExit(3000)) {
                Stop-Process -Id $installedEditorProcess.Id -Force
                $installedEditorProcess.WaitForExit()
            }
        }
        if (Test-Path -LiteralPath $installedEditorDirectory) {
            [IO.Directory]::Delete($installedEditorDirectory, $true)
        }
    }
}

Write-Host "Checking the Escape shortcut in presentation and framed modes..."
# 关闭图片以前是「行为」里的开关，现在是快捷键页的一个动作，默认绑在 Esc 上。
# 这一段盯两件事：默认 Esc 真的关图片；在快捷键页点 x 清空后，Esc 退回原来的
# 「退出沉浸预览」，而且清空结果要写进 4096 字节的设置文件、重开仍然有效。
$escapeDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Escape-" + [Guid]::NewGuid().ToString("N"))
$escapeViewer = Join-Path $escapeDirectory "YeImageViewer.exe"
$escapeProcess = $null

function Start-EscapeViewer {
    $process = Start-Process -FilePath $escapeViewer -ArgumentList ('"' + $sharpSvgFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 150
        $process.Refresh()
    } while (-not $process.HasExited -and $process.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited -or $process.MainWindowHandle -eq 0) {
        throw "Escape-shortcut regression failed: the viewer did not open."
    }

    # 窗口句柄出现不等于已经能干活：首次打开这张 SVG 要渲染好几秒（冷热差别实测
    # 140 ms 对 4~6 s，改动前后一样）。这期间发过去的按键要排在后面，本条用例
    # 量的是「Esc 关不关图」，不是启动有多快，所以先等它能回消息再按。
    $readyDeadline = [DateTime]::UtcNow.AddSeconds(15)
    $result = [UIntPtr]::Zero
    do {
        $responded = [YeImageViewerTestNativeV1365]::SendMessageTimeout(
            [IntPtr]$process.MainWindowHandle, 0x0000, [UIntPtr]::Zero, [IntPtr]::Zero,
            2, 1000, [ref]$result)
        if ($responded -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $readyDeadline)

    return $process
}

try {
    [void](New-Item -ItemType Directory -Path $escapeDirectory)
    Copy-Item -LiteralPath $viewer -Destination $escapeViewer

    $escapeProcess = Start-EscapeViewer
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        [IntPtr]$escapeProcess.MainWindowHandle, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    # 给 10 秒而不是 3 秒：按键进的是操作队列，要等绘制循环空出来才被消费，而首次
    # 打开这张 SVG 的渲染要好几秒（冷 5.5 s / 热 0.12 s，今天改动前后一样，
    # 已用 A/B 确认不是回归）。本条量的是「Esc 关不关图」，不是启动有多快。
    if (-not $escapeProcess.WaitForExit(10000)) {
        throw "Escape-shortcut regression failed: Escape does not close the image by default."
    }
    Write-Host "PASS Escape closes the image out of the box."

    # 清空「关闭图片」的快捷键：F3 直接开到快捷键页，点该行末尾的 x
    $escapeProcess = Start-EscapeViewer
    $escapeWindow = [IntPtr]$escapeProcess.MainWindowHandle
    [void][YeImageViewerTestNativeV1365]::SendMessage($escapeWindow, 0x0100, [UIntPtr]0x72, [IntPtr]::Zero)
    $settingDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $escapeSettingWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$escapeProcess.Id, "YeImageViewerSettingWnd")
    } while ($escapeSettingWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $settingDeadline)
    if ($escapeSettingWindow -eq [IntPtr]::Zero) {
        throw "Escape-shortcut regression failed: F3 did not open the Shortcuts page."
    }
    # 坐标写在固定的 620x620 逻辑画布里，鼠标消息带的是物理客户区像素；缩放倍率按
    # 实际客户区宽度反推，不能直接用 DPI——窗口放不下时程序会自己压低缩放。
    $escapeSettingRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($escapeSettingWindow, [ref]$escapeSettingRect)
    $escapeSettingWidth = $escapeSettingRect.Right - $escapeSettingRect.Left
    if ($escapeSettingWidth -le 0) { $escapeSettingWidth = 620 }
    # 「关闭图片」是键盘快捷键里的第 5 行（下标 4）：行 y = 288 + 4*40，x 见 SettingLayout 的
    # SHORTCUT_CLEAR_X；再加上标签页高度 52 换算成窗口坐标。
    $clearX = [int][Math]::Round((36 + 512 + 15) * $escapeSettingWidth / 620.0)
    $clearY = [int][Math]::Round((52 + 288 + 4 * 40 + 20) * $escapeSettingWidth / 620.0)
    $clearPosition = [IntPtr](($clearY -shl 16) -bor ($clearX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $escapeSettingWindow, 0x0201, [UIntPtr]1, $clearPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $escapeSettingWindow, 0x0202, [UIntPtr]0, $clearPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $escapeSettingWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250

    [void][YeImageViewerTestNativeV1365]::SendMessage($escapeWindow, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    $escapeProcess.Refresh()
    if ($escapeProcess.HasExited) {
        throw "Escape-shortcut regression failed: Escape still closed the image after the shortcut was cleared."
    }
    $framedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($escapeWindow, -16).ToInt64()
    if (($framedStyle -band 0x00C00000) -eq 0) {
        throw "Escape-shortcut regression failed: a cleared Escape did not fall back to leaving presentation mode."
    }
    Write-Host "PASS clearing the shortcut makes Escape leave presentation instead of closing."

    $settingsPath = Join-Path $escapeDirectory "YeImageViewer.db"
    [void]$escapeProcess.CloseMainWindow()
    if (-not $escapeProcess.WaitForExit(3000)) {
        Stop-Process -Id $escapeProcess.Id -Force
        $escapeProcess.WaitForExit()
    }
    # 设置块仍是固定 4096 字节，但文件后面可能跟着编辑器/目标/旋转的文本区，
    # 所以不能再要求「文件正好 4096 字节」，只能要求前 4096 字节是有效的设置块。
    if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
        throw "Escape-shortcut regression failed: the configuration file was not written."
    }
    $escapeConfigBytes = [IO.File]::ReadAllBytes($settingsPath)
    if ($escapeConfigBytes.Length -lt 4096 -or
        [Text.Encoding]::ASCII.GetString($escapeConfigBytes, 0, 20) -ne "YeImageViewerSetting") {
        throw "Escape-shortcut regression failed: the cleared shortcut was not persisted to the fixed 4096-byte settings block."
    }

    $escapeProcess = Start-EscapeViewer
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        [IntPtr]$escapeProcess.MainWindowHandle, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    $escapeProcess.Refresh()
    if ($escapeProcess.HasExited) {
        throw "Escape-shortcut regression failed: the cleared shortcut came back after a restart."
    }
    Write-Host "PASS a cleared shortcut stays cleared across a restart."
}
finally {
    if ($escapeProcess -and -not $escapeProcess.HasExited) {
        [void]$escapeProcess.CloseMainWindow()
        if (-not $escapeProcess.WaitForExit(3000)) {
            Stop-Process -Id $escapeProcess.Id -Force
            $escapeProcess.WaitForExit()
        }
    }
    if (Test-Path -LiteralPath $escapeDirectory) {
        Remove-Item -LiteralPath $escapeDirectory -Recurse -Force
    }
}

Write-Host "Checking current-image rename workflow..."
$renameTestDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Rename-" + [Guid]::NewGuid().ToString("N"))
$renameTestViewer = Join-Path $renameTestDirectory "YeImageViewer.exe"
$renameOriginal = Join-Path $renameTestDirectory "rename-original.png"
$renameTestProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $renameTestDirectory)
    Copy-Item -LiteralPath $viewer -Destination $renameTestViewer
    Copy-Item -LiteralPath $jaggedFixture -Destination $renameOriginal
    $renameTestProcess = Start-Process -FilePath $renameTestViewer -ArgumentList ('"' + $renameOriginal + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $renameTestProcess.Refresh()
    } while (-not $renameTestProcess.HasExited -and $renameTestProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($renameTestProcess.HasExited -or $renameTestProcess.MainWindowHandle -eq 0 -or -not $renameTestProcess.Responding) {
        throw "Rename regression failed: viewer did not open the dedicated rename fixture."
    }

    $renameMainWindow = [IntPtr]$renameTestProcess.MainWindowHandle
    [void][YeImageViewerTestNativeV1365]::PostMessage($renameMainWindow, 0x0100, [UIntPtr]0x71, [IntPtr]::Zero)
    $renameDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $renameWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$renameTestProcess.Id, "YeImageViewerRenameWnd")
    } while ($renameWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $renameDeadline)
    if ($renameWindow -eq [IntPtr]::Zero) {
        throw "Rename regression failed: F2 did not open the native rename window."
    }
    $renameEdit = [YeImageViewerTestNativeV1365]::GetDlgItem($renameWindow, 1001)
    if ($renameEdit -eq [IntPtr]::Zero) {
        throw "Rename regression failed: filename edit control was not available."
    }
    $renameClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($renameMainWindow, [ref]$renameClientRect)
    $renameImageX = [int](($renameClientRect.Right - $renameClientRect.Left) / 2)
    $renameImageY = [int](($renameClientRect.Bottom - $renameClientRect.Top) / 2)
    $renameImagePosition = [IntPtr](($renameImageY -shl 16) -bor ($renameImageX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $renameMainWindow, 0x0201, [UIntPtr]1, $renameImagePosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $renameMainWindow, 0x0202, [UIntPtr]::Zero, $renameImagePosition)
    $renameDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $renameWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$renameTestProcess.Id, "YeImageViewerRenameWnd")
    } while ($renameWindow -ne [IntPtr]::Zero -and [DateTime]::UtcNow -lt $renameDeadline)
    if ($renameWindow -ne [IntPtr]::Zero -or
        -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($renameMainWindow) -or
        -not (Test-Path -LiteralPath $renameOriginal)) {
        throw "Rename regression failed: clicking the image did not dismiss F2 rename cleanly."
    }
    $renameTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $renameMainWindow, $renameTitle, $renameTitle.Capacity)
    if (-not $renameTitle.ToString().Contains("rename-original.png")) {
        throw "Rename regression failed: cancelling F2 changed the current image."
    }

    [void][YeImageViewerTestNativeV1365]::PostMessage($renameMainWindow, 0x0111, [UIntPtr]1014, [IntPtr]::Zero)
    $renameDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $renameWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$renameTestProcess.Id, "YeImageViewerRenameWnd")
    } while ($renameWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $renameDeadline)
    if ($renameWindow -eq [IntPtr]::Zero) {
        throw "Rename regression failed: the context-menu rename command did not open the rename window."
    }
    $renameEdit = [YeImageViewerTestNativeV1365]::GetDlgItem($renameWindow, 1001)
    if ($renameEdit -eq [IntPtr]::Zero) {
        throw "Rename regression failed: context-menu rename controls were unavailable."
    }
    $renameBackgroundX = 4
    $renameBackgroundY = [int](($renameClientRect.Bottom - $renameClientRect.Top) / 2)
    $renameBackgroundPosition = [IntPtr](($renameBackgroundY -shl 16) -bor ($renameBackgroundX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $renameMainWindow, 0x0201, [UIntPtr]1, $renameBackgroundPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $renameMainWindow, 0x0202, [UIntPtr]::Zero, $renameBackgroundPosition)
    $renameDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $renameWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$renameTestProcess.Id, "YeImageViewerRenameWnd")
    } while ($renameWindow -ne [IntPtr]::Zero -and [DateTime]::UtcNow -lt $renameDeadline)
    if ($renameWindow -ne [IntPtr]::Zero -or
        -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($renameMainWindow) -or
        -not (Test-Path -LiteralPath $renameOriginal)) {
        throw "Rename regression failed: clicking the background did not dismiss context-menu rename cleanly."
    }
    $renameTitle.Clear() | Out-Null
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $renameMainWindow, $renameTitle, $renameTitle.Capacity)
    if (-not $renameTitle.ToString().Contains("rename-original.png")) {
        throw "Rename regression failed: cancelling context-menu rename changed the current image."
    }
    Write-Host "PASS F2 and context-menu Rename dismiss on image or background clicks without changing the file."
}
finally {
    if ($renameTestProcess -and -not $renameTestProcess.HasExited) {
        [void]$renameTestProcess.CloseMainWindow()
        if (-not $renameTestProcess.WaitForExit(3000)) {
            Stop-Process -Id $renameTestProcess.Id -Force
            $renameTestProcess.WaitForExit()
        }
    }
    foreach ($renameFile in @(
        $renameTestViewer,
        $renameOriginal,
        (Join-Path $renameTestDirectory "YeImageViewer.db"),
        (Join-Path $renameTestDirectory "YeImageViewer.rotations.db"),
        (Join-Path $renameTestDirectory "YeImageViewer.rotations.db.tmp")
    )) {
        if (Test-Path -LiteralPath $renameFile) {
            Remove-Item -LiteralPath $renameFile -Force
        }
    }
    if (Test-Path -LiteralPath $renameTestDirectory) {
        Remove-Item -LiteralPath $renameTestDirectory
    }
}

Write-Host "Checking rename-dialog input-method availability..."
$imeSourceGlobs = @(
    (Join-Path $repoRoot "YeImageViewer\src\*.cpp"),
    (Join-Path $repoRoot "YeImageViewer\include\*.h")
)
$imeThreadDisableHits = @(Select-String -Path $imeSourceGlobs -SimpleMatch -Pattern "ImmDisableIME(")
if ($imeThreadDisableHits.Count -gt 0) {
    throw "IME regression failed: ImmDisableIME is thread-wide and cannot be undone, so it also strips Chinese input from the rename dialog. Detach the input method per window instead. Found at $($imeThreadDisableHits[0].Path):$($imeThreadDisableHits[0].LineNumber)."
}
foreach ($imeDetachSite in @(
    @{ Path = (Join-Path $repoRoot "YeImageViewer\src\D3D11App.cpp"); Marker = "ImmAssociateContext(m_hWnd, nullptr)" },
    @{ Path = (Join-Path $repoRoot "YeImageViewer\include\MatWindow.h"); Marker = "ImmAssociateContext(m_hwnd, nullptr)" }
)) {
    if (-not (Select-String -LiteralPath $imeDetachSite.Path -SimpleMatch -Pattern $imeDetachSite.Marker -Quiet)) {
        throw "IME regression failed: $($imeDetachSite.Path) must keep '$($imeDetachSite.Marker)' so an active input method cannot swallow single-key shortcuts."
    }
}
if (Select-String -LiteralPath (Join-Path $repoRoot "YeImageViewer\src\main.cpp") -SimpleMatch -Pattern "ImmAssociateContext" -Quiet) {
    throw "IME regression failed: the rename dialog must keep the default input context, so main.cpp must not detach the input method."
}
$imeTestDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Ime-" + [Guid]::NewGuid().ToString("N"))
$imeTestViewer = Join-Path $imeTestDirectory "YeImageViewer.exe"
$imeFixture = Join-Path $imeTestDirectory "ime-fixture.png"
$imeTestProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $imeTestDirectory)
    Copy-Item -LiteralPath $viewer -Destination $imeTestViewer
    Copy-Item -LiteralPath $jaggedFixture -Destination $imeFixture
    $imeTestProcess = Start-Process -FilePath $imeTestViewer -ArgumentList ('"' + $imeFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $imeTestProcess.Refresh()
    } while (-not $imeTestProcess.HasExited -and $imeTestProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($imeTestProcess.HasExited -or $imeTestProcess.MainWindowHandle -eq 0 -or -not $imeTestProcess.Responding) {
        throw "IME regression failed: the viewer did not open the input-method fixture."
    }

    # A thread-wide ImmDisableIME leaves the whole process without a default IME
    # window, so no text service ever loads. Detaching the context per window does
    # not. That difference is what this check reads.
    $imeMainWindow = [IntPtr]$imeTestProcess.MainWindowHandle
    if ([YeImageViewerTestNativeV1365]::ImmGetDefaultIMEWnd($imeMainWindow) -eq [IntPtr]::Zero) {
        throw "IME regression failed: the viewer thread has no default IME window, so no input method can load into the process and the rename dialog cannot type Chinese."
    }

    [void][YeImageViewerTestNativeV1365]::PostMessage($imeMainWindow, 0x0100, [UIntPtr]0x71, [IntPtr]::Zero)
    $imeDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $imeRenameWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$imeTestProcess.Id, "YeImageViewerRenameWnd")
    } while ($imeRenameWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $imeDeadline)
    if ($imeRenameWindow -eq [IntPtr]::Zero) {
        throw "IME regression failed: F2 did not open the rename window."
    }
    $imeRenameEdit = [YeImageViewerTestNativeV1365]::GetDlgItem($imeRenameWindow, 1001)
    if ($imeRenameEdit -eq [IntPtr]::Zero) {
        throw "IME regression failed: the rename edit control was not available."
    }
    if ([YeImageViewerTestNativeV1365]::ImmGetDefaultIMEWnd($imeRenameEdit) -eq [IntPtr]::Zero) {
        throw "IME regression failed: the rename edit control has no default IME window."
    }

    # Composition lands in the focused window, so the edit control has to own focus.
    $imeThreadId = [YeImageViewerTestNativeV1365]::GetWindowThreadProcessId($imeMainWindow, [IntPtr]::Zero)
    $imeThreadInfo = New-Object YeImageViewerTestNativeV1365+GUITHREADINFO
    $imeThreadInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($imeThreadInfo)
    $imeThreadInfoAvailable = $false
    $imeFocusDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $imeThreadInfoAvailable = [YeImageViewerTestNativeV1365]::GetGUIThreadInfo(
            $imeThreadId, [ref]$imeThreadInfo)
        if ($imeThreadInfoAvailable -and $imeThreadInfo.Focus -eq $imeRenameEdit) {
            break
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $imeFocusDeadline)
    if (-not $imeThreadInfoAvailable -or $imeThreadInfo.Focus -ne $imeRenameEdit) {
        throw "IME regression failed: the rename edit control never took keyboard focus, so an input method would compose into the wrong window."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($imeRenameWindow, 0x0111, [UIntPtr]2, [IntPtr]::Zero)
    $imeDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $imeRenameWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$imeTestProcess.Id, "YeImageViewerRenameWnd")
    } while ($imeRenameWindow -ne [IntPtr]::Zero -and [DateTime]::UtcNow -lt $imeDeadline)
    if ($imeRenameWindow -ne [IntPtr]::Zero -or
        -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($imeMainWindow) -or
        -not (Test-Path -LiteralPath $imeFixture)) {
        throw "IME regression failed: cancelling the rename dialog did not restore the viewer."
    }
    Write-Host "PASS the process keeps a loadable input method and the rename edit control owns keyboard focus."
}
finally {
    if ($imeTestProcess -and -not $imeTestProcess.HasExited) {
        [void]$imeTestProcess.CloseMainWindow()
        if (-not $imeTestProcess.WaitForExit(3000)) {
            Stop-Process -Id $imeTestProcess.Id -Force
            $imeTestProcess.WaitForExit()
        }
    }
    foreach ($imeFile in @(
        $imeTestViewer,
        $imeFixture,
        (Join-Path $imeTestDirectory "YeImageViewer.db"),
        (Join-Path $imeTestDirectory "YeImageViewer.rotations.db"),
        (Join-Path $imeTestDirectory "YeImageViewer.rotations.db.tmp")
    )) {
        if (Test-Path -LiteralPath $imeFile) {
            Remove-Item -LiteralPath $imeFile -Force
        }
    }
    if (Test-Path -LiteralPath $imeTestDirectory) {
        Remove-Item -LiteralPath $imeTestDirectory
    }
}

Write-Host "Checking current-image window restoration..."
$restoreTestDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Current-Restore-" + [Guid]::NewGuid().ToString("N"))
$restoreTestViewer = Join-Path $restoreTestDirectory "YeImageViewer.exe"
$restoreTestProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $restoreTestDirectory)
    Copy-Item -LiteralPath $viewer -Destination $restoreTestViewer
    $restoreTestProcess = Start-Process -FilePath $restoreTestViewer -ArgumentList ('"' + $currentRestoreFixtures[0] + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $restoreTestProcess.Refresh()
    } while (-not $restoreTestProcess.HasExited -and $restoreTestProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($restoreTestProcess.HasExited -or $restoreTestProcess.MainWindowHandle -eq 0 -or -not $restoreTestProcess.Responding) {
        throw "Current-image restore regression failed: viewer did not open the five-image fixture set."
    }

    $restoreWindow = [IntPtr]$restoreTestProcess.MainWindowHandle
    # Esc 默认已改为关闭图片，退出沉浸预览改用文档里写的另一条路：点图片外的背景
    $restoreEnterRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($restoreWindow, [ref]$restoreEnterRect)
    $restoreBackgroundY = [int](($restoreEnterRect.Bottom - $restoreEnterRect.Top) / 2)
    $restoreBackgroundPosition = [IntPtr](($restoreBackgroundY -shl 16) -bor 4)
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0200, [UIntPtr]::Zero, $restoreBackgroundPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0201, [UIntPtr]1, $restoreBackgroundPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0202, [UIntPtr]0, $restoreBackgroundPosition)
    Start-Sleep -Milliseconds 500
    $firstRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($restoreWindow, [ref]$firstRect)
    $firstWidth = $firstRect.Right - $firstRect.Left
    $firstHeight = $firstRect.Bottom - $firstRect.Top
    if ($firstWidth -le 0 -or $firstHeight -le 0) {
        throw "Current-image restore regression failed: first image did not produce a framed client."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0112, [UIntPtr]0xF030, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
    for ($index = 0; $index -lt 4; $index++) {
        [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
        [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 350
    }
    $fifthTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText($restoreWindow, $fifthTitle, $fifthTitle.Capacity)
    if (-not $fifthTitle.ToString().Contains("[5/5]")) {
        throw "Current-image restore regression failed: immersive browsing did not reach image 5."
    }

    # 同上：Esc 现在默认关闭图片，退出沉浸预览改用点击图片外背景。
    # 坐标按当前这张图的实际留白算，不能写死——写死过 x=4，第五张图够宽时那个点
    # 正好落在画面上，点下去不退出沉浸，于是这一条偶发失败。
    $restoreExitPosition = Get-ViewerBackgroundPoint -Window $restoreWindow -Title $fifthTitle.ToString()
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0200, [UIntPtr]::Zero, $restoreExitPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0201, [UIntPtr]1, $restoreExitPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0202, [UIntPtr]0, $restoreExitPosition)

    # 点击进的是操作队列，等窗口真的变回带边框、尺寸不再变化为止，别固定睡一段
    $restoreFramedDeadline = [DateTime]::UtcNow.AddSeconds(8)
    $fifthStyle = 0
    $fifthWidth = 0
    $fifthHeight = 0
    $restoreStableCount = 0
    $restoreLastSize = ""
    do {
        Start-Sleep -Milliseconds 150
        $fifthStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($restoreWindow, -16).ToInt64()
        $fifthRect = New-Object YeImageViewerTestNativeV1365+RECT
        [void][YeImageViewerTestNativeV1365]::GetClientRect($restoreWindow, [ref]$fifthRect)
        $fifthWidth = $fifthRect.Right - $fifthRect.Left
        $fifthHeight = $fifthRect.Bottom - $fifthRect.Top
        $restoreNowSize = "${fifthWidth}x${fifthHeight}"
        if (($fifthStyle -band 0x00C00000) -ne 0 -and $restoreNowSize -eq $restoreLastSize) {
            $restoreStableCount++
        }
        else {
            $restoreStableCount = 0
        }
        $restoreLastSize = $restoreNowSize
    } while ($restoreStableCount -lt 3 -and [DateTime]::UtcNow -lt $restoreFramedDeadline)
    $restoreMonitor = [YeImageViewerTestNativeV1365]::MonitorFromWindow($restoreWindow, 2)
    $restoreMonitorInfo = New-Object YeImageViewerTestNativeV1365+MONITORINFO
    $restoreMonitorInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($restoreMonitorInfo)
    [void][YeImageViewerTestNativeV1365]::GetMonitorInfo($restoreMonitor, [ref]$restoreMonitorInfo)
    $maximumRestoreWidth = [int](($restoreMonitorInfo.Work.Right - $restoreMonitorInfo.Work.Left) * 90 / 100)
    $maximumRestoreHeight = [int](($restoreMonitorInfo.Work.Bottom - $restoreMonitorInfo.Work.Top) * 90 / 100)
    if (($fifthStyle -band 0x00C00000) -eq 0 -or
        $fifthWidth -eq $firstWidth -or $fifthHeight -eq $firstHeight -or
        [Math]::Abs($fifthWidth - $maximumRestoreWidth) -gt 1 -or
        [Math]::Abs($fifthHeight - $maximumRestoreHeight) -gt 1) {
        throw "Current-image restore regression failed: image 5 did not determine the capped framed size."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0100, [UIntPtr]0x25, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0101, [UIntPtr]0x25, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 450
    $normalBrowseRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($restoreWindow, [ref]$normalBrowseRect)
    if (($normalBrowseRect.Right - $normalBrowseRect.Left) -ne $fifthWidth -or
        ($normalBrowseRect.Bottom - $normalBrowseRect.Top) -ne $fifthHeight) {
        throw "Current-image restore regression failed: normal browsing changed the restored frame."
    }
    Write-Host "PASS immersive image 5 determines the capped framed size and normal browsing keeps it fixed."
}
finally {
    if ($restoreTestProcess -and -not $restoreTestProcess.HasExited) {
        [void]$restoreTestProcess.CloseMainWindow()
        if (-not $restoreTestProcess.WaitForExit(3000)) {
            Stop-Process -Id $restoreTestProcess.Id -Force
            $restoreTestProcess.WaitForExit()
        }
    }
    foreach ($restoreFile in @(
        $restoreTestViewer,
        (Join-Path $restoreTestDirectory "YeImageViewer.db"),
        (Join-Path $restoreTestDirectory "YeImageViewer.rotations.db"),
        (Join-Path $restoreTestDirectory "YeImageViewer.rotations.db.tmp")
    )) {
        if (Test-Path -LiteralPath $restoreFile) {
            Remove-Item -LiteralPath $restoreFile -Force
        }
    }
    if (Test-Path -LiteralPath $restoreTestDirectory) {
        Remove-Item -LiteralPath $restoreTestDirectory
    }
}

# 通过设置界面改「关闭图片」的快捷键。这一项在快捷键页的键盘区第 5 行（下标 4）：
# 几何见 SettingLayout 的 SHORTCUT_KEYBOARD_ROW_Y / SHORTCUT_KEY_CELL_X / SHORTCUT_CLEAR_X。
# 坐标写在固定的 620x620 逻辑画布里，鼠标消息带的是物理客户区像素，倍率按实际客户区
# 宽度反推——不能直接用 DPI，窗口放不下时程序会自己压低缩放。
function Set-CloseImageShortcut {
    param(
        [Parameter(Mandatory)][IntPtr]$ViewerWindow,
        [Parameter(Mandatory)][int]$ProcessId,
        [Parameter(Mandatory)][bool]$Assign
    )
    [void][YeImageViewerTestNativeV1365]::SendMessage($ViewerWindow, 0x0100, [UIntPtr]0x72, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $settingWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$ProcessId, "YeImageViewerSettingWnd")
    } while ($settingWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($settingWindow -eq [IntPtr]::Zero) {
        throw "Shortcut regression failed: F3 did not open the Shortcuts page."
    }
    $rect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($settingWindow, [ref]$rect)
    $width = $rect.Right - $rect.Left
    if ($width -le 0) { $width = 620 }
    $rowCenterY = 52 + 288 + 4 * 40 + 20
    $targetX = if ($Assign) { 36 + 256 + 125 } else { 36 + 512 + 15 }
    $x = [int][Math]::Round($targetX * $width / 620.0)
    $y = [int][Math]::Round($rowCenterY * $width / 620.0)
    $position = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($settingWindow, 0x0201, [UIntPtr]1, $position)
    [void][YeImageViewerTestNativeV1365]::SendMessage($settingWindow, 0x0202, [UIntPtr]0, $position)
    if ($Assign) {
        # 点按键格子进入录制，再按 Esc 把它录回去
        Start-Sleep -Milliseconds 150
        [void][YeImageViewerTestNativeV1365]::SendMessage($settingWindow, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($settingWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
}

Write-Host "Opening the SVG background-selector fixture..."
$viewerProcess = $null
try {
    $viewerProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $sharpSvgFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)

    do {
        Start-Sleep -Milliseconds 200
        $viewerProcess.Refresh()
    } while (-not $viewerProcess.HasExited -and $viewerProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($viewerProcess.HasExited -or $viewerProcess.MainWindowHandle -eq 0 -or -not $viewerProcess.Responding) {
        throw "Background-selector regression failed: viewer did not open a responsive SVG window."
    }

    $backgroundCommands = @(1100, 1101, 1102, 1103, 1100)
    foreach ($command in $backgroundCommands) {
        [void][YeImageViewerTestNativeV1365]::SendMessage(
            [IntPtr]$viewerProcess.MainWindowHandle, 0x0111, [UIntPtr]$command, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 150
        $viewerProcess.Refresh()
        if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
            $processState = if ($viewerProcess.HasExited) {
                $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$viewerProcess.ExitCode), 0)
                "exited with 0x$('{0:X8}' -f $unsignedExitCode)"
            } else {
                "stopped responding"
            }
            throw "Background-selector regression failed after menu command ${command}: $processState."
        }
    }

    Write-Host "PASS SVG background modes switch without exiting or hanging."

    $window = [IntPtr]$viewerProcess.MainWindowHandle
    # Esc 默认绑在「关闭图片」上，先在快捷键页清掉它，下面这串才是 Esc 的回退链：
    # 退出沉浸预览 -> 退出全屏 -> 还原最大化窗口 -> 普通窗口里什么都不做。
    Set-CloseImageShortcut -ViewerWindow $window -ProcessId $viewerProcess.Id -Assign $false
    $presentationStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($presentationStyle -band 0x00C00000) -ne 0) {
        throw "Escape regression failed: SVG did not begin in borderless presentation mode."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $presentationRestoredStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($presentationRestoredStyle -band 0x00C00000) -eq 0) {
        throw "Escape regression failed: Escape did not leave presentation mode."
    }
    Write-Host "PASS Escape leaves the initial immersive presentation."

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x46, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $fullScreenStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($fullScreenStyle -band 0x00C00000) -ne 0) {
        throw "Escape regression failed: F did not enter borderless fullscreen."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $restoredStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or ($restoredStyle -band 0x00C00000) -eq 0) {
        throw "Escape regression failed: Escape did not restore the pre-fullscreen window."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0112, [UIntPtr]0xF030, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $maximizePresentationStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    $maximizePresentationExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -20).ToInt64()
    if (($maximizePresentationStyle -band 0x00C00000) -ne 0 -or
        ($maximizePresentationExtendedStyle -band 0x00200000) -eq 0 -or
        [YeImageViewerTestNativeV1365]::IsZoomed($window)) {
        throw "Presentation regression failed: maximize did not enter borderless presentation mode."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $maximizeRestoredStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    $maximizeRestoredExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -20).ToInt64()
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or ($maximizeRestoredStyle -band 0x00C00000) -eq 0 -or
        ($maximizeRestoredExtendedStyle -band 0x00200000) -eq 0 -or
        [YeImageViewerTestNativeV1365]::IsZoomed($window)) {
        throw "Presentation regression failed: Escape did not restore the pre-presentation frame."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Escape regression failed: a cleared Escape closed the normal window."
    }
    Write-Host "PASS with the shortcut cleared, Escape walks back presentation, fullscreen, and maximize."

    # 装回默认绑定：录制时直接按 Esc，验证 Esc 本身能被录成快捷键，也让后续用例回到默认状态
    Set-CloseImageShortcut -ViewerWindow $window -ProcessId $viewerProcess.Id -Assign $true
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    $viewerProcess.Refresh()
    if (-not $viewerProcess.HasExited) {
        throw "Escape regression failed: Escape could not be recorded back onto the close-image action."
    }
    Write-Host "PASS Escape itself can be recorded back onto the close-image shortcut."
    $viewerProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $sharpSvgFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 150
        $viewerProcess.Refresh()
    } while (-not $viewerProcess.HasExited -and $viewerProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $deadline)
    if ($viewerProcess.HasExited -or $viewerProcess.MainWindowHandle -eq 0) {
        throw "Escape regression failed: the viewer did not reopen after the close-image check."
    }
    $window = [IntPtr]$viewerProcess.MainWindowHandle
    # 重开后又是沉浸预览，点图片外背景退回带边框窗口，后面的用例都基于这个状态
    $reopenRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$reopenRect)
    $reopenY = [int](($reopenRect.Bottom - $reopenRect.Top) / 2)
    $reopenPosition = [IntPtr](($reopenY -shl 16) -bor 4)
    # 点击进的是操作队列，要等绘制循环空出来才被消费；这张 SVG 首次渲染要好几秒，
    # 固定等 400 毫秒不够，改成轮询到出现边框为止。
    $framedDeadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0200, [UIntPtr]::Zero, $reopenPosition)
        [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0201, [UIntPtr]1, $reopenPosition)
        [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0202, [UIntPtr]0, $reopenPosition)
        Start-Sleep -Milliseconds 400
        $framedStyleNow = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    } while (($framedStyleNow -band 0x00C00000) -eq 0 -and [DateTime]::UtcNow -lt $framedDeadline)
    if (($framedStyleNow -band 0x00C00000) -eq 0) {
        throw "Escape regression failed: the reopened viewer did not return to a framed window."
    }

    $clientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$clientRect)
    $clientWidth = $clientRect.Right - $clientRect.Left
    $clientHeight = $clientRect.Bottom - $clientRect.Top
    $windowDpi = [YeImageViewerTestNativeV1365]::GetDpiForWindow($window)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $switchedClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$switchedClientRect)
    if (($switchedClientRect.Right - $switchedClientRect.Left) -ne $clientWidth -or
        ($switchedClientRect.Bottom - $switchedClientRect.Top) -ne $clientHeight) {
        throw "Framed-size regression failed: normal browsing changed the current fixed client size."
    }
    Write-Host "PASS normal image changes keep the current framed window size."

    $targetClientWidth = $clientWidth
    $targetClientHeight = $clientHeight
    $toolbarScale = Get-ToolbarScale -CanvasWidth $targetClientWidth -Dpi $windowDpi
    $toolbarWidth = Get-ScaledValue -Value 615 -Scale $toolbarScale
    $toolbarHeight = Get-ScaledValue -Value 50 -Scale $toolbarScale
    $toolbarBottom = Get-ScaledValue -Value 20 -Scale $toolbarScale
    $toolbarX = [int]($targetClientWidth / 2)
    $toolbarY = $targetClientHeight - $toolbarBottom - [int]($toolbarHeight / 2)
    $mousePosition = [IntPtr](($toolbarY -shl 16) -bor ($toolbarX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0200, [UIntPtr]::Zero, $mousePosition)
    Start-Sleep -Milliseconds 250
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Overlay regression failed: bottom toolbar hover was not responsive."
    }
    Write-Host "PASS centered reference toolbar hover remains responsive."

    $scaledToolbarWidth = Get-ScaledValue -Value 615 -Scale $toolbarScale
    $scaledButtonSize = Get-ScaledValue -Value 34 -Scale $toolbarScale
    $scaledPadding = Get-ScaledValue -Value 8 -Scale $toolbarScale
    $scaledZoomTextOffset = Get-ScaledValue -Value 526 -Scale $toolbarScale
    $scaledZoomTextWidth = Get-ScaledValue -Value 50 -Scale $toolbarScale
    $scaledNextOffset = Get-ScaledValue -Value 300 -Scale $toolbarScale
    $toolbarLeft = [int][Math]::Floor(($targetClientWidth - $scaledToolbarWidth) / 2.0)
    $zoomTextX = $toolbarLeft + $scaledPadding + $scaledZoomTextOffset +
        [int][Math]::Floor($scaledZoomTextWidth / 2.0)
    $zoomTextPosition = [IntPtr](($toolbarY -shl 16) -bor ($zoomTextX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0200, [UIntPtr]::Zero, $zoomTextPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0201, [UIntPtr]1, $zoomTextPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0202, [UIntPtr]::Zero, $zoomTextPosition)
    Start-Sleep -Milliseconds 100
    foreach ($key in @(0x31, 0x35, 0x30, 0x0D)) {
        [void][YeImageViewerTestNativeV1365]::SendMessage(
            $window, 0x0100, [UIntPtr]$key, [IntPtr]::Zero)
        [void][YeImageViewerTestNativeV1365]::SendMessage(
            $window, 0x0101, [UIntPtr]$key, [IntPtr]::Zero)
    }
    $zoomEditTitle = New-Object Text.StringBuilder 1024
    $zoomEditDeadline = [DateTime]::UtcNow.AddSeconds(4)
    do {
        Start-Sleep -Milliseconds 100
        [void]$zoomEditTitle.Clear()
        [void][YeImageViewerTestNativeV1365]::GetWindowText(
            $window, $zoomEditTitle, $zoomEditTitle.Capacity)
    } while ($zoomEditTitle.ToString() -notmatch '\s150%$' -and
        [DateTime]::UtcNow -lt $zoomEditDeadline)
    if ($zoomEditTitle.ToString() -notmatch '\s150%$') {
        throw "Zoom editor regression failed: clicking and entering 150 did not set an exact 150% zoom. Actual: $($zoomEditTitle.ToString())"
    }

    # A click elsewhere commits the edit, while Escape cancels the next edit.
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0200, [UIntPtr]::Zero, $zoomTextPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0201, [UIntPtr]1, $zoomTextPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0202, [UIntPtr]::Zero, $zoomTextPosition)
    Start-Sleep -Milliseconds 100
    foreach ($key in @(0x31, 0x37, 0x35)) {
        [void][YeImageViewerTestNativeV1365]::SendMessage(
            $window, 0x0100, [UIntPtr]$key, [IntPtr]::Zero)
        [void][YeImageViewerTestNativeV1365]::SendMessage(
            $window, 0x0101, [UIntPtr]$key, [IntPtr]::Zero)
    }
    # 工具栏上的空白处：沉浸按钮结束在 489、缩小按钮从 502 开始，495 正好在两者之间。
    # 插入「适应图片」之后按钮整体后移，原来的 458 落进了沉浸按钮里，
    # 一点就切进沉浸模式、缩放被重算，量出来的自然不是 175%。
    $bareToolbarX = $toolbarLeft + $scaledPadding +
        [int][Math]::Floor((495 * $toolbarScale + 500) / 1000.0)
    $bareToolbarPosition = [IntPtr](($toolbarY -shl 16) -bor ($bareToolbarX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0200, [UIntPtr]::Zero, $bareToolbarPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0201, [UIntPtr]1, $bareToolbarPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0202, [UIntPtr]::Zero, $bareToolbarPosition)
    $zoomBlurDeadline = [DateTime]::UtcNow.AddSeconds(4)
    do {
        Start-Sleep -Milliseconds 100
        [void]$zoomEditTitle.Clear()
        [void][YeImageViewerTestNativeV1365]::GetWindowText(
            $window, $zoomEditTitle, $zoomEditTitle.Capacity)
    } while ($zoomEditTitle.ToString() -notmatch '\s175%$' -and
        [DateTime]::UtcNow -lt $zoomBlurDeadline)
    if ($zoomEditTitle.ToString() -notmatch '\s175%$') {
        throw "Zoom editor regression failed: clicking outside did not commit 175%. Actual: $($zoomEditTitle.ToString())"
    }

    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0200, [UIntPtr]::Zero, $zoomTextPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0201, [UIntPtr]1, $zoomTextPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0202, [UIntPtr]::Zero, $zoomTextPosition)
    Start-Sleep -Milliseconds 100
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $window, 0x0100, [UIntPtr]0x32, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    [void]$zoomEditTitle.Clear()
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $window, $zoomEditTitle, $zoomEditTitle.Capacity)
    if ($zoomEditTitle.ToString() -notmatch '\s175%$') {
        throw "Zoom editor regression failed: Escape did not cancel the pending edit. Actual: $($zoomEditTitle.ToString())"
    }
    Write-Host "PASS toolbar percentage supports exact entry, outside-click commit, and Escape cancel."

    $nextButtonX = $toolbarLeft + $scaledPadding + $scaledNextOffset +
        [int][Math]::Floor($scaledButtonSize / 2.0)
    $nextButtonPosition = [IntPtr](($toolbarY -shl 16) -bor ($nextButtonX -band 0xFFFF))
    $titleBeforeToolbarClick = New-Object Text.StringBuilder 1024
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $window, $titleBeforeToolbarClick, $titleBeforeToolbarClick.Capacity)
    # Queue the synthetic move/down/up as one ordered input sequence. A
    # synchronous fake move can otherwise arm TrackMouseEvent and let a real
    # WM_MOUSELEAVE reset the hover target before the synthetic click arrives.
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0200, [UIntPtr]::Zero, $nextButtonPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0201, [UIntPtr]1, $nextButtonPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0202, [UIntPtr]::Zero, $nextButtonPosition)
    $titleAfterToolbarClick = New-Object Text.StringBuilder 1024
    $toolbarSwitchDeadline = [DateTime]::UtcNow.AddSeconds(4)
    do {
        Start-Sleep -Milliseconds 100
        [void]$titleAfterToolbarClick.Clear()
        [void][YeImageViewerTestNativeV1365]::GetWindowText(
            $window, $titleAfterToolbarClick, $titleAfterToolbarClick.Capacity)
    } while ($titleBeforeToolbarClick.ToString() -eq $titleAfterToolbarClick.ToString() -and
        [DateTime]::UtcNow -lt $toolbarSwitchDeadline)
    if ($titleBeforeToolbarClick.ToString() -eq $titleAfterToolbarClick.ToString()) {
        throw "Overlay regression failed: the centered Next button did not change images. dpi=$windowDpi client=${clientWidth}x${clientHeight} click=${nextButtonX},${toolbarY} title=$($titleAfterToolbarClick.ToString())"
    }
    Write-Host "PASS centered toolbar Next button changes images through its precise hit target."

    $scaledPlayOffset = [int][Math]::Floor((265 * $toolbarScale + 500) / 1000.0)
    $playButtonX = $toolbarLeft + $scaledPadding + $scaledPlayOffset +
        [int][Math]::Floor($scaledButtonSize / 2.0)
    $playButtonPosition = [IntPtr](($toolbarY -shl 16) -bor ($playButtonX -band 0xFFFF))
    $titleBeforeSlideshow = $titleAfterToolbarClick.ToString()
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0200, [UIntPtr]::Zero, $playButtonPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0201, [UIntPtr]1, $playButtonPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0202, [UIntPtr]::Zero, $playButtonPosition)
    $titleDuringSlideshow = New-Object Text.StringBuilder 1024
    $slideshowDeadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 100
        [void]$titleDuringSlideshow.Clear()
        [void][YeImageViewerTestNativeV1365]::GetWindowText(
            $window, $titleDuringSlideshow, $titleDuringSlideshow.Capacity)
    } while ($titleBeforeSlideshow -eq $titleDuringSlideshow.ToString() -and
        [DateTime]::UtcNow -lt $slideshowDeadline)
    if ($titleBeforeSlideshow -eq $titleDuringSlideshow.ToString()) {
        throw "Slideshow regression failed: Play did not advance to the next image."
    }

    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0200, [UIntPtr]::Zero, $playButtonPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0201, [UIntPtr]1, $playButtonPosition)
    [void][YeImageViewerTestNativeV1365]::PostMessage(
        $window, 0x0202, [UIntPtr]::Zero, $playButtonPosition)
    Start-Sleep -Milliseconds 3500
    $titleAfterPause = New-Object Text.StringBuilder 1024
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $window, $titleAfterPause, $titleAfterPause.Capacity)
    if ($titleAfterPause.ToString() -ne $titleDuringSlideshow.ToString()) {
        throw "Slideshow regression failed: Pause did not keep the current image stable."
    }
    Write-Host "PASS centered Play/Pause starts and stops the three-second slideshow."

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0112, [UIntPtr]0xF030, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    $presentationAfterMaximize = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($presentationAfterMaximize -band 0x00C00000) -ne 0) {
        throw "Presentation overlay regression failed: maximize did not re-enter presentation."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0111, [UIntPtr]1010, [IntPtr]::Zero)
    $settingDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $presentationSettingWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$viewerProcess.Id, "YeImageViewerSettingWnd")
    } while ($presentationSettingWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $settingDeadline)
    if ($presentationSettingWindow -eq [IntPtr]::Zero) {
        throw "Presentation overlay regression failed: Settings did not open over presentation."
    }
    [void][YeImageViewerTestNativeV1365]::SetWindowPos(
        $presentationSettingWindow, [IntPtr]::Zero, 80, 80, 0, 0, 0x0015)
    [void][YeImageViewerTestNativeV1365]::SetWindowPos(
        $presentationSettingWindow, [IntPtr]::Zero, 420, 180, 0, 0, 0x0015)
    Start-Sleep -Milliseconds 300
    $viewerProcess.Refresh()
    $styleAfterSettingMove = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding -or
        ($styleAfterSettingMove -band 0x00C00000) -ne 0) {
        throw "Presentation overlay regression failed after moving Settings over the image."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $presentationSettingWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Write-Host "PASS moving Settings over the image keeps presentation stable."

    $presentationClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$presentationClientRect)
    $presentationWidth = $presentationClientRect.Right - $presentationClientRect.Left
    # The close button scales with DPI too, so derive its centre instead of
    # assuming the 96-DPI 42px button at a 12px margin.
    $presentationDpi = [YeImageViewerTestNativeV1365]::GetDpiForWindow($window)
    $presentationScale = [int][Math]::Floor($presentationDpi * 1000 / 96)
    $closeSize = Get-ScaledValue -Value 42 -Scale $presentationScale
    $closeMargin = Get-ScaledValue -Value 12 -Scale $presentationScale
    $closeX = $presentationWidth - $closeMargin - [int][Math]::Floor($closeSize / 2)
    $closeY = $closeMargin + [int][Math]::Floor($closeSize / 2)
    $closePosition = [IntPtr](($closeY -shl 16) -bor ($closeX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0200, [UIntPtr]::Zero, $closePosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0201, [UIntPtr]1, $closePosition)
    Start-Sleep -Milliseconds 250
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Presentation close regression failed: mouse-down closed the viewer before mouse-up and could click through to File Explorer."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0202, [UIntPtr]0, $closePosition)
    if (-not $viewerProcess.WaitForExit(3000)) {
        throw "Presentation close regression failed: persistent close button did not exit."
    }
    Write-Host "PASS presentation close waits for mouse-up, preventing click-through, then exits cleanly."
}
finally {
    if ($viewerProcess -and -not $viewerProcess.HasExited) {
        [void]$viewerProcess.CloseMainWindow()
        if (-not $viewerProcess.WaitForExit(3000)) {
            Stop-Process -Id $viewerProcess.Id -Force
            $viewerProcess.WaitForExit()
        }
    }
}

$existingViewer = Get-CimInstance Win32_Process -Filter "Name='YeImageViewer.exe'" |
    Where-Object { $_.ExecutablePath -eq $viewer }
if ($existingViewer) {
    throw "Close the YeImageViewer instance running from $viewer before starting the crash regression test."
}

Write-Host "Opening the DJI MotionPhoto crash fixture..."
$viewerProcess = $null
try {
    $viewerProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $crashFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)

    do {
        Start-Sleep -Milliseconds 200
        $viewerProcess.Refresh()
    } while (-not $viewerProcess.HasExited -and $viewerProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($viewerProcess.HasExited) {
        $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$viewerProcess.ExitCode), 0)
        throw "DJI MotionPhoto regression failed: viewer exited with 0x$('{0:X8}' -f $unsignedExitCode)."
    }
    if ($viewerProcess.MainWindowHandle -eq 0) {
        throw "DJI MotionPhoto regression failed: viewer did not create a window before the timeout."
    }
    if (-not $viewerProcess.Responding) {
        throw "DJI MotionPhoto regression failed: viewer window is not responding."
    }

    Start-Sleep -Seconds 2
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "DJI MotionPhoto regression failed: viewer did not remain responsive."
    }

    Write-Host "PASS DJI MotionPhoto remains open and responsive."
}
finally {
    if ($viewerProcess -and -not $viewerProcess.HasExited) {
        [void]$viewerProcess.CloseMainWindow()
        if (-not $viewerProcess.WaitForExit(3000)) {
            Stop-Process -Id $viewerProcess.Id -Force
            $viewerProcess.WaitForExit()
        }
    }
}

Write-Host "Opening the enlarged-text interpolation fixture..."
$viewerProcess = $null
try {
    $viewerProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $jaggedFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)

    do {
        Start-Sleep -Milliseconds 200
        $viewerProcess.Refresh()
    } while (-not $viewerProcess.HasExited -and $viewerProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($viewerProcess.HasExited) {
        $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$viewerProcess.ExitCode), 0)
        throw "Raster interpolation regression failed: viewer exited with 0x$('{0:X8}' -f $unsignedExitCode)."
    }
    if ($viewerProcess.MainWindowHandle -eq 0 -or -not $viewerProcess.Responding) {
        throw "Raster interpolation regression failed: viewer did not open a responsive window."
    }

    Start-Sleep -Seconds 1
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Raster interpolation regression failed: viewer did not remain responsive."
    }

    Write-Host "PASS enlarged-text interpolation fixture remains open and responsive."
}
finally {
    if ($viewerProcess -and -not $viewerProcess.HasExited) {
        [void]$viewerProcess.CloseMainWindow()
        if (-not $viewerProcess.WaitForExit(3000)) {
            Stop-Process -Id $viewerProcess.Id -Force
            $viewerProcess.WaitForExit()
        }
    }
}

Write-Host "Checking copy and move to a configured folder..."
# 这两条菜单项曾经整个不可用：1200-1204 / 1210-1214 这段命令 ID 压根没有处理分支，
# 而「移动」后来复用了 deleteImg——那条路要求文件还在原处，移动完文件已经不在了。
# 当时修完没有留下回归测试，这里补上：用真窗口发真命令，查文件系统的真实结果。
$targetTestRoot = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Targets-" + [Guid]::NewGuid().ToString("N"))
$targetProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $targetTestRoot)
    # 用独立目录里的一份 exe，配置就不会和开发机上的真配置搅在一起
    $targetViewer = Join-Path $targetTestRoot "YeImageViewer.exe"
    Copy-Item -LiteralPath $viewer -Destination $targetViewer

    $targetPictures = Join-Path $targetTestRoot "pics"
    $targetFolder = Join-Path $targetTestRoot "collected"
    [void](New-Item -ItemType Directory -Path $targetPictures)
    [void](New-Item -ItemType Directory -Path $targetFolder)
    foreach ($name in @("a.png", "b.png", "c.png")) {
        Copy-Item -LiteralPath $commonPngFixture -Destination (Join-Path $targetPictures $name)
    }
    $firstImage = Join-Path $targetPictures "a.png"

    # 先跑一次让程序把 4096 字节的设置区写出来，再往文本区追加目标文件夹。
    # 直接自己造整个文件不如让程序造——设置区是固定结构，手写容易对不上。
    $seedProcess = Start-Process -FilePath $targetViewer -ArgumentList ('"' + $firstImage + '"') -PassThru
    $seedDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $seedProcess.Refresh()
    } while (-not $seedProcess.HasExited -and $seedProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $seedDeadline)
    if ($seedProcess.HasExited -or $seedProcess.MainWindowHandle -eq 0) {
        throw "Copy/move regression failed: the viewer did not open while seeding its config."
    }
    [void]$seedProcess.CloseMainWindow()
    if (-not $seedProcess.WaitForExit(5000)) {
        Stop-Process -Id $seedProcess.Id -Force
        [void]$seedProcess.WaitForExit(3000)
    }
    Start-Sleep -Milliseconds 400

    $targetDatabase = Join-Path $targetTestRoot "YeImageViewer.db"
    if (-not (Test-Path -LiteralPath $targetDatabase -PathType Leaf)) {
        throw "Copy/move regression failed: the viewer did not write its configuration file."
    }
    # 文本区就在 4096 字节的设置区之后，一行一个 Key=Value，UTF-8（见 ConfigFile.h）
    $databaseBytes = [IO.File]::ReadAllBytes($targetDatabase)
    if ($databaseBytes.Length -lt 4096) {
        throw "Copy/move regression failed: the configuration file is shorter than its 4096-byte header."
    }
    $targetLines = "TargetCount=1`r`nTargetActive=0`r`nTarget0=$targetFolder`r`n"
    $stream = [IO.File]::Open($targetDatabase, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite)
    try {
        [void]$stream.Seek(4096, [IO.SeekOrigin]::Begin)
        $lineBytes = [Text.Encoding]::UTF8.GetBytes($targetLines)
        $stream.Write($lineBytes, 0, $lineBytes.Length)
        $stream.SetLength(4096 + $lineBytes.Length)
    }
    finally {
        $stream.Dispose()
    }

    $targetProcess = Start-Process -FilePath $targetViewer -ArgumentList ('"' + $firstImage + '"') -PassThru
    $openDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $targetProcess.Refresh()
    } while (-not $targetProcess.HasExited -and $targetProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $openDeadline)
    if ($targetProcess.HasExited -or $targetProcess.MainWindowHandle -eq 0) {
        throw "Copy/move regression failed: the viewer did not open the first image."
    }
    Start-Sleep -Milliseconds 800

    $targetWindow = $targetProcess.MainWindowHandle
    $targetTitle = New-Object Text.StringBuilder 512
    [void][YeImageViewerTestNativeV1365]::GetWindowText($targetWindow, $targetTitle, 512)
    if ($targetTitle.ToString() -notmatch "\[\d+/3\]") {
        throw ("Copy/move regression failed: expected a three-image list, got " +
            "'$($targetTitle.ToString())'.")
    }

    # 「复制到第一个目标」= ContextMenu::copyToTargetFirst
    [void][YeImageViewerTestNativeV1365]::PostMessage($targetWindow, 0x0111, [UIntPtr]1200, [IntPtr]::Zero)
    $copiedFile = Join-Path $targetFolder "a.png"
    $copyDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while (-not (Test-Path -LiteralPath $copiedFile -PathType Leaf) -and
        [DateTime]::UtcNow -lt $copyDeadline) {
        Start-Sleep -Milliseconds 150
    }
    if (-not (Test-Path -LiteralPath $copiedFile -PathType Leaf)) {
        throw "Copy/move regression failed: copying to the configured folder produced no file."
    }
    if (-not (Test-Path -LiteralPath $firstImage -PathType Leaf)) {
        throw "Copy/move regression failed: copying removed the source file."
    }
    $targetProcess.Refresh()
    if ($targetProcess.HasExited) {
        throw "Copy/move regression failed: the viewer exited while copying."
    }
    [void][YeImageViewerTestNativeV1365]::GetWindowText($targetWindow, $targetTitle, 512)
    if ($targetTitle.ToString() -notmatch "\[\d+/3\]") {
        throw ("Copy/move regression failed: copying must not change the image list, got " +
            "'$($targetTitle.ToString())'.")
    }

    # 「移动到第一个目标」= ContextMenu::moveToTargetFirst。目标里已经有 a.png 了，
    # 所以这一份应当让路成 a (2).png，源文件要消失，列表要少一张。
    [void][YeImageViewerTestNativeV1365]::PostMessage($targetWindow, 0x0111, [UIntPtr]1210, [IntPtr]::Zero)
    $movedFile = Join-Path $targetFolder "a (2).png"
    $moveDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while ((Test-Path -LiteralPath $firstImage -PathType Leaf) -and
        [DateTime]::UtcNow -lt $moveDeadline) {
        Start-Sleep -Milliseconds 150
    }
    if (Test-Path -LiteralPath $firstImage -PathType Leaf) {
        throw "Copy/move regression failed: moving left the source file in place."
    }
    if (-not (Test-Path -LiteralPath $movedFile -PathType Leaf)) {
        throw ("Copy/move regression failed: the moved file did not get out of the way as " +
            "'a (2).png'; the target folder holds " +
            (((Get-ChildItem -LiteralPath $targetFolder -File).Name) -join ", ") + ".")
    }
    $targetProcess.Refresh()
    if ($targetProcess.HasExited) {
        throw "Copy/move regression failed: the viewer exited while moving."
    }
    if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($targetWindow)) {
        throw "Copy/move regression failed: the viewer stayed disabled after moving."
    }

    # 列表要缩到两张，而且当前显示的不再是被移走的那一张
    $listDeadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        [void][YeImageViewerTestNativeV1365]::GetWindowText($targetWindow, $targetTitle, 512)
    } while ($targetTitle.ToString() -notmatch "\[\d+/2\]" -and [DateTime]::UtcNow -lt $listDeadline)
    if ($targetTitle.ToString() -notmatch "\[\d+/2\]") {
        throw ("Copy/move regression failed: the moved image stayed in the list, title is " +
            "'$($targetTitle.ToString())'.")
    }
    if ($targetTitle.ToString() -match "\ba\.png\b") {
        throw ("Copy/move regression failed: the viewer still shows the moved file, title is " +
            "'$($targetTitle.ToString())'.")
    }

    Write-Host ("PASS copy keeps the source and the list, move takes the file away, renames " +
        "around a collision, and leaves the viewer usable.")
}
finally {
    if ($targetProcess -and -not $targetProcess.HasExited) {
        [void]$targetProcess.CloseMainWindow()
        if (-not $targetProcess.WaitForExit(4000)) {
            Stop-Process -Id $targetProcess.Id -Force
            [void]$targetProcess.WaitForExit(3000)
        }
    }
    if (Test-Path -LiteralPath $targetTestRoot) {
        Remove-Item -LiteralPath $targetTestRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Checking delete-to-recycle-bin, clipboard copy, and the info overlay..."
# 删除、复制到剪贴板、EXIF 信息这三样此前一条自动化测试都没有。删除尤其值得盯：
# 它动的是用户的文件，而且默认会先弹确认框——确认框那一步也是行为的一部分。
#
# 这一环会改写剪贴板（复制图像/信息本来就是往剪贴板写），跑完原有内容不保证还在。
$fileOpsRoot = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-FileOps-" + [Guid]::NewGuid().ToString("N"))
$fileOpsProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $fileOpsRoot)
    $fileOpsViewer = Join-Path $fileOpsRoot "YeImageViewer.exe"
    Copy-Item -LiteralPath $viewer -Destination $fileOpsViewer

    $fileOpsPictures = Join-Path $fileOpsRoot "pics"
    [void](New-Item -ItemType Directory -Path $fileOpsPictures)
    foreach ($name in @("one.png", "two.png", "three.png")) {
        Copy-Item -LiteralPath $commonPngFixture -Destination (Join-Path $fileOpsPictures $name)
    }
    $fileOpsImage = Join-Path $fileOpsPictures "one.png"

    $fileOpsProcess = Start-Process -FilePath $fileOpsViewer -ArgumentList ('"' + $fileOpsImage + '"') -PassThru
    $fileOpsDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $fileOpsProcess.Refresh()
    } while (-not $fileOpsProcess.HasExited -and $fileOpsProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $fileOpsDeadline)
    if ($fileOpsProcess.HasExited -or $fileOpsProcess.MainWindowHandle -eq 0) {
        throw "File-operation regression failed: the viewer did not open."
    }
    Start-Sleep -Milliseconds 800
    $fileOpsWindow = $fileOpsProcess.MainWindowHandle

    # ---- 复制图像数据到剪贴板 = ContextMenu::copyImageData ----
    # 不往剪贴板里写哨兵：那是去和程序抢同一个全机器资源，程序那边
    # OpenClipboard 一失败就什么都没写进去。改成记下版本号，等它变。
    $clipboardBefore = [YeImageViewerTestNativeV1365]::GetClipboardSequenceNumber()
    [void][YeImageViewerTestNativeV1365]::PostMessage($fileOpsWindow, 0x0111, [UIntPtr]1003, [IntPtr]::Zero)
    $clipboardSize = ""
    $clipboardDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ($clipboardSize -eq "" -and [DateTime]::UtcNow -lt $clipboardDeadline) {
        Start-Sleep -Milliseconds 200
        if ([YeImageViewerTestNativeV1365]::GetClipboardSequenceNumber() -ne $clipboardBefore) {
            $clipboardSize = [YeImageViewerTestNativeV1365]::ClipboardImageSize()
        }
    }
    if ($clipboardSize -eq "") {
        throw "File-operation regression failed: copying the image put no bitmap on the clipboard."
    }
    if ($clipboardSize -ne "160x80") {
        throw ("File-operation regression failed: the clipboard image is ${clipboardSize}, " +
            "expected 160x80.")
    }

    # ---- 复制图像信息 = ContextMenu::copyImageInfo，内容要对得上这张图 ----
    $infoBefore = [YeImageViewerTestNativeV1365]::GetClipboardSequenceNumber()
    [void][YeImageViewerTestNativeV1365]::PostMessage($fileOpsWindow, 0x0111, [UIntPtr]1001, [IntPtr]::Zero)
    $clipboardText = ""
    $textDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $textDeadline) {
        Start-Sleep -Milliseconds 200
        if ([YeImageViewerTestNativeV1365]::GetClipboardSequenceNumber() -eq $infoBefore) {
            continue
        }
        $clipboardText = [YeImageViewerTestNativeV1365]::ClipboardText()
        if ($clipboardText) { break }
    }
    if (-not $clipboardText) {
        throw "File-operation regression failed: copying the image info put nothing on the clipboard."
    }
    if ($clipboardText -notmatch "one\.png" -or $clipboardText -notmatch "160" -or
        $clipboardText -notmatch "80") {
        throw ("File-operation regression failed: the copied info does not describe the image: " +
            "'" + ($clipboardText -replace "`r?`n", " | ") + "'")
    }

    # ---- EXIF 信息面板开关 = ContextMenu::toggleExifDisplay，来回切不能把界面搞死 ----
    foreach ($round in 1..2) {
        [void][YeImageViewerTestNativeV1365]::PostMessage($fileOpsWindow, 0x0111, [UIntPtr]1004, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 500
        $fileOpsProcess.Refresh()
        if ($fileOpsProcess.HasExited) {
            throw "File-operation regression failed: toggling the info overlay exited the viewer (round $round)."
        }
        $overlayResult = [UIntPtr]::Zero
        if ([YeImageViewerTestNativeV1365]::SendMessageTimeout($fileOpsWindow, 0x0000,
                [UIntPtr]::Zero, [IntPtr]::Zero, 0x0002, 3000, [ref]$overlayResult) -eq [IntPtr]::Zero) {
            throw "File-operation regression failed: the viewer stopped answering after toggling the info overlay (round $round)."
        }
    }

    # ---- 删除到回收站 = ContextMenu::deleteImage ----
    # 默认 isNoteBeforeDelete 为真，所以会先弹 MB_YESNO 确认框（默认按钮是「否」）。
    # 先验「否」确实不删，再验「是」真的删。
    $deleteTarget = $fileOpsImage
    [void][YeImageViewerTestNativeV1365]::PostMessage($fileOpsWindow, 0x0111, [UIntPtr]1006, [IntPtr]::Zero)
    $confirmDialog = [IntPtr]::Zero
    $confirmDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while ($confirmDialog -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $confirmDeadline) {
        Start-Sleep -Milliseconds 150
        $confirmDialog = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$fileOpsProcess.Id, "#32770")
    }
    if ($confirmDialog -eq [IntPtr]::Zero) {
        throw ("File-operation regression failed: deleting did not ask for confirmation, " +
            "although 删除前提示 is on by default.")
    }
    # IDNO = 7。按钮子窗口发 BM_CLICK 比给对话框发 WM_COMMAND 可靠（实测后者无效）。
    $noButton = [YeImageViewerTestNativeV1365]::GetDlgItem($confirmDialog, 7)
    if ($noButton -eq [IntPtr]::Zero) {
        throw "File-operation regression failed: the confirmation dialog has no No button."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($noButton, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800
    if (-not (Test-Path -LiteralPath $deleteTarget -PathType Leaf)) {
        throw "File-operation regression failed: answering No still deleted the file."
    }

    [void][YeImageViewerTestNativeV1365]::PostMessage($fileOpsWindow, 0x0111, [UIntPtr]1006, [IntPtr]::Zero)
    $confirmDialog = [IntPtr]::Zero
    $confirmDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while ($confirmDialog -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $confirmDeadline) {
        Start-Sleep -Milliseconds 150
        $confirmDialog = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$fileOpsProcess.Id, "#32770")
    }
    if ($confirmDialog -eq [IntPtr]::Zero) {
        throw "File-operation regression failed: the second delete did not ask for confirmation."
    }
    # IDYES = 6
    $yesButton = [YeImageViewerTestNativeV1365]::GetDlgItem($confirmDialog, 6)
    if ($yesButton -eq [IntPtr]::Zero) {
        throw "File-operation regression failed: the confirmation dialog has no Yes button."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($yesButton, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero)

    $goneDeadline = [DateTime]::UtcNow.AddSeconds(10)
    while ((Test-Path -LiteralPath $deleteTarget -PathType Leaf) -and
        [DateTime]::UtcNow -lt $goneDeadline) {
        Start-Sleep -Milliseconds 200
    }
    if (Test-Path -LiteralPath $deleteTarget -PathType Leaf) {
        throw "File-operation regression failed: answering Yes did not delete the file."
    }

    # 必须进回收站，不能是直接抹掉——用户按 Ctrl+Z 或者从回收站还原是常见动作
    $recycleBin = (New-Object -ComObject Shell.Application).Namespace(10)
    $restorable = $false
    foreach ($item in $recycleBin.Items()) {
        if ($item.Name -eq "one.png" -or $item.Name -eq "one") {
            $originalFolder = $recycleBin.GetDetailsOf($item, 1)
            if ($originalFolder -and $originalFolder.StartsWith($fileOpsPictures)) {
                $restorable = $true
                break
            }
        }
    }
    if (-not $restorable) {
        throw ("File-operation regression failed: the deleted file is not in the recycle bin, " +
            "so it cannot be restored.")
    }

    $fileOpsProcess.Refresh()
    if ($fileOpsProcess.HasExited) {
        throw "File-operation regression failed: the viewer exited after deleting."
    }
    if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($fileOpsWindow)) {
        throw "File-operation regression failed: the viewer stayed disabled after deleting."
    }

    $fileOpsTitle = New-Object Text.StringBuilder 512
    $listDeadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        [void][YeImageViewerTestNativeV1365]::GetWindowText($fileOpsWindow, $fileOpsTitle, 512)
    } while ($fileOpsTitle.ToString() -notmatch "\[\d+/2\]" -and [DateTime]::UtcNow -lt $listDeadline)
    if ($fileOpsTitle.ToString() -notmatch "\[\d+/2\]") {
        throw ("File-operation regression failed: the deleted image stayed in the list, title is " +
            "'$($fileOpsTitle.ToString())'.")
    }

    Write-Host ("PASS the image copies to the clipboard, its info matches, the overlay toggles, " +
        "and delete asks first then moves the file to the recycle bin.")
}
finally {
    if ($fileOpsProcess -and -not $fileOpsProcess.HasExited) {
        [void]$fileOpsProcess.CloseMainWindow()
        if (-not $fileOpsProcess.WaitForExit(4000)) {
            Stop-Process -Id $fileOpsProcess.Id -Force
            [void]$fileOpsProcess.WaitForExit(3000)
        }
    }
    # 把测试丢进回收站的那张删干净，不留在用户的回收站里
    try {
        $recycleBin = (New-Object -ComObject Shell.Application).Namespace(10)
        foreach ($item in @($recycleBin.Items())) {
            $originalFolder = $recycleBin.GetDetailsOf($item, 1)
            if ($originalFolder -and $originalFolder.StartsWith($fileOpsRoot)) {
                Remove-Item -LiteralPath $item.Path -Force -ErrorAction SilentlyContinue
            }
        }
    }
    catch { }
    if (Test-Path -LiteralPath $fileOpsRoot) {
        Remove-Item -LiteralPath $fileOpsRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Checking frame export for animated images..."
# 导出（Ctrl+S 把动图每一帧存成 PNG）此前只靠手工冒烟。它没有保存对话框，
# 只有一个是/否确认框，所以完全可以自动验：答「否」一个文件都不该出现，
# 答「是」要在原图旁边生成 <名字>_0001.png 起的一串，而且每个都是能解的 PNG。
$exportRoot = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Export-" + [Guid]::NewGuid().ToString("N"))
$exportProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $exportRoot)
    $exportViewer = Join-Path $exportRoot "YeImageViewer.exe"
    Copy-Item -LiteralPath $viewer -Destination $exportViewer

    $exportPictures = Join-Path $exportRoot "pics"
    [void](New-Item -ItemType Directory -Path $exportPictures)
    $animatedSource = Join-Path $formatCorpusRoot "files\animated.gif"
    if (-not (Test-Path -LiteralPath $animatedSource -PathType Leaf)) {
        throw "Export regression failed: the animated fixture is missing: $animatedSource"
    }
    $exportImage = Join-Path $exportPictures "clip.gif"
    Copy-Item -LiteralPath $animatedSource -Destination $exportImage

    $exportProcess = Start-Process -FilePath $exportViewer -ArgumentList ('"' + $exportImage + '"') -PassThru
    $exportDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $exportProcess.Refresh()
    } while (-not $exportProcess.HasExited -and $exportProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $exportDeadline)
    if ($exportProcess.HasExited -or $exportProcess.MainWindowHandle -eq 0) {
        throw "Export regression failed: the viewer did not open the animated fixture."
    }
    Start-Sleep -Milliseconds 1200
    $exportWindow = $exportProcess.MainWindowHandle

    # Ctrl+S。程序自己按 WM_KEYDOWN VK_CONTROL 记状态，所以合成这两条消息就够了。
    function Send-ExportShortcut([IntPtr]$Window) {
        [void][YeImageViewerTestNativeV1365]::PostMessage($Window, 0x0100, [UIntPtr]0x11, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 120
        [void][YeImageViewerTestNativeV1365]::PostMessage($Window, 0x0100, [UIntPtr]0x53, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 120
        [void][YeImageViewerTestNativeV1365]::PostMessage($Window, 0x0101, [UIntPtr]0x53, [IntPtr]::Zero)
        [void][YeImageViewerTestNativeV1365]::PostMessage($Window, 0x0101, [UIntPtr]0x11, [IntPtr]::Zero)
    }

    function Wait-ExportDialog([int]$ProcessId) {
        $dialog = [IntPtr]::Zero
        $deadline = [DateTime]::UtcNow.AddSeconds(8)
        while ($dialog -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $dialog = [YeImageViewerTestNativeV1365]::FindProcessWindow([uint32]$ProcessId, "#32770")
        }
        return $dialog
    }

    # ---- 先答「否」：不该产生任何文件 ----
    Send-ExportShortcut $exportWindow
    $exportDialog = Wait-ExportDialog $exportProcess.Id
    if ($exportDialog -eq [IntPtr]::Zero) {
        throw ("Export regression failed: Ctrl+S on an animated image did not ask before " +
            "writing a file per frame.")
    }
    # IDNO = 7，按钮子窗口发 BM_CLICK
    $exportNo = [YeImageViewerTestNativeV1365]::GetDlgItem($exportDialog, 7)
    if ($exportNo -eq [IntPtr]::Zero) {
        throw "Export regression failed: the export confirmation has no No button."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($exportNo, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 1200
    $strayFiles = @(Get-ChildItem -LiteralPath $exportPictures -Filter "clip_*.png" -File)
    if ($strayFiles.Count -gt 0) {
        throw ("Export regression failed: answering No still wrote " +
            "$($strayFiles.Count) file(s).")
    }

    # ---- 再答「是」：每帧一个 PNG ----
    Send-ExportShortcut $exportWindow
    $exportDialog = Wait-ExportDialog $exportProcess.Id
    if ($exportDialog -eq [IntPtr]::Zero) {
        throw "Export regression failed: the second Ctrl+S did not ask for confirmation."
    }
    # IDYES = 6
    $exportYes = [YeImageViewerTestNativeV1365]::GetDlgItem($exportDialog, 6)
    if ($exportYes -eq [IntPtr]::Zero) {
        throw "Export regression failed: the export confirmation has no Yes button."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($exportYes, 0x00F5, [UIntPtr]::Zero, [IntPtr]::Zero)

    $exportedFiles = @()
    $writeDeadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $writeDeadline) {
        Start-Sleep -Milliseconds 250
        $exportedFiles = @(Get-ChildItem -LiteralPath $exportPictures -Filter "clip_*.png" -File |
            Sort-Object Name)
        if ($exportedFiles.Count -ge 2) { break }
    }
    if ($exportedFiles.Count -lt 2) {
        throw ("Export regression failed: the animated fixture has at least two frames but only " +
            "$($exportedFiles.Count) PNG(s) were written.")
    }
    if ($exportedFiles[0].Name -ne "clip_0001.png") {
        throw ("Export regression failed: frames should be numbered from clip_0001.png, got " +
            "$($exportedFiles[0].Name).")
    }

    # 每个导出的文件都要是真能用的 PNG：查签名，并从 IHDR 里读出尺寸
    foreach ($exported in $exportedFiles) {
        $header = [byte[]]::new(24)
        $stream = [IO.File]::OpenRead($exported.FullName)
        try { [void]$stream.Read($header, 0, 24) } finally { $stream.Dispose() }
        $signature = @(137, 80, 78, 71, 13, 10, 26, 10)
        for ($index = 0; $index -lt 8; $index++) {
            if ($header[$index] -ne $signature[$index]) {
                throw ("Export regression failed: $($exported.Name) is not a PNG file.")
            }
        }
        # IHDR: 宽在偏移 16，高在偏移 20，都是大端 32 位
        $width = ($header[16] -shl 24) -bor ($header[17] -shl 16) -bor ($header[18] -shl 8) -bor $header[19]
        $height = ($header[20] -shl 24) -bor ($header[21] -shl 16) -bor ($header[22] -shl 8) -bor $header[23]
        if ($width -ne 160 -or $height -ne 80) {
            throw ("Export regression failed: $($exported.Name) is ${width}x${height}, " +
                "expected the fixture's 160x80.")
        }
    }

    # 两帧的内容不该一模一样——真的是不同帧，而不是同一帧写了两遍
    if ($exportedFiles.Count -ge 2) {
        $firstHash = (Get-FileHash -LiteralPath $exportedFiles[0].FullName -Algorithm SHA256).Hash
        $secondHash = (Get-FileHash -LiteralPath $exportedFiles[1].FullName -Algorithm SHA256).Hash
        if ($firstHash -eq $secondHash) {
            throw ("Export regression failed: the exported frames are byte-identical, so the " +
                "same frame was written twice.")
        }
    }

    $exportProcess.Refresh()
    if ($exportProcess.HasExited) {
        throw "Export regression failed: the viewer exited while exporting."
    }
    if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($exportWindow)) {
        throw "Export regression failed: the viewer stayed disabled after exporting."
    }

    Write-Host ("PASS frame export asks first, writes one real PNG per frame next to the source " +
        "($($exportedFiles.Count) frames), and leaves the viewer usable.")
}
finally {
    if ($exportProcess -and -not $exportProcess.HasExited) {
        [void]$exportProcess.CloseMainWindow()
        if (-not $exportProcess.WaitForExit(6000)) {
            Stop-Process -Id $exportProcess.Id -Force
            [void]$exportProcess.WaitForExit(3000)
        }
    }
    if (Test-Path -LiteralPath $exportRoot) {
        Remove-Item -LiteralPath $exportRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Checking that the language and theme radios really take effect..."
# 繁體是第三种语言，而「界面是不是中文」一旦写成 UI_LANG == 0，繁體就会掉进英文分支。
# 源码检查只拦得住那一种写法，这里从真窗口验结果：在设置页点语言，再看设置窗口
# 自己的标题是哪一种语言（设置 / Settings / 設定，都来自 stringRes 第 39 条）。
#
# 读「界面现在是什么语言」用窗口标题，不用剪贴板。剪贴板是全机器共享的，
# 程序和测试会抢，实测会时不时读到空——而标题是窗口自带的，取多少次都一样。
# 标题在建窗口时就定了，所以每次改完语言要关掉再开一次才能看到新标题。
$languageRoot = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Lang-" + [Guid]::NewGuid().ToString("N"))
$languageProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $languageRoot)
    $languageViewer = Join-Path $languageRoot "YeImageViewer.exe"
    Copy-Item -LiteralPath $viewer -Destination $languageViewer
    $languagePictures = Join-Path $languageRoot "pics"
    [void](New-Item -ItemType Directory -Path $languagePictures)
    $languageImage = Join-Path $languagePictures "sample.png"
    Copy-Item -LiteralPath $commonPngFixture -Destination $languageImage

    $languageProcess = Start-Process -FilePath $languageViewer -ArgumentList ('"' + $languageImage + '"') -PassThru
    $languageDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        $languageProcess.Refresh()
    } while (-not $languageProcess.HasExited -and $languageProcess.MainWindowHandle -eq 0 -and
        [DateTime]::UtcNow -lt $languageDeadline)
    if ($languageProcess.HasExited -or $languageProcess.MainWindowHandle -eq 0) {
        throw "Language regression failed: the viewer did not open."
    }
    Start-Sleep -Milliseconds 800
    $languageWindow = $languageProcess.MainWindowHandle
    $languagePid = [uint32]$languageProcess.Id

    function Open-LanguageSettings {
        # 右键菜单的「设置」= ContextMenu::openSetting
        [void][YeImageViewerTestNativeV1365]::PostMessage($languageWindow, 0x0111, [UIntPtr]1010, [IntPtr]::Zero)
        $window = [IntPtr]::Zero
        $deadline = [DateTime]::UtcNow.AddSeconds(8)
        while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $window = [YeImageViewerTestNativeV1365]::FindProcessWindow($languagePid, "YeImageViewerSettingWnd")
        }
        if ($window -eq [IntPtr]::Zero) {
            throw "Language regression failed: the Settings window did not open."
        }
        return $window
    }

    function Close-LanguageSettings([IntPtr]$Window) {
        [void][YeImageViewerTestNativeV1365]::SendMessage($Window, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        $deadline = [DateTime]::UtcNow.AddSeconds(6)
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 150
            if ([YeImageViewerTestNativeV1365]::FindProcessWindow($languagePid, "YeImageViewerSettingWnd") -eq
                [IntPtr]::Zero) {
                return
            }
        }
        throw "Language regression failed: the Settings window did not close."
    }

    # 语言单选是「显示」卡片里的第 3 行（下标 2），逻辑坐标按 SettingLayout 推：
    #   GENERAL_CHECK_BOTTOM    = 54 + 2*36 + 32   = 158
    #   GENERAL_BEHAVIOR_CARD.h = 158 + 4 - 20     = 142
    #   GENERAL_DISPLAY_CARD_Y  = 20 + 142 + 16    = 178
    #   GENERAL_RADIO_FIRST_Y   = 178 + 38         = 216
    #   行 y（下标 2）          = 216 + 2*48        = 312
    # 选项区从行左边 +138（labelWidth）起、宽 544-138=406，三个选项各 135 宽
    # （见 Setting.h 的 handleGeneralTab）。窗口坐标还要加 52 的标签页高度。
    $languageOptionY = 52 + 312 + 5 + 14
    $languageOptionX = @(0, 1, 2 | ForEach-Object { 38 + 138 + $_ * 135 + 67 })

    $languageExpectations = @(
        @{ Index = 0; Name = "简体中文"; Title = "设置" },
        @{ Index = 1; Name = "English"; Title = "Settings" },
        @{ Index = 2; Name = "繁體中文"; Title = "設定" }
    )

    $languageSettings = Open-LanguageSettings
    try {
        foreach ($expectation in $languageExpectations) {
            # 逻辑画布固定 620 宽，倍率按实际客户区宽度反推（窗口放不下时程序会压低缩放）
            $settingRect = New-Object YeImageViewerTestNativeV1365+RECT
            [void][YeImageViewerTestNativeV1365]::GetClientRect($languageSettings, [ref]$settingRect)
            $settingWidth = $settingRect.Right - $settingRect.Left
            if ($settingWidth -le 0) { $settingWidth = 620 }

            $clickX = [int][Math]::Round($languageOptionX[$expectation.Index] * $settingWidth / 620.0)
            $clickY = [int][Math]::Round($languageOptionY * $settingWidth / 620.0)
            $position = [IntPtr](($clickY -shl 16) -bor ($clickX -band 0xFFFF))
            [void][YeImageViewerTestNativeV1365]::SendMessage($languageSettings, 0x0201, [UIntPtr]1, $position)
            [void][YeImageViewerTestNativeV1365]::SendMessage($languageSettings, 0x0202, [UIntPtr]0, $position)
            Start-Sleep -Milliseconds 400

            Close-LanguageSettings $languageSettings
            $languageSettings = Open-LanguageSettings

            $settingTitle = New-Object Text.StringBuilder 512
            [void][YeImageViewerTestNativeV1365]::GetWindowText($languageSettings, $settingTitle, 512)
            $actual = $settingTitle.ToString()
            if ($actual -ne $expectation.Title) {
                throw ("Language regression failed: after selecting $($expectation.Name) the " +
                    "Settings window is titled '${actual}', expected '$($expectation.Title)'. " +
                    "A wrong title here means the UI fell back to another language.")
            }
        }
    }
    finally {
        if ($languageSettings -ne [IntPtr]::Zero) {
            [void][YeImageViewerTestNativeV1365]::SendMessage($languageSettings, 0x0010,
                [UIntPtr]::Zero, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 400
        }
    }

    # ---- 主题：跟随系统 / 浅色 / 深色 ----
    # 主题是「界面主题」里唯一能在窗口外部验的部分：切过去之后程序会给主窗口设
    # DWMWA_USE_IMMERSIVE_DARK_MODE(20)，读回来就知道到底切没切。
    # 行是「显示」卡片的第 2 行（下标 1）：y = 216 + 1*48 = 264，三个选项，x 同语言那一行。
    $themeOptionY = 52 + 264 + 5 + 14
    $systemUsesLightTheme = 1
    try {
        $systemUsesLightTheme = (Get-ItemProperty -Path `
            "HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize" `
            -Name "AppsUseLightTheme" -ErrorAction Stop).AppsUseLightTheme
    }
    catch {
        # 这个值不存在时 Windows 按浅色处理
        $systemUsesLightTheme = 1
    }

    $themeExpectations = @(
        @{ Index = 1; Name = "浅色"; Dark = 0 },
        @{ Index = 2; Name = "深色"; Dark = 1 },
        @{ Index = 0; Name = "跟随系统"; Dark = $(if ($systemUsesLightTheme -eq 0) { 1 } else { 0 }) }
    )

    $themeSettings = Open-LanguageSettings
    try {
        foreach ($expectation in $themeExpectations) {
            $settingRect = New-Object YeImageViewerTestNativeV1365+RECT
            [void][YeImageViewerTestNativeV1365]::GetClientRect($themeSettings, [ref]$settingRect)
            $settingWidth = $settingRect.Right - $settingRect.Left
            if ($settingWidth -le 0) { $settingWidth = 620 }

            $clickX = [int][Math]::Round($languageOptionX[$expectation.Index] * $settingWidth / 620.0)
            $clickY = [int][Math]::Round($themeOptionY * $settingWidth / 620.0)
            $position = [IntPtr](($clickY -shl 16) -bor ($clickX -band 0xFFFF))
            [void][YeImageViewerTestNativeV1365]::SendMessage($themeSettings, 0x0201, [UIntPtr]1, $position)
            [void][YeImageViewerTestNativeV1365]::SendMessage($themeSettings, 0x0202, [UIntPtr]0, $position)

            # 主题是异步落到主窗口上的（isNeedUpdateTheme 由绘制循环消费），轮询等它生效
            $themeDeadline = [DateTime]::UtcNow.AddSeconds(6)
            $actualDark = -1
            do {
                Start-Sleep -Milliseconds 200
                $actualDark = [YeImageViewerTestNativeV1365]::DarkModeFlag($languageWindow)
                $normalized = if ($actualDark -gt 0) { 1 } else { $actualDark }
            } while ($normalized -ne $expectation.Dark -and [DateTime]::UtcNow -lt $themeDeadline)

            if ($actualDark -lt 0) {
                throw ("Theme regression failed: the window does not report its dark-mode state, " +
                    "so selecting $($expectation.Name) cannot be verified.")
            }
            $normalized = if ($actualDark -gt 0) { 1 } else { 0 }
            if ($normalized -ne $expectation.Dark) {
                throw ("Theme regression failed: after selecting $($expectation.Name) the window's " +
                    "dark-mode flag is ${normalized}, expected $($expectation.Dark).")
            }
        }
    }
    finally {
        if ($themeSettings -ne [IntPtr]::Zero) {
            [void][YeImageViewerTestNativeV1365]::SendMessage($themeSettings, 0x0010,
                [UIntPtr]::Zero, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 400
        }
    }

    $languageProcess.Refresh()
    if ($languageProcess.HasExited) {
        throw "Language regression failed: the viewer exited while switching languages."
    }

    Write-Host ("PASS the language radio switches between 简体中文, English and 繁體中文, the UI " +
        "text follows all three, and the theme radio really switches the window between " +
        "light and dark.")
}
finally {
    if ($languageProcess -and -not $languageProcess.HasExited) {
        [void]$languageProcess.CloseMainWindow()
        if (-not $languageProcess.WaitForExit(5000)) {
            Stop-Process -Id $languageProcess.Id -Force
            [void]$languageProcess.WaitForExit(3000)
        }
    }
    if (Test-Path -LiteralPath $languageRoot) {
        Remove-Item -LiteralPath $languageRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Checking the print preview on extreme aspect ratios..."
# 打印预览的缩放以前只算缩放系数，不管算出来的边长：一张 10000x1 的图缩到 800 宽，
# 高就成了 round(1 * 0.08) = 0，预览窗口压根建不起来——点「打印」什么都不发生，
# 也没有任何提示。单元测试盯住了算法（PrintLayout），这里盯住真窗口真的开得起来。
#
# 顺带把超过 16384 的那条路也盯上：那条路会先弹一个「尺寸过大，已缩小到 ...」的提示框，
# 提示框会挡住预览线程，关掉之后预览才出来。少了这一步测试会误判成「预览没开」。
$printFixtureDir = Join-Path $repoRoot "test\corpus\13-dimensions"
$printFixtures = @(
    @{ Name = "101x99.png"; ExpectsOversizeWarning = $false },
    @{ Name = "1x1.png"; ExpectsOversizeWarning = $false },
    @{ Name = "10000x1.png"; ExpectsOversizeWarning = $false },
    @{ Name = "1x10000.png"; ExpectsOversizeWarning = $false },
    @{ Name = "19200x200.png"; ExpectsOversizeWarning = $true }
)

function Dismiss-ProcessDialog([int]$ProcessId) {
    $dialog = [YeImageViewerTestNativeV1365]::FindProcessWindow([uint32]$ProcessId, "#32770")
    if ($dialog -eq [IntPtr]::Zero) {
        return $false
    }
    # WM_CLOSE。给 MessageBox 发 WM_COMMAND/IDOK 关不掉（实测提示框还在原地），
    # 只有一个确定按钮的提示框收到 WM_CLOSE 就当按了确定。
    [void][YeImageViewerTestNativeV1365]::PostMessage($dialog, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    return $true
}

foreach ($printFixture in $printFixtures) {
    $printFixtureName = $printFixture.Name
    $printFixturePath = Join-Path $printFixtureDir $printFixtureName
    if (-not (Test-Path -LiteralPath $printFixturePath -PathType Leaf)) {
        throw "Print regression failed: fixture is missing: $printFixturePath"
    }

    $printProcess = $null
    try {
        $printProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $printFixturePath + '"') -PassThru
        $printDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 200
            $printProcess.Refresh()
        } while (-not $printProcess.HasExited -and $printProcess.MainWindowHandle -eq 0 -and
            [DateTime]::UtcNow -lt $printDeadline)

        if ($printProcess.HasExited -or $printProcess.MainWindowHandle -eq 0) {
            throw "Print regression failed: the viewer did not open $printFixtureName."
        }

        # 右键菜单的「打印」= ContextMenu::printImage。预览窗口建在另一个线程上，
        # 用 PostMessage 把命令丢进去就不管了，下面轮询等窗口出现。
        [void][YeImageViewerTestNativeV1365]::PostMessage($printProcess.MainWindowHandle,
            0x0111, [UIntPtr]1008, [IntPtr]::Zero)

        $printerWindow = [IntPtr]::Zero
        $sawOversizeWarning = $false
        $printerDeadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 200
            $printProcess.Refresh()
            if ($printProcess.HasExited) { break }
            if (Dismiss-ProcessDialog $printProcess.Id) {
                $sawOversizeWarning = $true
                continue
            }
            $printerWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
                [uint32]$printProcess.Id, "YeImageViewerPrinterWnd")
        } while ($printerWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $printerDeadline)

        if ($printProcess.HasExited) {
            $printExitCode = [BitConverter]::ToUInt32(
                [BitConverter]::GetBytes([int]$printProcess.ExitCode), 0)
            throw ("Print regression failed: printing ${printFixtureName} killed the viewer " +
                "(0x$('{0:X8}' -f $printExitCode)).")
        }
        if ($printerWindow -eq [IntPtr]::Zero) {
            throw ("Print regression failed: no print preview opened for ${printFixtureName}. " +
                "An extreme aspect ratio must still produce a preview at least one pixel thick, " +
                "not a silent no-op.")
        }
        if ($printFixture.ExpectsOversizeWarning -and -not $sawOversizeWarning) {
            throw ("Print regression failed: ${printFixtureName} is wider than the 16384 limit, " +
                "so printing it should warn that the image was scaled down.")
        }
        if (-not $printFixture.ExpectsOversizeWarning -and $sawOversizeWarning) {
            throw ("Print regression failed: ${printFixtureName} is within the 16384 limit, " +
                "so printing it must not warn about resizing.")
        }

        # 预览窗口要真的在跑消息泵，而不只是有个句柄
        $printerResult = [UIntPtr]::Zero
        $printerAnswered = [YeImageViewerTestNativeV1365]::SendMessageTimeout($printerWindow,
            0x0000, [UIntPtr]::Zero, [IntPtr]::Zero, 0x0002, 5000, [ref]$printerResult)
        if ($printerAnswered -eq [IntPtr]::Zero) {
            throw ("Print regression failed: the print preview for ${printFixtureName} does not " +
                "answer messages.")
        }

        # Esc 关预览，主窗口要恢复可用
        [void][YeImageViewerTestNativeV1365]::PostMessage($printerWindow, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
        $closeDeadline = [DateTime]::UtcNow.AddSeconds(8)
        do {
            Start-Sleep -Milliseconds 200
            $printProcess.Refresh()
            $printerWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
                [uint32]$printProcess.Id, "YeImageViewerPrinterWnd")
        } while ($printerWindow -ne [IntPtr]::Zero -and -not $printProcess.HasExited -and
            [DateTime]::UtcNow -lt $closeDeadline)

        if ($printerWindow -ne [IntPtr]::Zero) {
            throw "Print regression failed: Escape did not close the print preview for ${printFixtureName}."
        }
        if ($printProcess.HasExited) {
            throw ("Print regression failed: closing the print preview for ${printFixtureName} " +
                "also closed the viewer.")
        }
        if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($printProcess.MainWindowHandle)) {
            throw ("Print regression failed: the viewer stayed disabled after the print preview " +
                "for ${printFixtureName} closed.")
        }
    }
    finally {
        if ($printProcess -and -not $printProcess.HasExited) {
            # 可能还有提示框挡着，先关掉它主窗口才收得到 WM_CLOSE
            [void](Dismiss-ProcessDialog $printProcess.Id)
            [void]$printProcess.CloseMainWindow()
            if (-not $printProcess.WaitForExit(3000)) {
                Stop-Process -Id $printProcess.Id -Force
                $printProcess.WaitForExit()
            }
        }
    }
}
Write-Host ("PASS print preview opens, answers messages, and closes cleanly for all " +
    "$($printFixtures.Count) aspect ratios including 10000x1, 1x10000 and an over-16K panorama.")

Write-Host "Checking that every common operation survives extreme aspect ratios..."
# 本轮的两个真缺陷（打印预览开不出来、ras/sr 没缩略图）都是「拿极端输入把功能挨个过
# 一遍」找出来的，不是读代码读出来的。这一环把那种扫法固定下来：六种极端尺寸的图，
# 每张都把常用操作走一遍，只要有一下崩了或者不回消息就记失败。
#
# 刻意不验「做得对不对」——那是各功能自己的测试负责的。这里只验「不会把程序搞死」，
# 判据简单才跑得快、才不会误报。
$extremeOpsFixtures = @("10000x1.png", "1x10000.png", "1x1.png", "1x2.png",
    "19200x200.png", "200x19200.png")
# 消息、参数、名字。滚轮的 wParam 高 16 位是滚动量，0x0078 = 120 一格，
# 0xFF88 = -120 反向；低位 0x0008 是 MK_CONTROL，按住 Ctrl 才是缩放。
$extremeOpsSteps = @(
    @{ Name = "rotate left (Q)"; Message = 0x0100; WParam = 0x51 },
    @{ Name = "rotate right (E)"; Message = 0x0100; WParam = 0x45 },
    @{ Name = "rotate right again"; Message = 0x0100; WParam = 0x45 },
    @{ Name = "toggle the info overlay (I)"; Message = 0x0100; WParam = 0x49 },
    @{ Name = "toggle the info overlay back"; Message = 0x0100; WParam = 0x49 },
    @{ Name = "zoom in (Up)"; Message = 0x0100; WParam = 0x26 },
    @{ Name = "zoom in again"; Message = 0x0100; WParam = 0x26 },
    @{ Name = "zoom out (Down)"; Message = 0x0100; WParam = 0x28 },
    @{ Name = "fit (5)"; Message = 0x0100; WParam = 0x35 },
    @{ Name = "Ctrl+wheel zoom in"; Message = 0x020A; WParam = 0x780008u },
    @{ Name = "Ctrl+wheel zoom out"; Message = 0x020A; WParam = 0xFF880008u },
    @{ Name = "pan with the wheel"; Message = 0x020A; WParam = 0x780000u },
    @{ Name = "next image (Right)"; Message = 0x0100; WParam = 0x27 },
    @{ Name = "previous image (Left)"; Message = 0x0100; WParam = 0x25 },
    @{ Name = "last image (End)"; Message = 0x0100; WParam = 0x23 },
    @{ Name = "first image (Home)"; Message = 0x0100; WParam = 0x24 },
    @{ Name = "copy the image (Ctrl+C)"; Message = 0x0111; WParam = 1003 },
    @{ Name = "copy the image info (C)"; Message = 0x0111; WParam = 1001 },
    @{ Name = "toggle fullscreen (F)"; Message = 0x0100; WParam = 0x46 },
    @{ Name = "leave fullscreen (F)"; Message = 0x0100; WParam = 0x46 },
    @{ Name = "enter immersive view"; Message = 0x0111; WParam = 1009 },
    @{ Name = "leave immersive view"; Message = 0x0111; WParam = 1009 }
)

# 在临时目录里跑，不要直接用仓库里的 exe 和素材：旋转角度和每图缩放是持久化的
# （存在 exe 旁边的 YeImageViewer.db 里），直接跑的话每次的起始状态都不一样——
# 实测连跑六轮，起始缩放分别是 17%/46%/9%，旋转分别是顺时针90°/180°/逆时针90°/无。
# 结果不可重现的测试没法用来判断「是不是真的坏了」，而且它还会把旋转记录写进
# 开发目录的配置，影响别的环节。
$extremeOpsRoot = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-ExtremeOps-" + [Guid]::NewGuid().ToString("N"))
try {
[void](New-Item -ItemType Directory -Path $extremeOpsRoot)
$extremeOpsViewer = Join-Path $extremeOpsRoot "YeImageViewer.exe"
Copy-Item -LiteralPath $viewer -Destination $extremeOpsViewer
$extremeOpsPictures = Join-Path $extremeOpsRoot "pics"
[void](New-Item -ItemType Directory -Path $extremeOpsPictures)
foreach ($extremeOpsName in $extremeOpsFixtures) {
    $extremeOpsSource = Join-Path $repoRoot "test\corpus\13-dimensions\$extremeOpsName"
    if (-not (Test-Path -LiteralPath $extremeOpsSource -PathType Leaf)) {
        throw "Extreme-operation regression failed: fixture is missing: $extremeOpsSource"
    }
    Copy-Item -LiteralPath $extremeOpsSource -Destination (Join-Path $extremeOpsPictures $extremeOpsName)
}

foreach ($extremeOpsName in $extremeOpsFixtures) {
    $extremeOpsImage = Join-Path $extremeOpsPictures $extremeOpsName

    $extremeOpsProcess = $null
    try {
        $extremeOpsProcess = Start-Process -FilePath $extremeOpsViewer `
            -ArgumentList ('"' + $extremeOpsImage + '"') -PassThru
        $extremeOpsDeadline = [DateTime]::UtcNow.AddSeconds(12)
        do {
            Start-Sleep -Milliseconds 200
            $extremeOpsProcess.Refresh()
        } while (-not $extremeOpsProcess.HasExited -and
            $extremeOpsProcess.MainWindowHandle -eq 0 -and
            [DateTime]::UtcNow -lt $extremeOpsDeadline)

        if ($extremeOpsProcess.HasExited -or $extremeOpsProcess.MainWindowHandle -eq 0) {
            throw "Extreme-operation regression failed: the viewer did not open $extremeOpsName."
        }
        Start-Sleep -Milliseconds 700
        $extremeOpsWindow = $extremeOpsProcess.MainWindowHandle

        foreach ($extremeOpsStep in $extremeOpsSteps) {
            [void][YeImageViewerTestNativeV1365]::PostMessage($extremeOpsWindow,
                [uint32]$extremeOpsStep.Message, [UIntPtr][uint64]$extremeOpsStep.WParam,
                [IntPtr]::Zero)
            Start-Sleep -Milliseconds 350
            $extremeOpsProcess.Refresh()

            if ($extremeOpsProcess.HasExited) {
                $extremeOpsCode = [BitConverter]::ToUInt32(
                    [BitConverter]::GetBytes([int]$extremeOpsProcess.ExitCode), 0)
                throw ("Extreme-operation regression failed: ${extremeOpsName} exited with " +
                    "0x$('{0:X8}' -f $extremeOpsCode) right after '$($extremeOpsStep.Name)'.")
            }

            $extremeOpsAnswer = [UIntPtr]::Zero
            if ([YeImageViewerTestNativeV1365]::SendMessageTimeout($extremeOpsWindow, 0x0000,
                    [UIntPtr]::Zero, [IntPtr]::Zero, 0x0002, 5000, [ref]$extremeOpsAnswer) -eq
                    [IntPtr]::Zero) {
                throw ("Extreme-operation regression failed: ${extremeOpsName} stopped answering " +
                    "messages after '$($extremeOpsStep.Name)'.")
            }
        }

        # 走完一整轮还得是能用的窗口，不能只是「进程还活着」
        if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($extremeOpsWindow)) {
            throw ("Extreme-operation regression failed: ${extremeOpsName} left the window " +
                "disabled after the whole round.")
        }
    }
    finally {
        if ($extremeOpsProcess -and -not $extremeOpsProcess.HasExited) {
            [void]$extremeOpsProcess.CloseMainWindow()
            if (-not $extremeOpsProcess.WaitForExit(4000)) {
                Stop-Process -Id $extremeOpsProcess.Id -Force
                [void]$extremeOpsProcess.WaitForExit(2000)
            }
        }
    }
}
}
finally {
    if (Test-Path -LiteralPath $extremeOpsRoot) {
        Remove-Item -LiteralPath $extremeOpsRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Write-Host ("PASS all $($extremeOpsSteps.Count) common operations survive on " +
    "$($extremeOpsFixtures.Count) extreme aspect ratios, from 1x1 to 200x19200.")

Write-Host "All regression tests passed."
