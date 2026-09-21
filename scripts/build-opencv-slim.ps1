<#
.SYNOPSIS
    重建 opencv_world 静态库，只留本程序真正用到的模块，并去掉 Intel IPP。

.DESCRIPTION
    仓库里原来那份 opencv_world4130.lib 是按「全功能」构建的：带上了 opencv_contrib
    的全部模块（tracking、xfeatures2d、ximgproc、stitching、gapi……）、videoio、highgui，
    还链进了 Intel IPP 的静态 blob。本程序只调用 core / imgproc / imgcodecs 三个模块，
    连一次 imshow 都没有，这些统统是死重量，其中 IPP 一项就占 exe 约 25 MiB。

    砍掉的东西和理由：
      IPP / IPP IW   —— 约 25 MiB。imgproc 会退回 OpenCV 自带的 SSE/AVX2 实现，
                        发布闸门里的性能压测负责盯住有没有变慢。
      contrib 全部   —— 一行都没调用。
      videoio        —— 本程序自带 FFmpeg 解码，不走 OpenCV 的视频通道。
      highgui        —— 只有一句 #include，没有任何调用。（README 里提到的
                        「HighGUI 光标从 IDC_CROSS 改成 IDC_ARROW」也随之不再需要。）
      ITT / OpenCL   —— 只在性能分析和 GPU 路径上用得到。

    分辨率上限仍然要改源码（见下面的补丁段）。这三个上限虽然也认环境变量，但它们是
    loadsave.cpp 里命名空间作用域的 static const，CRT 在进入 wWinMain 之前就初始化完了，
    程序里再设环境变量根本来不及——试过，结果是 240MP 以上的 PNG 全被拒绝。

    图像编解码器全部保留（JPEG/PNG/TIFF/WEBP/OpenJPEG/OpenEXR/GIF/HDR/SUNRASTER/PXM/PFM），
    与原库一致，否则语料测试会当场报格式不支持。

    SIMD 基线与派发也照抄原库（SSE3 基线，派发到 AVX2 / AVX512_SKX），
    免得换个构建就悄悄改了指令集要求。

.EXAMPLE
    .\build-opencv-slim.ps1                 # 下载源码并构建，产物留在工作目录
    .\build-opencv-slim.ps1 -Install        # 构建完成后替换仓库里的库（原件自动备份）
#>
param(
    [string]$WorkDir = (Join-Path $env:TEMP "yeimageviewer-slim-opencv"),
    [switch]$Install
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$libRoot = Join-Path $repoRoot "YeImageViewer"
$version = "4.13.0"
$archiveUrl = "https://github.com/opencv/opencv/archive/refs/tags/$version.tar.gz"
$archiveSha256 = "1D40CA017EA51C533CF9FD5CBDE5B5FE7AE248291DDF2AF99D4C17CF8E13017D"

$srcDir = Join-Path $WorkDir "src"
$buildDir = Join-Path $WorkDir "build"
New-Item -ItemType Directory -Force -Path $srcDir, $buildDir | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "找不到 vswhere.exe，请先安装 Visual Studio 或 Build Tools" }
$vsPath = & $vswhere -latest -property installationPath
$cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
foreach ($tool in @($cmake, $ninja)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "缺少 $tool，请在 Visual Studio 安装程序里勾选「适用于 Windows 的 C++ CMake 工具」"
    }
}

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "call `"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match "^([^=]+)=(.*)$") { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}

$source = Join-Path $srcDir "opencv-$version"
if (-not (Test-Path -LiteralPath $source)) {
    $archive = Join-Path $srcDir "opencv-$version.tar.gz"
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "下载 OpenCV $version ..."
        Invoke-WebRequest -Uri $archiveUrl -OutFile $archive -UseBasicParsing
    }
    $actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actualSha256 -ne $archiveSha256) {
        throw "OpenCV 源码包校验失败：期望 $archiveSha256，实际 $actualSha256"
    }
    Write-Host "解压 ..."
    tar -xzf $archive -C $srcDir
    if (-not (Test-Path -LiteralPath $source)) { throw "解压后没有找到 $source" }
}

