<#
.SYNOPSIS
    按「双击图片 → 看到清晰的完整图」这把尺子，横向对比几款看图软件。

.DESCRIPTION
    和 compare-viewers.ps1 不同的地方只有一处，但很关键：判定的是「最后一次画面变化」，
    而不是「画面第一次稳下来」。

    为什么要分开量。YeImageViewer 对特大图是渐进加载：先秒出一张缩略预览，
    再在后台解完原图换上去。预览本身是静止画面，按「连续几帧不变」判定会在预览那一刻
    就宣布加载完成——数字很好看，可用户盯着的还是一张糊图。JarkViewer 则相反，
    解完之前窗口一片空白，第一次画出来的就是清晰图。两种策略用同一把尺子量才有意义，
    所以这里一直看到画面彻底不动（连续 QuietMs 没有任何变化）为止，
    把最后一次变化的时刻当作「清晰图到位」。

    每张图给出三个时间（都从进程启动算起）：

      窗口出现    用户第一次看到这个程序的窗口。
      首帧画面    窗口里第一次画出和刚出现时不同的东西（预览或图片，取决于软件）。
      清晰图      画面最后一次变化，之后 QuietMs 内纹丝不动。这才是「加载完了」。

    另外记下首帧和末帧的「细节量」（相邻像素灰度差的均值）。预览是放大的糊图，
    细节量明显低于原图；两个数字拉开差距，就证明末尾那次变化确实是「变清晰」，
    而不是某个控件闪了一下。

    公平性：
      - 每张图在测之前先整份读一遍，把 OS 文件缓存喂热，谁也不占冷热的便宜；
      - 每轮全新进程，测完立刻关掉；
      - 取中位数，不取平均。

    直接启动 exe 而不是真的在资源管理器里双击：双击多出来的是 shell 解析关联的开销，
    这部分对所有软件都一样，摊在谁身上都不影响相对结果，而直接启动能精确地在
    进程创建那一刻起表。

.EXAMPLE
    ./measure-open-to-sharp.ps1 -ImageDir "D:\work\code\code_o\YeImageViewer\test\bigimage"
    ./measure-open-to-sharp.ps1 -ImageDir "...\07-extreme-png\02_Real_100MP_Plus" -Repeats 3
