param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "artifacts\release"),
    [switch]$SkipBuild,
    # 候选／开发包留在指定输出目录，验收后再显式归档；正式发布默认仍双归档。
    [switch]$NoArchive
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$releaseDirectory = Join-Path $repoRoot "x64\Release"
$viewer = Join-Path $releaseDirectory "YeImageViewer.exe"
$thumbnailProvider = Join-Path $releaseDirectory "YeThumbnailProvider.dll"

if (-not $SkipBuild) {
    $shell = Join-Path $PSHOME "pwsh.exe"
    if (-not (Test-Path -LiteralPath $shell)) {
        throw "PowerShell 7 is required to build the Release package."
    }
    & $shell -NoProfile -File (Join-Path $repoRoot "buildRelease.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed with exit code $LASTEXITCODE."
    }
}

foreach ($requiredFile in @($viewer, $thumbnailProvider,(Join-Path $releaseDirectory 'YeLayerBackend.dll'),(Join-Path $releaseDirectory 'LAYER_BACKEND_NOTICES.txt'))) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Release runtime file is missing: $requiredFile"
    }
}

$fileVersion = (Get-Item -LiteralPath $viewer).VersionInfo.FileVersion
$versionParts = @($fileVersion -split "\.")
if ($versionParts.Count -lt 3) {
    throw "Unexpected viewer file version: $fileVersion"
}
$version = "v$($versionParts[0]).$($versionParts[1]).$($versionParts[2])"
# 预发布版（如 1.37.2-rc1）的后缀写在 ProductVersion 字符串里——FILEVERSION 必须是四段数字，
# 放不下后缀。有后缀就用它命名，免得候选版的安装包和正式版同名。
$productVersion = (Get-Item -LiteralPath $viewer).VersionInfo.ProductVersion
if ($productVersion -match '^\d+\.\d+\.\d+-[0-9A-Za-z.]+$') {
    $version = "v$productVersion"
}
$packageName = "YeImageViewer-$version-win-x64-portable"

