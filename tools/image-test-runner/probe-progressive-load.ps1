# 大图渐进加载回归测试
#
# 打开一张解码要数秒的大图时，要求：
#   1. 窗口立刻出现，不等解码
#   2. 短时间内出现模糊预览（取自 Windows 缩略图服务），而不是空白
#   3. 解码完成后换成清晰原图，且取景与预览一致（不能突然铺开或缩小）
#
# 判定靠三个指标，按固定间隔采样窗口画面得出：
#   亮度        —— 空白背景很暗，有内容则显著变亮
#   梯度        —— 相邻像素平均绝对差。模糊预览被放大后高频能量低，原图高
#   非暗像素%   —— 画面中有内容的比例，用来比较预览与原图的取景是否一致
#
# 注意：沉浸预览模式下窗口背景是透明的，没有内容时 CopyFromScreen 抓到的是背后的桌面，
# 因此「画面有内容」这一项在透明背景下会虚过。真正起判定作用的是切换点前后的取景漂移——
# 没有预览时画面会从「透明/空白」直接跳成图片，漂移极大。
# 实测旧版（无预览）漂移 36.7，本版 0.0。
#
# 实测（moon_81M.png，290 MB / 9000x9000 / 16 位）：
#   窗口 0.21s   预览 <0.9s   原图 4.3s   梯度 2.84 -> 3.05   非暗像素 57.3% -> 57.1%
#
# 用法:
#   ./probe-progressive-load.ps1 -Image D:\path\to\huge.png

param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\x64\Release\YeImageViewer.exe"),
    [string]$Image = (Join-Path $PSScriptRoot "..\..\test\bigimage\moon_81M.png"),
    [double]$Duration = 8.0,
    # 判定阈值
    [double]$MaxWindowSeconds = 1.5,    # 窗口必须在此之前出现
    [double]$MaxPreviewSeconds = 2.5,   # 画面必须在此之前有内容
    [double]$MinContentPercent = 15.0,  # 「有内容」的判定：非暗像素占比
    [double]$MaxFramingDrift = 6.0      # 预览与原图的非暗像素占比之差上限
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class LoadProbe {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr h, int index, StringBuilder buf, int length, out int needed);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    // 锁屏或 UAC 安全桌面显示时，输入桌面是 Winlogon，普通进程打不开；
    // 只有能打开且名字是 Default 时，屏幕上显示的才是用户桌面。
    public static string InputDesktopName() {
        IntPtr h = OpenInputDesktop(0, false, 0x0001);
        if (h == IntPtr.Zero) return "";
        var name = new StringBuilder(256);
        int needed;
        GetUserObjectInformation(h, 2, name, name.Capacity * 2, out needed);
        CloseDesktop(h);
        return name.ToString();
    }
}
"@

# 退出码约定：0 通过，1 失败，3 未执行。
# 「未执行」必须和「通过」分开——锁屏时本测试读不到像素，若退 0，
# 发布闸门就会把一条阻断项当成通过放行。
$EXIT_SKIPPED = 3

foreach ($p in @($Exe, $Image)) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Output "SKIPPED 缺少 $p"
        exit $EXIT_SKIPPED
    }
}

# 本测试靠读屏幕像素判定。锁屏时 CopyFromScreen 抓到的是锁屏界面，
# 指标全是噪声——那时报 FAIL 与被测行为无关，只会掩盖真实回归。
# 判定看输入桌面而不是 LogonUI 进程：有的机器解锁后 LogonUI.exe 仍常驻，
# 按进程判会把已解锁误报成锁定，这条阻断项就永远跑不起来。
if ([LoadProbe]::InputDesktopName() -ne "Default") {
    Write-Output "SKIPPED 屏幕已锁定，无法读取窗口像素"
    exit $EXIT_SKIPPED
}

function Measure-Frame($bmp) {
    $w = $bmp.Width; $h = $bmp.Height
    $data = $bmp.LockBits(
        (New-Object System.Drawing.Rectangle 0, 0, $w, $h),
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $data.Stride
    $bytes = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)

    [long]$lumSum = 0; [long]$gradSum = 0; [long]$n = 0; [long]$nonDark = 0
    for ($y = 0; $y -lt $h; $y += 6) {
        $row = $y * $stride
        for ($x = 0; $x -lt ($w - 8); $x += 6) {
            $i = $row + $x * 4
            $lum = ($bytes[$i] + $bytes[$i + 1] + $bytes[$i + 2]) / 3
            $j = $i + 24
            $lum2 = ($bytes[$j] + $bytes[$j + 1] + $bytes[$j + 2]) / 3
            $lumSum += $lum
            $gradSum += [Math]::Abs($lum - $lum2)
            if ($lum -gt 28) { $nonDark++ }
            $n++
        }
    }
    if ($n -eq 0) { return $null }
    [pscustomobject]@{
        Lum     = [math]::Round($lumSum / $n, 1)
        Grad    = [math]::Round($gradSum / $n, 2)
        NonDark = [math]::Round(100.0 * $nonDark / $n, 1)
    }
}

