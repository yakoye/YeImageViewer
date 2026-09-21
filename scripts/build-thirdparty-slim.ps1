<#
.SYNOPSIS
    重建 libavif 与 libheif 静态库，去掉看图软件用不到的编码器，产出精简版 avif.lib / heif.lib。

.DESCRIPTION
    看图软件只解码不编码，但上游 static_lib 包里的 avif.lib 与 heif.lib 是按「全功能」
    构建的：libavif 拉进了 aom 的 AV1 编码器，libheif 拉进了 x265 的 HEVC 编码器和
    aom 的一整套 AV1 编解码。这些编码器一个字节都用不上，却实打实占着 exe：

        avif.lib  1.7 MB  → 0.6 MB    （AV1 解码改由已在链接的 dav1d 承担）
        heif.lib  150 MB  → 34 MB
        x265-static.lib    不再链接    （HEVC 编码，exe 里约 4.9 MiB）

    合计主程序减少约 4.9 MiB（88.68 → 83.78 MiB），解码能力零损失：AVIF 的 8/10 位、
    透明通道、灰度、奇数尺寸，以及 HEIC/HEIF 语料全部照常通过。

    aom.lib 暂时还不能去掉——FFmpeg 的 avcodec 里带着 libaom 的编解码包装
    （libaomenc.o / libaomdec.o）仍在引用它，等 FFmpeg 换成最小化构建后可一并移除，
    那时 exe 还能再少约 9.4 MiB。

    跑完本脚本后需要用新的 avif.lib / heif.lib 替换仓库里的同名文件（-Install 会自动做，
    并把原件备份到 lib-backup/）。注意 ImageDatabase.h 已经不再 #pragma comment
    链接 x265-static.lib，所以直接拿旧的 static_lib 包是链接不过去的——必须跑一次本脚本。

    解码依赖不重复构建，直接用仓库里现成的：
        dav1d.lib     YeImageViewer/libavif/dav1d.lib    AV1 解码
        libde265.lib  YeImageViewer/lib/libde265.lib     HEVC 解码
        yuv.lib       YeImageViewer/lib/yuv.lib          色彩空间转换

.EXAMPLE
    .\build-thirdparty-slim.ps1                  # 下载源码、构建两个库，产物留在工作目录
    .\build-thirdparty-slim.ps1 -Install         # 构建完成后替换仓库里的库（原件自动备份）
    .\build-thirdparty-slim.ps1 -SkipHeif        # 只重建 libavif（libheif 编译较慢）
#>
param(
    [string]$WorkDir = (Join-Path $env:TEMP "yeimageviewer-slim-libs"),
    [switch]$SkipAvif,
    [switch]$SkipHeif,
    [switch]$Install                  # 把产物复制进仓库，原件备份到 lib-backup/
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$libRoot = Join-Path $repoRoot "YeImageViewer"     # 静态库与头文件所在的工程目录

# 源码包。版本要与上游 static_lib 包一致，换版本前先确认解码行为没变。
$packages = @(
    @{
        Name    = "libavif"
        Version = "1.3.0"
        Url     = "https://github.com/AOMediaCodec/libavif/archive/refs/tags/v1.3.0.tar.gz"
        Sha256  = "0a545e953cc049bf5bcf4ee467306a2f113a75110edf59e61248873101cd26c1"
    },
    @{
        Name    = "libheif"
        Version = "1.20.1"
        Url     = "https://github.com/strukturag/libheif/releases/download/v1.20.1/libheif-1.20.1.tar.gz"
        Sha256  = "9d3d601ec7a55281217aaa6c773cf6645757b062bc7e9680b664bbd8e481112d"
    }
)

$srcDir = Join-Path $WorkDir "src"
$buildDir = Join-Path $WorkDir "build"
New-Item -ItemType Directory -Force -Path $srcDir, $buildDir | Out-Null

# ---- 工具链 ------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "找不到 vswhere.exe，请先安装 Visual Studio 或 Build Tools" }
$vsPath = & $vswhere -latest -property installationPath
if (-not $vsPath) { throw "vswhere 没有找到 Visual Studio 安装" }

$cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
foreach ($tool in @($cmake, $ninja)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "缺少 $tool，请在 Visual Studio 安装程序里勾选「适用于 Windows 的 C++ CMake 工具」"
    }
}