#>
param(
    [string]$ImageDir,
    [string[]]$Image = @(),
    [string[]]$Viewer = @(
        (Join-Path $PSScriptRoot "..\..\x64\Release\YeImageViewer.exe"),
        "D:\software\JarkViewer.exe",
        "C:\Program Files\GuoheView\GuoheView.exe",
        "C:\Program Files\ImageGlass\ImageGlass.exe",
        "C:\Program Files\2345Soft\2345Pic\2345PicViewer.exe"
    ),
    [int]$Repeats = 3,
    [int]$QuietMs = 2500,
    [double]$MinWatchSeconds = 5.0,
    [double]$TimeoutSeconds = 90.0,
    [string]$OutputCsv = (Join-Path $PSScriptRoot "..\..\artifacts\release-gate\open-to-sharp.csv")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class SharpProbe {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr param);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint to, uint from, bool attach);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr h, int index, StringBuilder buf, int len, out int needed);

    public delegate bool EnumProc(IntPtr hWnd, IntPtr param);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    public static string InputDesktopName() {
        IntPtr h = OpenInputDesktop(0, false, 0x0001);
        if (h == IntPtr.Zero) return "";
        var name = new StringBuilder(256);
        int needed;
        GetUserObjectInformation(h, 2, name, name.Capacity * 2, out needed);
        CloseDesktop(h);
        return name.ToString();
    }

    public static string Title(IntPtr window) {
        var buffer = new StringBuilder(512);
        GetWindowTextW(window, buffer, buffer.Capacity);
        return buffer.ToString();
    }
    public static string ClassName(IntPtr window) {
        var buffer = new StringBuilder(256);
        GetClassNameW(window, buffer, buffer.Capacity);
        return buffer.ToString();
    }

    // 够大的可见顶层窗口。按窗口而不是按进程找：有的软件（2345看图王）由一个
    // 启动器拉起另一个进程来显示，盯着被启动那个进程名会永远等不到窗口。
    public static IntPtr[] VisibleWindows(int minWidth, int minHeight) {
        var found = new List<IntPtr>();
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            if (!IsWindowVisible(h) || IsIconic(h)) return true;
            RECT r;
            if (!GetWindowRect(h, out r)) return true;
            if (r.R - r.L < minWidth || r.B - r.T < minHeight) return true;
            found.Add(h);
            return true;
        }, IntPtr.Zero);
        return found.ToArray();
    }

    public static uint ProcessOf(IntPtr window) {
        uint pid;
        GetWindowThreadProcessId(window, out pid);
        return pid;
    }

    public static bool ForceForeground(IntPtr window) {
        if (GetForegroundWindow() == window) return true;
        uint target;
        uint foreground = GetWindowThreadProcessId(GetForegroundWindow(), out target);
        uint current = GetCurrentThreadId();
        bool attached = foreground != current && AttachThreadInput(foreground, current, true);
        ShowWindow(window, 5);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        if (attached) AttachThreadInput(foreground, current, false);
        return GetForegroundWindow() == window;
    }

    public static void ParkCursor(int x, int y) { SetCursorPos(x, y); }

    // FNV-1a，放 C# 里算：PowerShell 的 * 会把 uint64 提升成 double 而溢出。
    public static ulong HashBytes(byte[] data, int step) {
        unchecked {
            ulong hash = 14695981039346656037UL;
            for (int i = 0; i < data.Length; i += step) {
                hash ^= data[i];
                hash *= 1099511628211UL;
            }
            return hash;
        }
    }

    // 细节量：横向相邻像素的灰度差均值。糊图（预览放大）低，原图高。
    // 用它把「画面变了」和「画面变清晰了」区分开。
    public static double Detail(byte[] data, int stride, int width, int height) {
        long sum = 0;
        long count = 0;
        for (int y = 0; y < height; y++) {
            int row = y * stride;
            for (int x = 4; x < width * 4; x += 4) {
                int diff = data[row + x] - data[row + x - 4];
                sum += diff < 0 ? -diff : diff;
                count++;
            }
        }
        return count == 0 ? 0.0 : (double)sum / count;
    }
}
"@

$EXIT_SKIPPED = 3

if ([SharpProbe]::InputDesktopName() -ne "Default") {
    Write-Output "SKIPPED 屏幕已锁定，读不到窗口像素"
    exit $EXIT_SKIPPED
}
try {
    $trialBitmap = New-Object System.Drawing.Bitmap 1, 1
    $trialGraphics = [System.Drawing.Graphics]::FromImage($trialBitmap)
    $trialGraphics.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size 1, 1))
    $trialGraphics.Dispose(); $trialBitmap.Dispose()
}
catch {
    Write-Output "SKIPPED 读不到屏幕像素（远程桌面最小化或已断开）"
    exit $EXIT_SKIPPED
}

# ---- 待测图片 ----------------------------------------------------------------
$targets = New-Object Collections.ArrayList
foreach ($path in $Image) {
    if (Test-Path -LiteralPath $path -PathType Leaf) { [void]$targets.Add((Get-Item -LiteralPath $path)) }
}
if ($ImageDir) {
    if (-not (Test-Path -LiteralPath $ImageDir -PathType Container)) {
        Write-Output "SKIPPED 图片目录不存在：$ImageDir"
        exit $EXIT_SKIPPED
    }
    Get-ChildItem -LiteralPath $ImageDir -File |
        Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|webp|bmp|tif|tiff|avif|heic|jxl)$' } |
        Sort-Object Name | ForEach-Object { [void]$targets.Add($_) }
}
if ($targets.Count -eq 0) {
    Write-Output "SKIPPED 没有可测的图片（-ImageDir / -Image 都为空）"
    exit $EXIT_SKIPPED
}

