<#
.SYNOPSIS
    重建 FFmpeg 静态库，只保留实况照片用得到的解码路径。

.DESCRIPTION
    仓库里原来那份 FFmpeg 是「全功能」构建：几百个编解码器、所有封装格式、网络协议，
    还外挂了一大串第三方库（SDL2、OpenCL、libmfx、x264/x265、libaom、vpx、theora、
    openssl、mpg123、libass……）。本程序只用它做一件事——把实况照片 / 动态照片里那段
    视频解出画面和声音，用到的 API 只有 avformat_open_input、avcodec_send_packet、
    sws_scale、swr_convert 这一小撮。

    砍掉的东西：
      avdevice / avfilter / postproc —— 一个符号都没引用。
      全部编码器、网络协议、字幕      —— 看图软件不编码、不联网、不放字幕。
      --disable-autodetect           —— 不再链接任何外部库。这一条顺带解决了
                                        aom.lib 去不掉的问题：此前 avcodec 里的
                                        libaomenc.o / libaomdec.o 一直在引用它。

    保留的解码路径（覆盖 iPhone、安卓、谷歌动态照片三条来源）：
      视频 h264 / hevc / mpeg4 / vp8 / vp9 / av1 / mjpeg
      声音 aac / mp3 / opus / vorbis / flac / alac / ac3 / pcm
      封装 mov（mp4、m4a、3gp）/ matroska（webm）/ mp3 / wav / ogg / flac / avi

    构建需要 MSYS2（FFmpeg 的 configure 是 shell 脚本）和 nasm，编译本身仍走 MSVC，
    产出的 .lib 与主程序的 /MT 一致。

.EXAMPLE
    .\build-ffmpeg-slim.ps1                 # 下载源码并构建，产物留在工作目录
    .\build-ffmpeg-slim.ps1 -Install        # 构建完成后替换仓库里的库（原件自动备份）
#>
param(
    [string]$WorkDir = (Join-Path $env:TEMP "yeimageviewer-slim-ffmpeg"),
    [string]$Msys2Root = "C:\msys64",
    [switch]$Install
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$libRoot = Join-Path $repoRoot "YeImageViewer"
$version = "7.1.1"     # 与仓库里的头文件一致：avcodec 61.19.101
$archiveUrl = "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz"
$archiveSha256 = "733984395E0DBBE5C046ABDA2DC49A5544E7E0E1E2366BBA849222AE9E3A03B1"

$bash = Join-Path $Msys2Root "usr\bin\bash.exe"
if (-not (Test-Path -LiteralPath $bash)) {
    throw "找不到 $bash。先装 MSYS2：winget install --id MSYS2.MSYS2，再在里面装 make nasm diffutils。"
}

$srcDir = Join-Path $WorkDir "src"
New-Item -ItemType Directory -Force -Path $srcDir | Out-Null

$source = Join-Path $srcDir "ffmpeg-$version"
if (-not (Test-Path -LiteralPath $source)) {
    $archive = Join-Path $srcDir "ffmpeg-$version.tar.xz"
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "下载 FFmpeg $version ..."
        Invoke-WebRequest -Uri $archiveUrl -OutFile $archive -UseBasicParsing
    }
    $actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actualSha256 -ne $archiveSha256) {
        throw "FFmpeg 源码包校验失败：期望 $archiveSha256，实际 $actualSha256"
    }
    Write-Host "解压 ..."
    tar -xf $archive -C $srcDir
    if (-not (Test-Path -LiteralPath $source)) { throw "解压后没有找到 $source" }
}

# MSVC 的编译环境要带进 MSYS2 的 bash，configure 才找得到 cl.exe 和 INCLUDE / LIB。
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "找不到 vswhere.exe，请先安装 Visual Studio 或 Build Tools" }
$vsPath = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "call `"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}
# bash 不带 -l，直接继承这里的环境；再把 MSYS2 自己的工具目录接在最前面。
$env:MSYS2_ARG_CONV_EXCL = "*"
$env:CHERE_INVOKING = "1"

