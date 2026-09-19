<#
.SYNOPSIS
    生成实况照片（动态照片）测试素材：一段带音轨的视频，按三种封装各出一份。

.DESCRIPTION
    视频由 tools/test-fixtures/make-motion-clip.cpp 用 Windows 自带的 Media Foundation 编码
    （H.264 + AAC），内容专为验证音画对齐设计：t = 1.000 s 那一帧整帧纯白，同一时刻有一声
    50 ms 的 1 kHz 响声，其余时间是轻微的 440 Hz 底音。

    三种封装分别走查看器里三条不同的加载路径：
      live-microvideo.jpg         谷歌 MicroVideo：JPEG 尾部附视频，XMP 的 GCamera:MicroVideoOffset 标出长度
      live-photo.livp             苹果 iCloud 导出的 .livp：zip 里一张 JPEG 加一段 MOV
      live-sidecar.jpg / .mov     苹果、VIVO 的同名配对：静态图旁边放同名视频文件

    需要 Visual Studio C++ 工具（编译生成器）和 ImageMagick（生成静态图）。
    生成结果提交进仓库；构造方式本身就是这份脚本和生成器源码，可审阅、可重建。
#>
param(
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\test\live-photo")
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$work = Join-Path ([IO.Path]::GetTempPath()) ("yeimageviewer-live-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $work, $OutputDir | Out-Null

try {
    # 1. 编译生成器
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) {
        throw "需要 Visual Studio C++ 工具来编译 make-motion-clip.cpp。"
    }
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
    $source = Join-Path $repoRoot "tools\test-fixtures\make-motion-clip.cpp"
    $generator = Join-Path $work "make-motion-clip.exe"
    $compile = "call `"$vcvars`" >nul 2>&1 && cl /nologo /EHsc /O2 /std:c++17 /utf-8 /W4 `"$source`" /Fe:`"$generator`" /Fo:`"$work\\`""
    cmd /c $compile | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $generator)) {
        throw "编译 make-motion-clip.cpp 失败。"
    }

    # 2. 生成带音轨的视频
    $clip = Join-Path $work "clip.mp4"
    & $generator $clip
    if ($LASTEXITCODE -ne 0) {
        throw "make-motion-clip 生成视频失败，退出码 $LASTEXITCODE。"
    }
    $clipBytes = [IO.File]::ReadAllBytes($clip)
    # 解码器会跳过小于 64 KiB 的视频（当作无效数据），素材必须明显大于这个门槛
    if ($clipBytes.Length -lt 131072) {
        throw "生成的视频只有 $($clipBytes.Length) 字节，太接近解码器的 64 KiB 门槛。"
    }

    # 3. 静态图：与视频同尺寸，去掉所有元数据，后面自己写 XMP
    if (-not (Get-Command magick -ErrorAction SilentlyContinue)) {
        throw "需要 ImageMagick（magick 命令）生成静态图。"
    }
    $still = Join-Path $work "still.jpg"
    # 不写文字：文字渲染依赖字体配置，换台机器可能就生成不出来
    & magick -size 640x480 "gradient:#3a5a7a-#c8b48c" -strip -quality 90 $still
    if ($LASTEXITCODE -ne 0) {
        throw "ImageMagick 生成静态图失败。"
    }
    $stillBytes = [IO.File]::ReadAllBytes($still)

    # 4a. 谷歌 MicroVideo：在 JPEG 里插入 XMP（APP1），再把视频接在文件尾部
    $xmp = @"
<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:GCamera="http://ns.google.com/photos/1.0/camera/"
    GCamera:MicroVideo="1"
    GCamera:MicroVideoVersion="1"
    GCamera:MicroVideoOffset="$($clipBytes.Length)"
    GCamera:MicroVideoPresentationTimestampUs="1000000"/>
 </rdf:RDF>
</x:xmpmeta>
"@
    # PowerShell 数组相加得到的是 object[]，这里显式声明成 byte[]
    [byte[]]$payload = [Text.Encoding]::ASCII.GetBytes("http://ns.adobe.com/xap/1.0/") + [byte]0 +
        [Text.Encoding]::UTF8.GetBytes($xmp)
    $segmentLength = $payload.Length + 2
    if ($segmentLength -gt 65535) {
        throw "XMP 太长，放不进一个 APP1 段。"
    }
    [byte[]]$app1 = [byte[]](0xFF, 0xE1, ($segmentLength -shr 8), ($segmentLength -band 0xFF)) + $payload
    if ($stillBytes[0] -ne 0xFF -or $stillBytes[1] -ne 0xD8) {
        throw "静态图不是 JPEG。"
    }
    # APP1 插在 SOI 之后；若有 JFIF 的 APP0，按规范排在它后面
    $insertAt = 2
    if ($stillBytes[2] -eq 0xFF -and $stillBytes[3] -eq 0xE0) {
        $insertAt = 4 + ($stillBytes[4] * 256 + $stillBytes[5])
    }
    $microVideo = New-Object System.IO.MemoryStream
    $microVideo.Write($stillBytes, 0, $insertAt)
    $microVideo.Write($app1, 0, $app1.Length)
    $microVideo.Write($stillBytes, $insertAt, $stillBytes.Length - $insertAt)
    $microVideo.Write($clipBytes, 0, $clipBytes.Length)
    [IO.File]::WriteAllBytes((Join-Path $OutputDir "live-microvideo.jpg"), $microVideo.ToArray())

    # 4b. 苹果 .livp：zip 里一张 JPEG 加一段 MOV（MP4 与 MOV 同属 ISO 媒体容器，FFmpeg 按内容识别）
    Add-Type -AssemblyName System.IO.Compression
    $livpPath = Join-Path $OutputDir "live-photo.livp"
    if (Test-Path -LiteralPath $livpPath) {
        [IO.File]::Delete($livpPath)
    }
    $zipStream = [IO.File]::Open($livpPath, [IO.FileMode]::CreateNew)
    try {
        $zip = New-Object System.IO.Compression.ZipArchive($zipStream, [IO.Compression.ZipArchiveMode]::Create)
        foreach ($entry in @(@("img_test.jpg", $stillBytes), @("img_test.mov", $clipBytes))) {
            $item = $zip.CreateEntry($entry[0], [IO.Compression.CompressionLevel]::NoCompression)
            $writer = $item.Open()
            $writer.Write($entry[1], 0, $entry[1].Length)
            $writer.Dispose()
        }
        $zip.Dispose()
    }
    finally {
        $zipStream.Dispose()
    }

    # 4c. 同名配对：静态图旁边放同名 .mov
    [IO.File]::WriteAllBytes((Join-Path $OutputDir "live-sidecar.jpg"), $stillBytes)
    [IO.File]::WriteAllBytes((Join-Path $OutputDir "live-sidecar.mov"), $clipBytes)

    Get-ChildItem -LiteralPath $OutputDir -File | ForEach-Object {
        "{0,-24} {1,10:N0} 字节" -f $_.Name, $_.Length
    }
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