$sevenZipCandidates = @(
    (Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
)
$sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($sevenZipCommand) {
    $sevenZip = $sevenZipCommand.Source
}
else {
    $sevenZip = $sevenZipCandidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1
}
if (-not $sevenZip) {
    throw "7-Zip is required to create the compact full package."
}

$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryRoot = Join-Path $temporaryBase ("YeImageViewer-Package-" + [Guid]::NewGuid().ToString("N"))
$stagingRoot = Join-Path $temporaryRoot $packageName

try {
    # 绿色版：exe 就在包的根目录，解压出来双击即用，不再埋在 x64\Release\ 下面
    New-Item -ItemType Directory -Path $stagingRoot -Force | Out-Null
    Copy-Item -LiteralPath $viewer -Destination (Join-Path $stagingRoot "YeImageViewer.exe")
    Copy-Item -LiteralPath $thumbnailProvider -Destination (Join-Path $stagingRoot "YeThumbnailProvider.dll")
    foreach($file in @('YeLayerBackend.dll','LAYER_BACKEND_NOTICES.txt')){Copy-Item -LiteralPath (Join-Path $releaseDirectory $file) -Destination (Join-Path $stagingRoot $file)}
    foreach ($document in @(
        "CHANGELOG.md",
        "installLocal.ps1",
        "LICENSE",
        "README.md",
        "README_EN.md",
        "THIRD_PARTY_NOTICES.md",
        "UPSTREAM.md"
    )) {
        Copy-Item -LiteralPath (Join-Path $repoRoot $document) -Destination (Join-Path $stagingRoot $document)
    }
    # 当前阶段的真实能力与使用限制随包提供，不把未完成的颜色助手页面当已交付。
    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/color-picker/QuickStart.md') -Destination (Join-Path $stagingRoot '颜色取色-使用说明.md')
    foreach($launcher in @('屏幕快速取色.cmd','颜色面板.cmd')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot ('tools/'+$launcher)) -Destination (Join-Path $stagingRoot $launcher)
    }

    # 包里放一张说明：绿色版到底怎么用、哪个文件是可选的
    $portableNotice = @"
YeImageViewer 绿色版
====================

双击 YeImageViewer.exe 就能用，不需要安装。
无需打开看图主窗口时，可双击“屏幕快速取色.cmd”或“颜色面板.cmd”。
两个脚本应与 EXE 放在同一目录；关闭工具即退出，默认不常驻后台。

文件说明
--------
YeImageViewer.exe        程序本体。只要这一个文件就能看图，拷到 U 盘、
                         放到任意目录都行，不写注册表、不留后台服务。
YeLayerBackend.dll      图层与通道的 PhotoCraft 合成后端。高级图层浏览需要
                         此文件放在 EXE 旁边；普通图片首帧不加载它。
LAYER_BACKEND_NOTICES.txt 后端及依赖的完整许可，分发 DLL 时必须一起保留。
YeThumbnailProvider.dll  可选。放在 exe 旁边并运行一次下面的关联步骤之后，
                         资源管理器才会给 RAW、HEIC、AVIF、PSD 这些
                         Windows 自己不认的格式显示缩略图。
                         不要这个功能的话，删掉它不影响看图。
installLocal.ps1         可选。想要开始菜单/桌面快捷方式、或者想注册缩略图，
                         用 PowerShell 跑它。不跑也完全不影响使用。

配置文件
--------
通用设置保存在程序旁边的 YeImageViewer.db（固定头4 KB，后接文本配置）：没有就生成，
有就直接用。快速取色的格式、格数、倍率等偏好另存 YeImageViewer.color-picker.ini。
删掉某个配置只恢复该部分默认，不会删除图片；程序照常跑。

发给别人、或者换台机器：普通看图可只拷 YeImageViewer.exe；需要新版图层/通道功能时
请解压完整绿色包，保留 YeLayerBackend.dll 及 LAYER_BACKEND_NOTICES.txt。
想把设置（界面语言、主题、快捷键、关联过的格式、每张图记住的旋转角度）一起
带走，就把 YeImageViewer.db 也拷过去，放在 exe 旁边即可。
还要保留快速取色偏好时，一并复制 YeImageViewer.color-picker.ini；新旧候选建议独立目录。

未使用文件关联或 installLocal.ps1 时：删除这个文件夹即可。
使用过「立即关联」时，先在设置 → 文件关联里点「全不选」再「立即关联」，
撤销本程序的格式登记；若修改过系统默认应用，还需在 Windows 设置中改回。
installLocal.ps1 创建的快捷方式及缩略图注册需按 README 的移除说明单独撤销。

把本程序设成图片的默认打开方式
------------------------------
程序里：右键菜单 → 设置 → 文件关联 → 选格式 → 立即关联。
此操作登记打开方式，Windows 10/11 的受保护默认应用仍需用户在系统设置中确认。
结果对话框可打开系统「默认应用」；本程序不覆盖其他程序的用户选择。
换位置之后需要重新关联一次（绿色软件记的是当前路径）。
"@
    [IO.File]::WriteAllText((Join-Path $stagingRoot "使用说明.txt"),
        ($portableNotice -replace "`r?`n", "`r`n"), [Text.UTF8Encoding]::new($true))

    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $archive = Join-Path $OutputDirectory "$packageName.7z"
    if (Test-Path -LiteralPath $archive) {
        [IO.File]::Delete([IO.Path]::GetFullPath($archive))
    }

    Push-Location $stagingRoot
    try {
        & $sevenZip a -t7z -mx=9 -m0=lzma2 -md=64m -ms=on -mmt=on $archive '.\*' | Out-Null
    } finally {Pop-Location}
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "7-Zip package creation failed with exit code $LASTEXITCODE."
    }

    $maximumDownloadBytes = 25MB
    $archiveInfo = Get-Item -LiteralPath $archive
    if ($archiveInfo.Length -gt $maximumDownloadBytes) {
        throw "Full package is $([math]::Round($archiveInfo.Length / 1MB, 2)) MiB, above the 25 MiB release limit."
    }

    # 单独再放一份裸 exe：只要看图的话，下载这一个文件就够了。
    # 原先这里还会做一个 7z SFX 一键安装器，已经去掉——它每次运行都会触发
    # Windows「程序兼容性助手」的「可能未正确安装此程序」弹窗（SFX 的版本信息
    # 写着 "7z Setup SFX small"，PCA 把它当安装程序，而它退出时又不写卸载项）。
    # 本程序是绿色单文件，装不装都一样，没必要为此留一个会吓人的弹窗。
    $standaloneExe = Join-Path $OutputDirectory "YeImageViewer.exe"
    Copy-Item -LiteralPath $viewer -Destination $standaloneExe -Force

    # zip 也出一份，而且是默认出：Windows 自带就能解压，不用先装 7-Zip。
    # 体积比 7z 大（14.5 对 8 MiB），但都远在 25 MiB 的下载上限之内。
    $zip = Join-Path $OutputDirectory "$packageName.zip"
    if (Test-Path -LiteralPath $zip) {
        [IO.File]::Delete([IO.Path]::GetFullPath($zip))
    }
    Compress-Archive -Path (Join-Path $stagingRoot '*') -DestinationPath $zip -CompressionLevel Optimal
    $zipInfo = Get-Item -LiteralPath $zip
    if ($zipInfo.Length -gt $maximumDownloadBytes) {
        throw "Portable zip is $([math]::Round($zipInfo.Length / 1MB, 2)) MiB, above the 25 MiB release limit."
    }

    $outputs = @($standaloneExe, $zip, $archive)

    $checksums = foreach ($output in $outputs) {
        $hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
        "$($hash.Hash)  $([IO.Path]::GetFileName($output))"
    }
    $checksumPath = Join-Path $OutputDirectory "SHA256SUMS.txt"
    [IO.File]::WriteAllLines($checksumPath, $checksums, [Text.UTF8Encoding]::new($false))
    if (-not $NoArchive) {
        & (Join-Path $repoRoot 'scripts/archive-release.ps1') -ReleaseDirectory $OutputDirectory
    }

    [PSCustomObject]@{
        Version = $version
        StandaloneExe = $standaloneExe
        StandaloneExeMiB = [math]::Round((Get-Item -LiteralPath $standaloneExe).Length / 1MB, 2)
        PortableZip = $zip
        PortableZipMiB = [math]::Round($zipInfo.Length / 1MB, 2)
        Portable7z = $archive
        Portable7zMiB = [math]::Round($archiveInfo.Length / 1MB, 2)
        Checksums = $checksumPath
    }
}
finally {
    $resolvedTemporaryRoot = [IO.Path]::GetFullPath($temporaryRoot)
    if ($resolvedTemporaryRoot.StartsWith($temporaryBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemporaryRoot).StartsWith("YeImageViewer-Package-", [StringComparison]::Ordinal)) {
        if (Test-Path -LiteralPath $resolvedTemporaryRoot) {
            [IO.Directory]::Delete($resolvedTemporaryRoot, $true)
        }
    }
}