# imgcodecs 默认拒收超过 2^20 边长、2^30 像素的图片，看图软件不该有这个上限。
# 只能改源码，不能靠环境变量：这三个上限是 loadsave.cpp 里的命名空间作用域
# static const，CRT 在进入 wWinMain 之前就把它们初始化完了，程序里再设环境变量已经晚了。
# （动画/PNG 那条路还会把像素数乘以「帧数 + 4」，所以静态 PNG 的实际上限只有
#  2^30 / 5 ≈ 2.15 亿像素——240MP 的图就是卡在这儿被拒的。）
$loadsave = Join-Path $source "modules\imgcodecs\src\loadsave.cpp"
$loadsaveText = [IO.File]::ReadAllText($loadsave)
$limitPatches = @(
    @{ Old = '"OPENCV_IO_MAX_IMAGE_WIDTH", 1 << 20';  New = '"OPENCV_IO_MAX_IMAGE_WIDTH", 1 << 22' },
    @{ Old = '"OPENCV_IO_MAX_IMAGE_HEIGHT", 1 << 20'; New = '"OPENCV_IO_MAX_IMAGE_HEIGHT", 1 << 22' },
    @{ Old = '"OPENCV_IO_MAX_IMAGE_PIXELS", 1 << 30'; New = '"OPENCV_IO_MAX_IMAGE_PIXELS", (size_t)1 << 38' }
)
$patched = 0
foreach ($patch in $limitPatches) {
    if ($loadsaveText.Contains($patch.Old)) {
        $loadsaveText = $loadsaveText.Replace($patch.Old, $patch.New)
        $patched++
    }
    elseif (-not $loadsaveText.Contains($patch.New)) {
        throw "loadsave.cpp 里找不到待放宽的上限：$($patch.Old)。换 OpenCV 版本后要重新核对这段补丁。"
    }
}
if ($patched -gt 0) {
    [IO.File]::WriteAllText($loadsave, $loadsaveText)
    Write-Host "已放宽 imgcodecs 的 $patched 处尺寸上限（边长 2^22，像素 2^38）"
}