$viewers = @()
foreach ($path in $Viewer) {
    $full = $path
    try { $full = (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path } catch { }
    if (Test-Path -LiteralPath $full -PathType Leaf) { $viewers += $full }
    else { Write-Output "跳过（找不到程序）：$path" }
}
if ($viewers.Count -eq 0) {
    Write-Output "SKIPPED 一个待测程序都没找到"
    exit $EXIT_SKIPPED
}

# ---- 采样 --------------------------------------------------------------------
$sampleWidth = 320
$sampleHeight = 200
$sampleBitmap = New-Object System.Drawing.Bitmap $sampleWidth, $sampleHeight
$sampleGraphics = [System.Drawing.Graphics]::FromImage($sampleBitmap)
$sampleRect = New-Object System.Drawing.Rectangle 0, 0, $sampleWidth, $sampleHeight
$sampleSize = New-Object System.Drawing.Size $sampleWidth, $sampleHeight

function Get-Sample([IntPtr]$Window) {
    $rect = New-Object SharpProbe+RECT
    if (-not [SharpProbe]::GetWindowRect($Window, [ref]$rect)) { return $null }
    $width = $rect.R - $rect.L
    $height = $rect.B - $rect.T
    if ($width -le 0 -or $height -le 0) { return $null }
    $x = $rect.L + [int](($width - $sampleWidth) / 2)
    $y = $rect.T + [int](($height - $sampleHeight) / 2)
    try {
        $sampleGraphics.CopyFromScreen($x, $y, 0, 0, $sampleSize)
    }
    catch { return $null }
    $data = $sampleBitmap.LockBits($sampleRect,
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
    try {
        $bytes = New-Object byte[] ($data.Stride * $sampleHeight)
        [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        $stride = $data.Stride
    }
    finally { $sampleBitmap.UnlockBits($data) }
    # 每 4 字节取一个点（每像素一个），比 compare-viewers 密得多：
    # 预览换成原图时画面只是「变清晰」，稀疏采样很容易整帧撞上同一个哈希。
    return [pscustomobject]@{
        Hash = [SharpProbe]::HashBytes($bytes, 4)
        Detail = [SharpProbe]::Detail($bytes, $stride, $sampleWidth, $sampleHeight)
    }
}

function Stop-Viewer([string]$ExePath, [string[]]$Extra) {
    $names = @([IO.Path]::GetFileNameWithoutExtension($ExePath)) + $Extra
    foreach ($name in ($names | Where-Object { $_ } | Select-Object -Unique)) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object {
            try {
                [void]$_.CloseMainWindow()
                if (-not $_.WaitForExit(1200)) { $_.Kill(); [void]$_.WaitForExit(1200) }
            }
            catch { }
        }
    }
    Start-Sleep -Milliseconds 300
}

$ignoreProcesses = @("explorer", "powershell", "pwsh", "WindowsTerminal", "conhost",
    "Claude", "ApplicationFrameHost", "TextInputHost", "ShellExperienceHost",
    "SearchHost", "StartMenuExperienceHost", "Code", "devenv")

function Wait-NewWindow {
    param([hashtable]$Known, [Diagnostics.Stopwatch]$Clock, [double]$TimeoutSeconds)
    while ($Clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        foreach ($window in [SharpProbe]::VisibleWindows(360, 240)) {
            if ($Known.ContainsKey([int64]$window)) { continue }
            # 不能叫 $pid：那是 PowerShell 的自动变量（当前进程号），赋值会炸
            $ownerPid = [SharpProbe]::ProcessOf($window)
            $process = Get-Process -Id $ownerPid -ErrorAction SilentlyContinue
            if ($null -eq $process) { continue }
            if ($ignoreProcesses -contains $process.ProcessName) { continue }
            return [pscustomobject]@{
                Window = $window
                Elapsed = $Clock.Elapsed.TotalMilliseconds
                Process = $process.ProcessName
            }
        }
        Start-Sleep -Milliseconds 4
    }
    return $null
}

# ---- 一次测量 ----------------------------------------------------------------
function Measure-OpenToSharp {
    param([string]$ExePath, [string]$ImagePath)

    $known = @{}
    foreach ($window in [SharpProbe]::VisibleWindows(360, 240)) { $known[[int64]$window] = $true }
    [SharpProbe]::ParkCursor(4, 4)

    $clock = [Diagnostics.Stopwatch]::StartNew()
    try { $null = Start-Process -FilePath $ExePath -ArgumentList ('"' + $ImagePath + '"') -PassThru }
    catch { return $null }

    $appeared = Wait-NewWindow -Known $known -Clock $clock -TimeoutSeconds $TimeoutSeconds
    if ($null -eq $appeared) { return $null }
    [void][SharpProbe]::ForceForeground($appeared.Window)

    $window = $appeared.Window
    $lastHash = $null
    $firstChangeMs = $null
    $firstDetail = $null
    # 首帧可能只是刷了个白底（JarkViewer 就是这样），那不叫「看到图了」。
    # 细节量迈过门槛才算画面里真有东西。
    $contentMs = $null
    $lastChangeMs = $null
    $lastDetail = $null
    $changes = 0

    # 收工条件必须同时满足三件事，缺一个就会量出假数据：
    #   至少看到过一次画面变化 —— 否则 JarkViewer 那种「先开一个空白窗口」会被记成
    #                             「窗口一出来就加载完了」，细节量 0 的空白也算通过；
    #   安静期够长           —— 渐进加载的预览本身是静止画面，安静期短于解码时间
    #                             就会在预览那一刻收工，量到的是糊图不是清晰图；
    #   总观察时间够长       —— 兜底，防止某个软件在极早期抖一下就骗过前两条。
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $now = $clock.Elapsed.TotalMilliseconds
        if ([SharpProbe]::GetForegroundWindow() -ne $window) {
            [void][SharpProbe]::ForceForeground($window)
        }
        $sample = Get-Sample $window
        if ($null -ne $sample) {
            if ($null -eq $lastHash) {
                $lastHash = $sample.Hash
                $lastDetail = $sample.Detail
            }
            elseif ($sample.Hash -ne $lastHash) {
                $lastHash = $sample.Hash
                $lastDetail = $sample.Detail
                $lastChangeMs = $now
                $changes++
                if ($null -eq $firstChangeMs) {
                    $firstChangeMs = $now
                    $firstDetail = $sample.Detail
                }
                if ($null -eq $contentMs -and $sample.Detail -ge 0.05) { $contentMs = $now }
            }
        }
        $elapsed = $clock.Elapsed.TotalMilliseconds
        if ($changes -ge 1 -and
            ($elapsed - $lastChangeMs) -gt $QuietMs -and
            $elapsed -gt ($MinWatchSeconds * 1000.0)) { break }
        Start-Sleep -Milliseconds 3
    }

    # 一次都没变过有两种情况，必须分开处理：
    #   画面是空的（细节量趋近 0）——窗口开了但始终没画出东西，这一轮作废，
    #                                宁可记空，也不能记成「窗口一出来就加载完了」；
    #   画面有内容                ——窗口是画好才显示的，那窗口出现的时刻就是加载完成的时刻。
    if ($changes -lt 1) {
        if ($null -eq $lastDetail -or $lastDetail -lt 0.05) { return $null }
        $firstChangeMs = $appeared.Elapsed
        $lastChangeMs = $appeared.Elapsed
        $firstDetail = $lastDetail
        $contentMs = $appeared.Elapsed
    }
    $timedOut = $clock.Elapsed.TotalSeconds -ge $TimeoutSeconds
    return [pscustomobject]@{
        WindowMs = [math]::Round($appeared.Elapsed, 1)
        FirstPaintMs = [math]::Round($firstChangeMs, 1)
        ContentMs = if ($null -ne $contentMs) { [math]::Round($contentMs, 1) } else { $null }
        SharpMs = if ($timedOut) { $null } else { [math]::Round($lastChangeMs, 1) }
        FirstDetail = [math]::Round($firstDetail, 2)
        FinalDetail = if ($null -ne $lastDetail) { [math]::Round($lastDetail, 2) } else { $null }
        Changes = $changes
        Process = $appeared.Process
        Title = [SharpProbe]::Title($window)
    }
}

function Get-Median([object]$Values) {
    $list = @($Values | Where-Object { $null -ne $_ } | Sort-Object)
    if ($list.Count -eq 0) { return $null }
    if ($list.Count % 2 -eq 1) { return [math]::Round($list[[int](($list.Count - 1) / 2)], 1) }
    return [math]::Round(($list[$list.Count / 2 - 1] + $list[$list.Count / 2]) / 2.0, 1)
}

# ---- 跑 ----------------------------------------------------------------------
Write-Output "双击到清晰图：$($viewers.Count) 个程序 × $($targets.Count) 张图 × $Repeats 轮"
Write-Output ""

$rows = New-Object Collections.ArrayList
foreach ($target in $targets) {
    $sizeMb = [math]::Round($target.Length / 1MB, 1)
    Write-Output ("=" * 78)
    Write-Output "$($target.Name)  ($sizeMb MB)"
    Write-Output ("=" * 78)

    # 先整份读一遍喂热文件缓存，谁都不占冷热的便宜
    try {
        $stream = [IO.File]::OpenRead($target.FullName)
        $buffer = New-Object byte[] (4MB)
        while ($stream.Read($buffer, 0, $buffer.Length) -gt 0) { }
        $stream.Dispose()
    }
    catch { }

    foreach ($exe in $viewers) {
        $label = [IO.Path]::GetFileNameWithoutExtension($exe)
        $extraNames = @()
        $window = @(); $first = @(); $content = @(); $sharp = @()
        $detailFirst = $null; $detailFinal = $null; $title = ""; $failures = 0
        $changeCount = $null

        # 预热一轮不计数：首次启动要吃磁盘和杀软的开销
        $warm = Measure-OpenToSharp -ExePath $exe -ImagePath $target.FullName
        if ($null -ne $warm) { $extraNames += $warm.Process }
        Stop-Viewer $exe $extraNames

        for ($round = 0; $round -lt $Repeats; $round++) {
            $result = Measure-OpenToSharp -ExePath $exe -ImagePath $target.FullName
            if ($null -eq $result) { $failures++ }
            else {
                $extraNames += $result.Process
                $window += $result.WindowMs
                $first += $result.FirstPaintMs
                if ($null -ne $result.ContentMs) { $content += $result.ContentMs }
                if ($null -ne $result.SharpMs) { $sharp += $result.SharpMs }
                $detailFirst = $result.FirstDetail
                $detailFinal = $result.FinalDetail
                $changeCount = $result.Changes
                $title = $result.Title
            }
            Stop-Viewer $exe $extraNames
        }

        $row = [pscustomobject]@{
            图片 = $target.Name
            大小MB = $sizeMb
            程序 = $label
            窗口出现ms = Get-Median $window
            首帧画面ms = Get-Median $first
            出图ms = Get-Median $content
            清晰图ms = Get-Median $sharp
            首帧细节 = $detailFirst
            末帧细节 = $detailFinal
            画面变化次数 = $changeCount
            失败次数 = $failures
            窗口标题 = $title
        }
        [void]$rows.Add($row)

        $sharpText = if ($null -ne $row.清晰图ms) { "{0,8:N0}" -f $row.清晰图ms } else { "   超时" }
        $contentText = if ($null -ne $row.出图ms) { "{0,7:N0}" -f $row.出图ms } else { "   --" }
        Write-Output ("  {0,-16} 窗口 {1,6:N0}   首帧 {2,6:N0}   出图 {3}   清晰图 {4}  ms   细节 {5} → {6}" -f `
            $label, $row.窗口出现ms, $row.首帧画面ms, $contentText, $sharpText, $row.首帧细节, $row.末帧细节)
    }
    Write-Output ""
}

$outputDir = Split-Path -Parent $OutputCsv
if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}
$rows | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding UTF8
Write-Output "明细已写入 $OutputCsv"

$sampleGraphics.Dispose()
$sampleBitmap.Dispose()
exit 0