# Ninja 生成器要求 cl.exe 已在 PATH 上，把 vcvars64 的环境导进当前会话。
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "call `"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}

# ---- 源码 --------------------------------------------------------------------
function Get-Source([hashtable]$Package) {
    $name = "$($Package.Name)-$($Package.Version)"
    $extracted = Join-Path $srcDir $name
    if (Test-Path -LiteralPath $extracted) { return $extracted }

    $archive = Join-Path $srcDir "$name.tar.gz"
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "下载 $name ..."
        Invoke-WebRequest -Uri $Package.Url -OutFile $archive -UseBasicParsing
    }
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actual -ne $Package.Sha256.ToUpper()) {
        throw "$name 校验失败：期望 $($Package.Sha256)，实际 $actual"
    }
    Write-Host "解压 $name ..."
    tar -xzf $archive -C $srcDir
    if (-not (Test-Path -LiteralPath $extracted)) { throw "解压后没有找到 $extracted" }
    return $extracted
}

# libavif 的 check_avif_option 看到同名导入目标已存在就直接用，不再去 find_package，
# 这样不用给它准备 pkg-config / CMake 配置文件，就能复用仓库里现成的静态库。
$depsCmake = Join-Path $WorkDir "avif-deps.cmake"
$libRootCMake = $libRoot.Replace("\", "/")
@"
set(YIV_LIB_ROOT "$libRootCMake")

add_library(dav1d::dav1d STATIC IMPORTED GLOBAL)
set_target_properties(dav1d::dav1d PROPERTIES
    IMPORTED_LOCATION "`${YIV_LIB_ROOT}/libavif/dav1d.lib"
    INTERFACE_INCLUDE_DIRECTORIES "`${YIV_LIB_ROOT}/include")

add_library(yuv::yuv STATIC IMPORTED GLOBAL)
set_target_properties(yuv::yuv PROPERTIES
    IMPORTED_LOCATION "`${YIV_LIB_ROOT}/lib/yuv.lib"
    INTERFACE_INCLUDE_DIRECTORIES "`${YIV_LIB_ROOT}/include")

# libavif 按版本号决定启用哪些 libyuv 快速路径，仓库里这份是 1895。
set(LIBYUV_VERSION 1895)
"@ | Set-Content -LiteralPath $depsCmake -Encoding UTF8

# ---- 构建 --------------------------------------------------------------------
function Invoke-SlimBuild([string]$Name, [string]$Source, [string[]]$Options) {
    $build = Join-Path $buildDir $Name
    New-Item -ItemType Directory -Force -Path $build | Out-Null

    $common = @(
        "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=$ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        # 主程序是 /MT，静态库也必须是 /MT，否则链接时 LNK2038 运行时库不匹配。
        # libavif 的 cmake_minimum_required 偏低，CMP0091 默认还是 OLD，
        # 不显式设成 NEW 的话 CMAKE_MSVC_RUNTIME_LIBRARY 会被忽略、悄悄构建成 /MD。
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
        "-DBUILD_SHARED_LIBS=OFF",
        "-S", $Source, "-B", $build
    )

    # cmake 的输出不能留在管道里，否则会跟着函数返回值一起传出去。
    Write-Host "=== 配置 $Name ===" -ForegroundColor Cyan
    $log = & $cmake @common @Options 2>&1
    if ($LASTEXITCODE -ne 0) { $log | ForEach-Object { Write-Host $_ }; throw "$Name 配置失败" }
    $log | Select-String -Pattern "^-- (Enabled|Disabled|Compiled|Using)|error" | ForEach-Object { Write-Host $_.Line }

    Write-Host "=== 编译 $Name ===" -ForegroundColor Cyan
    $log = & $cmake --build $build --parallel 2>&1
    if ($LASTEXITCODE -ne 0) { $log | ForEach-Object { Write-Host $_ }; throw "$Name 编译失败" }
    $log | Select-Object -Last 2 | ForEach-Object { Write-Host $_ }

    return $build
}

$results = @()

if (-not $SkipAvif) {
    $source = Get-Source $packages[0]
    $build = Invoke-SlimBuild "libavif" $source @(
        "-DCMAKE_PROJECT_INCLUDE_BEFORE=$($depsCmake.Replace('\', '/'))",
        "-DAVIF_CODEC_AOM=OFF",          # 砍掉：AV1 编码器，解码交给 dav1d
        "-DAVIF_CODEC_DAV1D=SYSTEM",
        "-DAVIF_LIBYUV=SYSTEM",
        "-DAVIF_BUILD_APPS=OFF", "-DAVIF_BUILD_TESTS=OFF", "-DAVIF_BUILD_EXAMPLES=OFF",
        "-DAVIF_BUILD_GDK_PIXBUF=OFF", "-DAVIF_ENABLE_WERROR=OFF"
    )
    $results += [pscustomobject]@{ Lib = "avif.lib"; Path = (Join-Path $build "avif.lib"); Target = (Join-Path $libRoot "libavif\avif.lib") }
}

if (-not $SkipHeif) {
    $source = Get-Source $packages[1]
    $build = Invoke-SlimBuild "libheif" $source @(
        "-DWITH_LIBDE265=ON",                                  # HEVC 解码，HEIC 主力
        "-DLIBDE265_INCLUDE_DIR=$libRootCMake/include",
        "-DLIBDE265_LIBRARY=$libRootCMake/lib/libde265.lib",
        "-DWITH_DAV1D=ON",                                     # AVIF-in-HEIF 解码，顶替 aom
        "-DDAV1D_INCLUDE_DIR=$libRootCMake/include",
        "-DDAV1D_LIBRARY=$libRootCMake/libavif/dav1d.lib",
        "-DWITH_UNCOMPRESSED_CODEC=ON",                        # 1.20 默认关，显式打开免得丢能力
        "-DWITH_AOM_DECODER=OFF", "-DWITH_AOM_ENCODER=OFF",    # 砍掉：AV1，改由 dav1d 解码
        "-DWITH_X265=OFF",                                     # 砍掉：HEVC 编码器
        "-DWITH_SvtEnc=OFF", "-DWITH_RAV1E=OFF", "-DWITH_KVAZAAR=OFF", "-DWITH_UVG266=OFF",
        "-DWITH_JPEG_DECODER=OFF", "-DWITH_JPEG_ENCODER=OFF",  # JPEG 走 OpenCV，不从这里出
        "-DWITH_OpenJPEG_DECODER=OFF", "-DWITH_OpenJPEG_ENCODER=OFF", "-DWITH_OPENJPH_ENCODER=OFF",
        "-DWITH_FFMPEG_DECODER=OFF", "-DWITH_VVDEC=OFF", "-DWITH_VVENC=OFF", "-DWITH_OpenH264_DECODER=OFF",
        "-DWITH_LIBSHARPYUV=OFF", "-DWITH_HEADER_COMPRESSION=OFF",
        "-DWITH_EXAMPLES=OFF", "-DBUILD_TESTING=OFF", "-DENABLE_PLUGIN_LOADING=OFF",
        "-DWITH_REDUCED_VISIBILITY=OFF"
    )
    $results += [pscustomobject]@{ Lib = "heif.lib"; Path = (Join-Path $build "libheif\heif.lib"); Target = (Join-Path $libRoot "lib\heif.lib") }
}

# ---- 产物 --------------------------------------------------------------------
Write-Host ""
Write-Host "=== 产物 ===" -ForegroundColor Cyan
foreach ($item in $results) {
    if (-not (Test-Path -LiteralPath $item.Path)) { throw "没有生成 $($item.Path)" }
    $size = (Get-Item -LiteralPath $item.Path).Length / 1MB
    $oldSize = if (Test-Path -LiteralPath $item.Target) { (Get-Item -LiteralPath $item.Target).Length / 1MB } else { 0 }
    "{0,-10} {1,8:N2} MB   （仓库现有 {2,8:N2} MB）" -f $item.Lib, $size, $oldSize
}

if ($Install) {
    $backup = Join-Path $repoRoot "lib-backup"
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    foreach ($item in $results) {
        if (Test-Path -LiteralPath $item.Target) {
            Copy-Item -LiteralPath $item.Target -Destination (Join-Path $backup $item.Lib) -Force
        }
        Copy-Item -LiteralPath $item.Path -Destination $item.Target -Force
        Write-Host "已替换 $($item.Target)"
    }
    Write-Host "原件备份在 $backup" -ForegroundColor Yellow
    Write-Host "接着跑 .\buildRelease.ps1 重新构建主程序。"
} else {
    Write-Host ""
    Write-Host "加 -Install 可自动替换仓库里的库（原件备份到 lib-backup/）。" -ForegroundColor Yellow
}