$options = @(
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",   # 与主程序的 /MT 一致
    "-DCMAKE_CXX_STANDARD=23",                      # 与主程序一致，避免 STL 跨库不一致
    "-DBUILD_SHARED_LIBS=OFF",

    # 只要这三个模块；缺了什么，主程序链接时会直接报未解析符号，不会悄悄少功能。
    # highgui 必须留在列表里：world 的 CMakeLists 无条件调用 highgui 定义的
    # ocv_highgui_configure_target，不带它配置直接报 Unknown CMake command。
    # 但把所有界面后端关掉，它只剩一层调用即抛错的空壳，不占体积。
    "-DBUILD_LIST=core,imgproc,imgcodecs,highgui",
    "-DBUILD_opencv_world=ON",
    "-DWITH_WIN32UI=OFF", "-DHIGHGUI_ENABLE_PLUGINS=OFF", "-DWITH_OPENGL=OFF",

    "-DWITH_IPP=OFF", "-DBUILD_IPP_IW=OFF", "-DWITH_ITT=OFF",
    "-DWITH_OPENCL=OFF", "-DWITH_OPENCL_SVM=OFF",
    "-DWITH_FFMPEG=OFF", "-DWITH_MSMF=OFF", "-DWITH_DSHOW=OFF", "-DWITH_OBSENSOR=OFF",
    "-DWITH_PROTOBUF=OFF", "-DWITH_ADE=OFF", "-DWITH_EIGEN=OFF", "-DWITH_TBB=OFF",
    "-DWITH_QUIRC=OFF", "-DWITH_LAPACK=OFF", "-DWITH_VTK=OFF", "-DWITH_GSTREAMER=OFF",
    "-DWITH_1394=OFF", "-DWITH_ARITH_DEC=ON", "-DWITH_ARITH_ENC=ON",

    # 编解码器保持与原库一致，一个都不能少
    "-DWITH_JPEG=ON", "-DBUILD_JPEG=ON",
    "-DWITH_PNG=ON", "-DBUILD_PNG=ON",
    "-DWITH_TIFF=ON", "-DBUILD_TIFF=ON",
    "-DWITH_WEBP=ON", "-DBUILD_WEBP=ON",
    "-DWITH_OPENJPEG=ON", "-DBUILD_OPENJPEG=ON",
    "-DWITH_OPENEXR=ON", "-DBUILD_OPENEXR=ON",
    "-DWITH_IMGCODEC_GIF=ON", "-DWITH_IMGCODEC_HDR=ON",
    "-DWITH_IMGCODEC_SUNRASTER=ON", "-DWITH_IMGCODEC_PXM=ON", "-DWITH_IMGCODEC_PFM=ON",
    "-DBUILD_ZLIB=ON",

    # 指令集与原库一致
    "-DCPU_BASELINE=SSE3",
    "-DCPU_DISPATCH=SSE4_1,SSE4_2,AVX,FP16,AVX2,AVX512_SKX",

    "-DBUILD_TESTS=OFF", "-DBUILD_PERF_TESTS=OFF", "-DBUILD_EXAMPLES=OFF",
    "-DBUILD_DOCS=OFF", "-DBUILD_opencv_apps=OFF", "-DBUILD_JAVA=OFF",
    "-DBUILD_PACKAGE=OFF", "-DINSTALL_TESTS=OFF",
    "-DOPENCV_GENERATE_SETUPVARS=OFF", "-DOPENCV_GENERATE_PKGCONFIG=OFF",

    "-S", $source, "-B", $buildDir
)

Write-Host "=== 配置 OpenCV ===" -ForegroundColor Cyan
$log = & $cmake @options 2>&1
if ($LASTEXITCODE -ne 0) { $log | ForEach-Object { Write-Host $_ }; throw "OpenCV 配置失败" }
$log | Select-String -Pattern "To be built|Disabled:|Intel IPP|ZLib|JPEG|PNG|TIFF|WEBP|OpenEXR|JPEG 2000|Baseline|Dispatched" |
    ForEach-Object { Write-Host $_.Line }

Write-Host "=== 编译 OpenCV（十几分钟起步）===" -ForegroundColor Cyan
& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "OpenCV 编译失败" }

$produced = Get-ChildItem $buildDir -Recurse -Filter "opencv_world*.lib" |
    Sort-Object Length -Descending | Select-Object -First 1
if (-not $produced) { throw "没有生成 opencv_world 静态库" }

$target = Join-Path $libRoot "libopencv\opencv_world4130.lib"
$oldSize = if (Test-Path -LiteralPath $target) { (Get-Item -LiteralPath $target).Length / 1MB } else { 0 }
Write-Host ""
"产物 {0}  {1:N1} MB   （仓库现有 {2:N1} MB）" -f $produced.Name, ($produced.Length / 1MB), $oldSize

if ($Install) {
    $backup = Join-Path $repoRoot "lib-backup"
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    $backupFile = Join-Path $backup "opencv_world4130.lib"
    # 只在第一次建立备份，理由同 build-thirdparty-slim.ps1
    if ((Test-Path -LiteralPath $target) -and -not (Test-Path -LiteralPath $backupFile)) {
        Copy-Item -LiteralPath $target -Destination $backupFile -Force
    }
    Copy-Item -LiteralPath $produced.FullName -Destination $target -Force
    Write-Host "已替换 $target，原件备份在 $backup" -ForegroundColor Yellow
    Write-Host "接着跑 .\buildRelease.ps1，再跑 .\run-release-tests.ps1 验证解码与性能。"
} else {
    Write-Host ""
    Write-Host "加 -Install 可自动替换仓库里的库（原件备份到 lib-backup/）。" -ForegroundColor Yellow
}
