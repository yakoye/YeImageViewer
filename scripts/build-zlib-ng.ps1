<#
.SYNOPSIS
    用 zlib-ng 重建 zlib.lib，PNG 解压提速约 1.4 倍。

.DESCRIPTION
    打开一张 291 MB、9000×9000、16 位的 PNG，解码要 2.2 秒，其中 1.56 秒是 inflate
    ——占七成。zlib 1.3.1 在这台机器上是 297 MB/s，而 zlib-ng 同一段数据跑到 416 MB/s
    （用 .NET 10 内置的 zlib-ng 实测的对照）。

    换法很省事：仓库里只有一份 zlib（`YeImageViewer/libopencv/zlib.lib`），
    OpenCV 的 opencv_world 是在最终链接时才去解析 inflate 这些符号的，
    所以把这个 .lib 换成 zlib-ng 的 compat 构建就够了，不必重建 OpenCV。

    ZLIB_COMPAT=ON 让 zlib-ng 导出与 zlib 完全相同的函数名和结构体布局，
    对已经编译好的 opencv_world、minizip、exiv2 来说是原地替换。
    运行时按 CPU 派发（SSE2 / SSSE3 / AVX2），不需要改基线要求。

.EXAMPLE
    .\build-zlib-ng.ps1                 # 只构建，产物留在工作目录
    .\build-zlib-ng.ps1 -Install        # 构建完成后替换仓库里的库与头文件（原件自动备份）
#>
param(
    [string]$WorkDir = (Join-Path $env:TEMP "yeimageviewer-zlib-ng"),
    [switch]$Install
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$libRoot = Join-Path $repoRoot "YeImageViewer"
$version = "2.2.4"
$archiveUrl = "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/$version.tar.gz"
$archiveSha256 = "A73343C3093E5CDC50D9377997C3815B878FD110BF6511C2C7759F2AFB90F5A3"

$srcDir = Join-Path $WorkDir "src"
$buildDir = Join-Path $WorkDir "build"
$installDir = Join-Path $WorkDir "install"
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

$source = Join-Path $srcDir "zlib-ng-$version"
if (-not (Test-Path -LiteralPath $source)) {
    $archive = Join-Path $srcDir "zlib-ng-$version.tar.gz"
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "下载 zlib-ng $version ..."
        Invoke-WebRequest -Uri $archiveUrl -OutFile $archive -UseBasicParsing
    }
    $actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actualSha256 -ne $archiveSha256) {
        throw "zlib-ng 源码包校验失败：期望 $archiveSha256，实际 $actualSha256"
    }
    Write-Host "解压 ..."
    tar -xzf $archive -C $srcDir
    if (-not (Test-Path -LiteralPath $source)) { throw "解压后没有找到 $source" }
}

$options = @(
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",   # 与主程序的 /MT 一致
    "-DCMAKE_INSTALL_PREFIX=$installDir",
    "-DBUILD_SHARED_LIBS=OFF",

    # 关键：导出与 zlib 完全相同的符号名和结构体布局，对已编译好的库是原地替换
    "-DZLIB_COMPAT=ON",

    # 运行时按 CPU 派发，不抬高基线要求
    "-DWITH_OPTIM=ON", "-DWITH_NATIVE_INSTRUCTIONS=OFF",

    "-DZLIB_ENABLE_TESTS=OFF", "-DZLIBNG_ENABLE_TESTS=OFF", "-DWITH_GTEST=OFF",
    "-DWITH_BENCHMARKS=OFF",

    "-S", $source, "-B", $buildDir
)

Write-Host "=== 配置 zlib-ng ===" -ForegroundColor Cyan
$log = & $cmake @options 2>&1
if ($LASTEXITCODE -ne 0) { $log | ForEach-Object { Write-Host $_ }; throw "zlib-ng 配置失败" }
$log | Select-String -Pattern "Compat|compat|Optim|SSE|AVX|Build type" | ForEach-Object { Write-Host $_.Line }

Write-Host "=== 编译 zlib-ng ===" -ForegroundColor Cyan
& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "zlib-ng 编译失败" }
& $cmake --install $buildDir | Out-Null
if ($LASTEXITCODE -ne 0) { throw "zlib-ng 安装失败" }

$producedLib = Get-ChildItem (Join-Path $installDir "lib") -Filter "*.lib" |
    Sort-Object Length -Descending | Select-Object -First 1
if (-not $producedLib) { throw "没有生成静态库" }
# zconf.h 会 #include "zlib_name_mangling.h"，少拷这一个编译就直接报找不到头文件
$producedHeaders = @("zlib.h", "zconf.h", "zlib_name_mangling.h") | ForEach-Object {
    $header = Join-Path $installDir "include\$_"
    if (-not (Test-Path -LiteralPath $header)) { throw "缺少头文件 $_" }
    $header
}

$targetLib = Join-Path $libRoot "libopencv\zlib.lib"
$oldSize = if (Test-Path -LiteralPath $targetLib) { (Get-Item -LiteralPath $targetLib).Length / 1KB } else { 0 }
Write-Host ""
"产物 {0}  {1:N0} KB   （仓库现有 {2:N0} KB）" -f $producedLib.Name, ($producedLib.Length / 1KB), $oldSize

if (-not $Install) {
    Write-Host ""
    Write-Host "未安装。加 -Install 参数可替换仓库里的库与头文件。" -ForegroundColor Yellow
    return
}

# 原件备份到 lib-backup/，和其他几个重建脚本一个规矩（那个目录已在 .gitignore 里）。
# 备份只在第一次建立：反复 -Install 时不能让备份被新文件覆盖，
# 否则想回退到原始的 zlib 1.3.1 就再也回不去了。
$backupDir = Join-Path $repoRoot "lib-backup\zlib-1.3.1"
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
foreach ($pair in @(
    @{ Source = $producedLib.FullName; Target = $targetLib },
    @{ Source = $producedHeaders[0];   Target = (Join-Path $libRoot "include\zlib.h") },
    @{ Source = $producedHeaders[1];   Target = (Join-Path $libRoot "include\zconf.h") },
    @{ Source = $producedHeaders[2];   Target = (Join-Path $libRoot "include\zlib_name_mangling.h") })) {
    $backup = Join-Path $backupDir ([IO.Path]::GetFileName($pair.Target))
    if ((Test-Path -LiteralPath $pair.Target) -and -not (Test-Path -LiteralPath $backup)) {
        Copy-Item -LiteralPath $pair.Target -Destination $backup
        Write-Host "已备份 $([IO.Path]::GetFileName($pair.Target)) → lib-backup/zlib-1.3.1/"
    }
    Copy-Item -LiteralPath $pair.Source -Destination $pair.Target -Force
    Write-Host "已替换 $($pair.Target)"
}

Write-Host ""
Write-Host "装好了。重新构建主程序后跑一遍发布闸门：" -ForegroundColor Green
Write-Host "  ./buildRelease.ps1 ; ./runTests.ps1"