Write-Output ("图片: {0}  ({1:F1} MB)" -f $Image, ((Get-Item -LiteralPath $Image).Length / 1MB))
Write-Output ""

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$proc = Start-Process -FilePath $Exe -ArgumentList "`"$Image`"" -PassThru
$hwnd = [IntPtr]::Zero
$windowAt = $null
$contentAt = $null
$samples = @()

try {
    while ($sw.Elapsed.TotalSeconds -lt $Duration) {
        if ($hwnd -eq [IntPtr]::Zero) {
            $fg = [LoadProbe]::GetForegroundWindow()
            [uint32]$owner = 0
            [void][LoadProbe]::GetWindowThreadProcessId($fg, [ref]$owner)
            if ($owner -eq $proc.Id -and [LoadProbe]::IsWindowVisible($fg)) {
                $hwnd = $fg
                $windowAt = $sw.Elapsed.TotalSeconds
                Write-Output ("窗口出现  t={0:F2}s" -f $windowAt)
                Write-Output ""
                Write-Output "   t(s)    亮度   梯度(清晰度)  非暗像素%"
                Write-Output "   -----  ------  ------------  ---------"
            }
            else { Start-Sleep -Milliseconds 20 }
            continue
        }

        $r = New-Object LoadProbe+RECT
        if ([LoadProbe]::GetWindowRect($hwnd, [ref]$r)) {
            $w = $r.R - $r.L; $h = $r.B - $r.T
            if ($w -gt 16 -and $h -gt 16) {
                $bmp = New-Object System.Drawing.Bitmap $w, $h
                $g = [System.Drawing.Graphics]::FromImage($bmp)
                $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $h))
                $g.Dispose()
                $m = Measure-Frame $bmp
                $bmp.Dispose()
                if ($m) {
                    $t = $sw.Elapsed.TotalSeconds
                    $samples += [pscustomobject]@{
                        T = $t; Lum = $m.Lum; Grad = $m.Grad; NonDark = $m.NonDark
                        Rect = "$($r.L),$($r.T),$($r.R),$($r.B)"
                    }
                    if (-not $contentAt -and $m.NonDark -ge $MinContentPercent) { $contentAt = $t }
                    Write-Output ("  {0,6:F2}  {1,6:F1}  {2,12:F2}  {3,9:F1}" -f $t, $m.Lum, $m.Grad, $m.NonDark)
                }
            }
        }
        Start-Sleep -Milliseconds 120
    }
}
finally {
    if ($proc -and -not $proc.HasExited) { $proc.Kill() }
}

Write-Output ""

# ── 判定 ──────────────────────────────────────────────────────────────────
$failed = $false

if (-not $windowAt) {
    Write-Output "FAIL 窗口始终未出现"
    exit 1
}
Write-Output ("窗口出现      {0:F2}s  (阈值 {1}s)" -f $windowAt, $MaxWindowSeconds)
if ($windowAt -gt $MaxWindowSeconds) {
    Write-Output "FAIL 窗口出现太慢 —— 打开图片仍在等解码"
    $failed = $true
}

if (-not $contentAt) {
    Write-Output "FAIL 整个观察期内画面都没有内容 —— 模糊预览没生效"
    exit 1
}
Write-Output ("画面有内容    {0:F2}s  (阈值 {1}s)" -f $contentAt, $MaxPreviewSeconds)
if ($contentAt -gt $MaxPreviewSeconds) {
    Write-Output "FAIL 模糊预览出现太慢"
    $failed = $true
}

# 窗口刚出现的头几帧还没画完，CopyFromScreen 会抓到底下的桌面，丢弃
$stable = @($samples | Where-Object { $_.T -ge ($windowAt + 0.6) })
if ($stable.Count -lt 3) {
    Write-Output "FAIL 有效采样不足，无法判定预览到原图的切换"
    exit 1
}

# 预览换成原图时清晰度会阶跃。找相邻两帧梯度变化最大的那一处当作切换点，
# 再看这一处前后的取景（非暗像素占比）有没有跟着跳——跳了就说明画面会突然铺开或缩小。
$switchIdx = -1
$maxDelta = 0.0
for ($i = 1; $i -lt $stable.Count; $i++) {
    $d = [Math]::Abs($stable[$i].Grad - $stable[$i - 1].Grad)
    if ($d -gt $maxDelta) { $maxDelta = $d; $switchIdx = $i }
}

if ($switchIdx -lt 0 -or $maxDelta -lt 0.05) {
    Write-Output ("梯度(清晰度)  {0:F2} -> {1:F2}  （未观察到切换，可能解码早于首次采样）" -f `
        $stable[0].Grad, $stable[-1].Grad)
}
else {
    $before = $stable[$switchIdx - 1]
    $after = $stable[$switchIdx]
    Write-Output ("切换点        t={0:F2}s" -f $after.T)
    Write-Output ("梯度(清晰度)  {0:F2} -> {1:F2}" -f $before.Grad, $after.Grad)

    $drift = [Math]::Abs($after.NonDark - $before.NonDark)
    Write-Output ("非暗像素%     {0:F1} -> {1:F1}  (漂移 {2:F1}，阈值 {3})" -f `
        $before.NonDark, $after.NonDark, $drift, $MaxFramingDrift)
    if ($drift -gt $MaxFramingDrift -and $before.Rect -eq $after.Rect) {
        Write-Output "FAIL 预览与原图取景不一致 —— 换图时画面会跳变"
        $failed = $true
    }
}

Write-Output ""
if ($failed) { exit 1 }
Write-Output "PASS 窗口立刻出现 + 模糊预览 + 渐进换清晰原图"
exit 0