$decoders = @(
    "h264", "hevc", "mpeg4", "vp8", "vp9", "av1", "mjpeg",
    # eac3 必须跟 ac3 一起开：两者共用 ac3dec_float.o，只开 ac3 会留下
    # ff_eac3_* 一串未解析符号（FFmpeg 7.1.1 里这几张表没有按 CONFIG_EAC3_DECODER 隔开）。
    "aac", "aac_latm", "mp3", "ac3", "eac3", "alac", "flac", "opus", "vorbis",
    "pcm_s16le", "pcm_s16be", "pcm_u8", "pcm_f32le"
) -join ","
$demuxers = @("mov", "matroska", "mp3", "aac", "wav", "ogg", "flac", "avi") -join ","
$parsers = @(
    "h264", "hevc", "mpeg4video", "vp8", "vp9", "av1", "mjpeg",
    "aac", "aac_latm", "mpegaudio", "ac3", "flac", "opus", "vorbis"
) -join ","
$bsfs = @("extract_extradata", "h264_mp4toannexb", "hevc_mp4toannexb",
    "aac_adtstoasc", "vp9_superframe") -join ","

$configure = @(
    "--toolchain=msvc", "--arch=x86_64", "--target-os=win64",
    "--enable-static", "--disable-shared",
    "--disable-programs", "--disable-doc", "--disable-autodetect",
    "--disable-avdevice", "--disable-avfilter", "--disable-postproc",
    "--disable-network", "--disable-everything",
    "--enable-swscale", "--enable-swresample",
    "--enable-decoder=$decoders",
    "--enable-demuxer=$demuxers",
    "--enable-parser=$parsers",
    "--enable-bsf=$bsfs",
    "--enable-protocol=file"
) -join " "

$sourceUnix = "/" + ($source -replace "\\", "/" -replace "^([A-Za-z]):", '$1').ToLower()
$script = @"
set -e
export PATH=/usr/bin:`$PATH
cd "$sourceUnix"
if [ ! -f ffbuild/config.mak ]; then
  ./configure $configure
fi
make -j `$(nproc)
"@
$scriptFile = Join-Path $WorkDir "build.sh"
[IO.File]::WriteAllText($scriptFile, $script.Replace("`r`n", "`n"), (New-Object Text.UTF8Encoding $false))
$scriptUnix = "/" + ($scriptFile -replace "\\", "/" -replace "^([A-Za-z]):", '$1').ToLower()

Write-Host "=== 配置并编译 FFmpeg（十几分钟）===" -ForegroundColor Cyan
& $bash $scriptUnix
if ($LASTEXITCODE -ne 0) { throw "FFmpeg 构建失败" }

# FFmpeg 在 msvc 工具链下仍按 Unix 习惯把静态库叫 libxxx.a，但 AR 用的是 lib.exe，
# 内容就是普通的 MSVC 归档，改个名就能直接链接。
$libNames = @("avcodec", "avformat", "avutil", "swscale", "swresample")
$results = @()
foreach ($name in $libNames) {
    $produced = Get-ChildItem $source -Recurse -Filter "lib$name.a" -ErrorAction SilentlyContinue |
        Sort-Object Length -Descending | Select-Object -First 1
    if (-not $produced) { throw "没有生成 lib$name.a" }
    $results += [pscustomobject]@{ Name = "$name.lib"; Path = $produced.FullName; Length = $produced.Length }
}

Write-Host ""
Write-Host "=== 产物 ===" -ForegroundColor Cyan
foreach ($item in $results) {
    $target = Join-Path $libRoot "libffmpeg\$($item.Name)"
    $oldSize = if (Test-Path -LiteralPath $target) { (Get-Item -LiteralPath $target).Length / 1MB } else { 0 }
    "{0,-18} {1,8:N1} MB   （仓库现有 {2,8:N1} MB）" -f $item.Name, ($item.Length / 1MB), $oldSize
}

if ($Install) {
    $backup = Join-Path $repoRoot "lib-backup"
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($item in $results) {
        $target = Join-Path $libRoot "libffmpeg\$($item.Name)"
        $backupFile = Join-Path $backup $item.Name
        # 只在第一次建立备份，理由同 build-thirdparty-slim.ps1
        if ((Test-Path -LiteralPath $target) -and -not (Test-Path -LiteralPath $backupFile)) {
            Copy-Item -LiteralPath $target -Destination $backupFile -Force
        }
        Copy-Item -LiteralPath $item.Path -Destination $target -Force
    }
    Write-Host "已替换 libffmpeg 下的 5 个库，原件备份在 $backup" -ForegroundColor Yellow
    Write-Host "ImageDatabase.h 里 avdevice / avfilter 以及 FFmpeg 外挂的第三方库 pragma 都要删掉。"
} else {
    Write-Host ""
    Write-Host "加 -Install 可自动替换仓库里的库（原件备份到 lib-backup/）。" -ForegroundColor Yellow
}
