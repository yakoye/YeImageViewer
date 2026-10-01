<#
.SYNOPSIS
    构建 libdeflate，供 PNG 快路径整块解压用。

.DESCRIPTION
    libdeflate 只提供「整块进、整块出」的解压接口，没有 zlib 那样的流式 API，
    所以它替代不了 libpng 内部的 inflate，只能配合我们自己的 PNG 快路径
    （见 `PngFastDecode.h`）：把所有 IDAT 拼起来一次解完。

    实测同一段 IDAT（moon_81M.png，463 MB 原始数据）：
      zlib-ng      827 ms   560 MB/s
      libdeflate   629 ms   737 MB/s   快 1.32 倍
    两边结果逐字节一致。

    zlib（zlib-ng）仍然必须保留：libpng、minizip、exiv2 都在用它，
    快路径不接的那些 PNG 也还是走 libpng。

.EXAMPLE
    .\build-libdeflate.ps1                 # 只构建，产物留在工作目录
    .\build-libdeflate.ps1 -Install        # 构建完成后把库和头文件装进仓库
#>
param(
    [string]$WorkDir = (Join-Path $env:TEMP "yeimageviewer-libdeflate"),
    [switch]$Install
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$libRoot = Join-Path $repoRoot "YeImageViewer"
$version = "1.24"
$archiveUrl = "https://github.com/ebiggers/libdeflate/archive/refs/tags/v$version.tar.gz"
$archiveSha256 = "AD8D3723D0065C4723AB738BE9723F2FF1CB0F1571E8BFCF0301FF9661F475E8"

$srcDir = Join-Path $WorkDir "src"
$buildDir = Join-Path $WorkDir "build"
New-Item -ItemType Directory -Force -Path $srcDir | Out-Null

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

$source = Join-Path $srcDir "libdeflate-$version"
if (-not (Test-Path -LiteralPath $source)) {
    $archive = Join-Path $srcDir "libdeflate-$version.tar.gz"
    if (-not (Test-Path -LiteralPath $archive)) {
        Write-Host "下载 libdeflate $version ..."
        Invoke-WebRequest -Uri $archiveUrl -OutFile $archive -UseBasicParsing
    }
    $actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($actualSha256 -ne $archiveSha256) {
        throw "libdeflate 源码包校验失败：期望 $archiveSha256，实际 $actualSha256"
    }
    Write-Host "解压 ..."
    tar -xzf $archive -C $srcDir
    if (-not (Test-Path -LiteralPath $source)) { throw "解压后没有找到 $source" }
}

# 只要解压，压缩那半边用不上（看图软件不写 PNG 走这条路）
$options = @(
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",   # 与主程序的 /MT 一致
    "-DLIBDEFLATE_BUILD_SHARED_LIB=OFF",
    "-DLIBDEFLATE_BUILD_GZIP=OFF",
    "-DLIBDEFLATE_BUILD_TESTS=OFF",
    "-S", $source, "-B", $buildDir
)

Write-Host "=== 配置 libdeflate ===" -ForegroundColor Cyan
$log = & $cmake @options 2>&1
if ($LASTEXITCODE -ne 0) { $log | ForEach-Object { Write-Host $_ }; throw "libdeflate 配置失败" }

Write-Host "=== 编译 libdeflate ===" -ForegroundColor Cyan
& $cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "libdeflate 编译失败" }

$produced = Get-ChildItem $buildDir -Recurse -Filter "*.lib" |
    Sort-Object Length -Descending | Select-Object -First 1
if (-not $produced) { throw "没有生成静态库" }
$header = Join-Path $source "libdeflate.h"
if (-not (Test-Path -LiteralPath $header)) { throw "找不到 libdeflate.h" }

$target = Join-Path $libRoot "lib\libdeflate.lib"
$oldSize = if (Test-Path -LiteralPath $target) { (Get-Item -LiteralPath $target).Length / 1KB } else { 0 }
Write-Host ""
"产物 {0}  {1:N0} KB   （仓库现有 {2:N0} KB）" -f $produced.Name, ($produced.Length / 1KB), $oldSize

if (-not $Install) {
    Write-Host ""
    Write-Host "未安装。加 -Install 参数可装进仓库。" -ForegroundColor Yellow
    return
}

Copy-Item -LiteralPath $produced.FullName -Destination $target -Force
Copy-Item -LiteralPath $header -Destination (Join-Path $libRoot "include\libdeflate.h") -Force
Write-Host "已装入 $target"
Write-Host "已装入 $(Join-Path $libRoot "include\libdeflate.h")"
Write-Host ""
Write-Host "重新构建主程序后跑 PNG 快路径自检：" -ForegroundColor Green
Write-Host "  ./buildRelease.ps1"
Write-Host "  ./x64/Release/YeImageViewer.exe --png-decode-selftest <含 PNG 的目录> result.txt"
