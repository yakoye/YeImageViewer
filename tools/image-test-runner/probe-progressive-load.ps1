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
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int index);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out int val, int size);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr h, int index, StringBuilder buf, int length, out int needed);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    // 有没有别的窗口压在被测窗口上面。CopyFromScreen 抓的是屏幕，别人盖上来就把
    // 读数搅乱了，而窗口句柄、窗口矩形一切正常，看不出任何异样。
    //
    // 不能用 WindowFromPoint：那是命中测试，而沉浸显示下图片以外那圈是半透明的
    // 压暗层，取样点会直接穿透到桌面，每轮都报「被遮挡」。Z 序相交不看透明。
    public static int CountOverlapping(IntPtr hwnd, RECT r) {
        int count = 0;
        IntPtr cur = hwnd;
        // GW_HWNDPREV = 3：Z 序里排在前面的，也就是画在上面的
        while ((cur = GetWindow(cur, 3)) != IntPtr.Zero) {
            if (!IsWindowVisible(cur) || IsIconic(cur)) continue;
            // 输入法候选条、浮动提示这类工具窗口不算遮挡
            if ((GetWindowLong(cur, -20) & 0x00000080) != 0) continue;
            // UWP 的后台窗口报「可见」但 DWM 根本没画它。DWMWA_CLOAKED = 14
            int cloaked;
            if (DwmGetWindowAttribute(cur, 14, out cloaked, 4) == 0 && cloaked != 0) continue;
            RECT o;
            if (!GetWindowRect(cur, out o)) continue;
            if (o.R - o.L <= 0 || o.B - o.T <= 0) continue;
            if (o.L < r.R && r.L < o.R && o.T < r.B && r.T < o.B) count++;
        }
        return count;
    }

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

# 远程桌面客户端最小化或断开时，输入桌面仍是 Default，但屏幕根本抓不下来
# （CopyFromScreen 报句柄无效）。同样与被测行为无关，记为未执行而不是失败。
try {
    $trialBitmap = New-Object System.Drawing.Bitmap 1, 1
    $trialGraphics = [System.Drawing.Graphics]::FromImage($trialBitmap)
    $trialGraphics.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size 1, 1))
    $trialGraphics.Dispose()
    $trialBitmap.Dispose()
}
catch {
    Write-Output "SKIPPED 无法读取屏幕像素（远程桌面最小化或已断开）"
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
$occludedFrames = 0

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
                # 读屏幕就得保证读到的是它。这台机器上有程序会自己弹窗口到 Z 序
                # 上面（实测 Shazao.MainWindow 一直压着，还在移动），盖住之后亮度
                # 和非暗像素会突变，判出「换图时画面会跳变」这种假失败。
                # HWND_TOPMOST(-1)，SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE：
                # 只改 Z 序，不动位置尺寸、不抢焦点，解码时序和渲染路径都不受影响。
                [void][LoadProbe]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x0013)
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
                # 这一帧读的时候窗口被别人盖住了没有。盖住了就整轮作废——
                # 被遮挡的那几帧混在里面，切换点会落在错的地方。
                if ([LoadProbe]::CountOverlapping($hwnd, $r) -gt 0) { $occludedFrames++ }
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

# 采样期间被别的窗口盖过，这一轮的像素读数就不可信了。这和锁屏、远程桌面断开
# 是同一类事：与被测行为无关的环境干扰，记未执行而不是失败。实测被盖住那一轮
# 的非暗像素从 66% 掉到 20%、亮度从 64 掉到 32，判出「画面会跳变」，而同一个
# 构建单独连跑三轮漂移都只有 0.1。
# 窗口已经置顶了，还能被盖住就只剩「别的 topmost 窗口压着」这一种可能。
# 那是真的环境干扰，和被测行为无关，记未执行而不是失败。
if ($occludedFrames -gt 0) {
    Write-Output ("SKIPPED 窗口已置顶，采样期间仍有 $occludedFrames 帧被其他置顶窗口遮挡，" +
        "屏幕读数不可信")
    exit $EXIT_SKIPPED
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
