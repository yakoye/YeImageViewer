# 翻页响应性回归测试
#
# 连续快速翻页时主线程不得卡在图片解码上。历史上 preImg/nextImg 走的是阻塞式
# getCheckedPtr，翻到大图会把 DrawScene 循环整个堵住，表现为「顿」。
# 这里对着一批解码较慢的图连续发翻页键，每次紧跟一个 SendMessageTimeout(WM_NULL)——
# 主线程若正卡在解码里就答不上来，耗时会直接反映被堵住的时长。
#
# 实测对照（14 次翻页，每张约 253 ms 解码）：
#   阻塞版   中位 283.2 ms   最大 303.4 ms
#   非阻塞版 中位  15.2 ms   最大  16.3 ms
#
# 用法:
#   ./probe-paging-responsiveness.ps1
#   ./probe-paging-responsiveness.ps1 -Folder D:\some\big\images -Pages 20

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\x64\Release\YeImageViewer.exe"),
    # 留空则在临时目录生成一批解码较慢的图（需要 ImageMagick）
    [string]$Folder = "",
    [int]$Pages = 14,
    # 判定阈值：主线程响应超过这个值即视为被解码阻塞
    [double]$MaxLatencyMs = 80,
    [double]$MedianLatencyMs = 40
)

$ErrorActionPreference = "Stop"

# 退出码约定：0 通过，1 失败，3 未执行（缺少可执行文件或素材）。
# 「未执行」不能退 0：发布闸门会把它当成通过。
$EXIT_SKIPPED = 3

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class PagingProbe {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(
        IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint timeout, out UIntPtr res);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);

    // 按进程枚举顶层窗口，而不是看前台窗口：锁屏时前台属于 LogonUI，
    // 按前台判定会取不到主窗口，给出与被测行为无关的假 FAIL。
    // 本测试只发消息、不读屏幕，锁屏下本该照常可跑。
    public static IntPtr FindMainWindow(uint targetPid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint pid = 0;
            GetWindowThreadProcessId(h, out pid);
            if (pid != targetPid || !IsWindowVisible(h)) return true;
            RECT r;
            if (!GetWindowRect(h, out r)) return true;
            if (r.R - r.L < 300 || r.B - r.T < 200) return true;
            found = h;
            return false;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101; $WM_NULL = 0x0000
$VK_RIGHT = 0x27; $SMTO_ABORTIFHUNG = 0x0002

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Output "SKIPPED 找不到可执行文件: $Exe"
    exit $EXIT_SKIPPED
}

# ── 准备素材 ──────────────────────────────────────────────────────────────
if (-not $Folder) {
    $Folder = Join-Path $env:TEMP "YeImageViewer-paging-fixture"
    New-Item -ItemType Directory -Force -Path $Folder | Out-Null
    if (-not (Get-Command magick -ErrorAction SilentlyContinue)) {
        Write-Output "SKIPPED 未提供 -Folder，且未安装 ImageMagick，无法生成测试素材"
        exit $EXIT_SKIPPED
    }
    # 4000x4000 16 位 PNG：文件很小但要 inflate 约 96 MB，单张解码约 250 ms
    1..6 | ForEach-Object {
        $f = Join-Path $Folder ("big{0:D2}.png" -f $_)
        if (-not (Test-Path -LiteralPath $f)) {
            magick -size 4000x4000 "gradient:hsl($($_*40),80%,50%)-hsl($($_*40+120),80%,30%)" `
                -depth 16 -define png:color-type=2 $f
        }
    }
}

$images = @(Get-ChildItem -LiteralPath $Folder -File | Where-Object { $_.Extension -match '\.(png|jpg|jpeg|tif|tiff|bmp|webp)$' })
if ($images.Count -lt 2) {
    Write-Output "SKIPPED $Folder 至少需要 2 张图片"
    exit $EXIT_SKIPPED
}

# ── 启动并等待窗口 ────────────────────────────────────────────────────────
$proc = Start-Process -FilePath $Exe -ArgumentList "`"$($images[0].FullName)`"" -PassThru
try {
    $hwnd = [IntPtr]::Zero
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 20 -and $hwnd -eq [IntPtr]::Zero) {
        $found = [PagingProbe]::FindMainWindow([uint32]$proc.Id)
        if ($found -ne [IntPtr]::Zero) { $hwnd = $found }
        else { Start-Sleep -Milliseconds 50 }
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        Write-Output "FAIL 未能取得主窗口"
        exit 1
    }
    Start-Sleep -Seconds 3   # 让首图解码和邻图预读安顿下来

    # ── 连续翻页并探测主线程 ──────────────────────────────────────────────
    Write-Output "素材: $Folder  ($($images.Count) 张)"
    Write-Output ""
    Write-Output "  翻页   主线程响应(ms)   无响应"
    Write-Output "  ----   -------------   ------"

    $latencies = @()
    $hangs = 0
    for ($i = 1; $i -le $Pages; $i++) {
        [void][PagingProbe]::PostMessage($hwnd, $WM_KEYDOWN, [IntPtr]$VK_RIGHT, [IntPtr]0)
        [void][PagingProbe]::PostMessage($hwnd, $WM_KEYUP,   [IntPtr]$VK_RIGHT, [IntPtr]0)

        $t = [System.Diagnostics.Stopwatch]::StartNew()
        [UIntPtr]$res = [UIntPtr]::Zero
        $r = [PagingProbe]::SendMessageTimeout($hwnd, $WM_NULL, [IntPtr]0, [IntPtr]0,
            $SMTO_ABORTIFHUNG, 3000, [ref]$res)
        $t.Stop()

        $ms = $t.Elapsed.TotalMilliseconds
        $hung = ($r -eq [IntPtr]::Zero)
        if ($hung) { $hangs++ }
        $latencies += $ms
        Write-Output ("  {0,4}   {1,13:F1}   {2}" -f $i, $ms, $(if ($hung) { "是" } else { "否" }))
        Start-Sleep -Milliseconds 60   # 模拟连续快速翻页
    }
}
finally {
    if ($proc -and -not $proc.HasExited) { $proc.Kill() }
}

# ── 判定 ──────────────────────────────────────────────────────────────────
$sorted = @($latencies | Sort-Object)
$median = $sorted[[int]($sorted.Count / 2)]
$max = ($latencies | Measure-Object -Maximum).Maximum

Write-Output ""
Write-Output ("  中位 {0:F1} ms   最大 {1:F1} ms   无响应 {2}/{3}" -f $median, $max, $hangs, $Pages)
Write-Output ""

$failed = $false
if ($hangs -gt 0) {
    Write-Output "FAIL 翻页期间主线程出现 $hangs 次无响应"
    $failed = $true
}
if ($median -gt $MedianLatencyMs) {
    Write-Output ("FAIL 主线程响应中位 {0:F1} ms 超过阈值 {1} ms —— 翻页仍在等解码" -f $median, $MedianLatencyMs)
    $failed = $true
}
if ($max -gt $MaxLatencyMs) {
    Write-Output ("FAIL 主线程响应最大 {0:F1} ms 超过阈值 {1} ms" -f $max, $MaxLatencyMs)
    $failed = $true
}

if ($failed) { exit 1 }
Write-Output "PASS 连续翻页期间主线程保持响应"
exit 0
