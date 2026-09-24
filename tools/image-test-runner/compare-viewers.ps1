<#
.SYNOPSIS
    横向对比几款看图软件的「打开速度」和「左右切换速度」。

.DESCRIPTION
    所有软件用同一个图片目录、同一台机器、同一套判定方式，测两件事：

      打开速度  从进程启动到窗口出现，再到画面稳定（连续三帧像素不再变化）。
      切换速度  按一次「→」，到画面首次变化、再到画面稳定各用了多久。

    为什么靠读屏幕像素而不是读窗口标题：只有一部分软件把文件名写在标题里
    （JarkViewer、GuoheView、YeImageViewer 写了，ImageGlass 和 2345看图王 没写），
    靠标题就没法把五个软件放在同一把尺子上量。像素判定对所有软件一视同仁，
    量到的也正是用户真正看到画面的那一刻。

    公平性上做了这些：
      - 每个软件先热一遍再正式测，避开首次启动的磁盘与杀软开销；
      - 每轮都是全新进程，测完立刻关掉，不让常驻实例占便宜；
      - 按键统一用 SendInput 发给前台窗口，和真人按键走同一条路；
      - 切换取中位数而不是平均，避开个别卡顿把结果带偏。

    读屏幕像素要求会话没锁屏、远程桌面没最小化，否则记 SKIPPED（退出码 3），
    与其他探针一致——这种情况下的数字全是噪声，报出来只会误导。

.EXAMPLE
    ./compare-viewers.ps1 -ImageDir "D:\photos"
    ./compare-viewers.ps1 -ImageDir "...\01_Real_PNG_50-70MB" -Switches 10
#>
param(
    [string]$ImageDir = (Join-Path $PSScriptRoot "..\..\test\format corpus\files"),
    [int]$Switches = 20,
    [int]$Repeats = 3,
    [string[]]$Viewer = @(
        (Join-Path $PSScriptRoot "..\..\x64\Release\YeImageViewer.exe"),
        "D:\software\JarkViewer.exe",
        "C:\Program Files\GuoheView\GuoheView.exe",
        "C:\Program Files\ImageGlass\ImageGlass.exe",
        "C:\Program Files\2345Soft\2345Pic\2345PicViewer.exe"
    ),
    [string]$OutputCsv = (Join-Path $PSScriptRoot "..\..\artifacts\release-gate\viewer-comparison.csv"),
    [double]$OpenTimeoutSeconds = 20.0,
    [double]$SwitchTimeoutSeconds = 8.0
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class ViewerCompare {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr param);
    [DllImport("user32.dll")] public static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetUserObjectInformation(IntPtr h, int index, StringBuilder buf, int length, out int needed);

    public delegate bool EnumProc(IntPtr hWnd, IntPtr param);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
        public ushort vk; public ushort scan; public uint flags; public uint time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
        public int dx; public int dy; public uint data; public uint flags; public uint time; public IntPtr extra; }
    // 键盘和鼠标事件共用同一个 INPUT，这里按最大的那个（鼠标）铺够字节，
    // 两个成员起始偏移相同，各自只写自己那几项。
    [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION {
        [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public MOUSEINPUT mi; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT {
        public uint type; public INPUTUNION u; }

    // 锁屏或 UAC 安全桌面时输入桌面不是 Default，屏幕上根本不是用户画面。
    public static string InputDesktopName() {
        IntPtr h = OpenInputDesktop(0, false, 0x0001);
        if (h == IntPtr.Zero) return "";
        var name = new StringBuilder(256);
        int needed;
        GetUserObjectInformation(h, 2, name, name.Capacity * 2, out needed);
        CloseDesktop(h);
        return name.ToString();
    }

    // 取某个进程里的主窗口：优先「有标题的可见窗口里最大的那个」。
    // 只按面积挑会踩坑——2345看图王 的阴影层 RCShadowWindow 比真正的图片窗口还大，
    // 抓着它采样和发键，量出来的永远是「画面没变化」。有标题的一个都没有时才退回按面积挑。
    public static IntPtr MainWindow(uint processId) {
        IntPtr bestTitled = IntPtr.Zero, bestAny = IntPtr.Zero;
        long titledArea = 0, anyArea = 0;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint owner;
            GetWindowThreadProcessId(h, out owner);
            if (owner != processId || !IsWindowVisible(h) || IsIconic(h)) return true;
            RECT r;
            if (!GetWindowRect(h, out r)) return true;
            long area = (long)(r.R - r.L) * (r.B - r.T);
            if (area > anyArea) { anyArea = area; bestAny = h; }
            if (Title(h).Length > 0 && area > titledArea) { titledArea = area; bestTitled = h; }
            return true;
        }, IntPtr.Zero);
        return bestTitled != IntPtr.Zero ? bestTitled : bestAny;
    }

    // FNV-1a。放在 C# 里算：PowerShell 的 * 会把 uint64 提升成 double，
    // 一乘就溢出报错；顺带每帧少几十万次解释执行，采样间隔才压得下来。
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

    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint attachTo, uint attachFrom, bool attach);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();

    // Windows 的前台锁：后台进程直接 SetForegroundWindow 会被忽略（实测五个软件里
    // 有四个因此测不到切换）。先把输入队列挂到当前前台线程上，借它的身份置前，
    // 这是没有 UI 自动化框架时最稳的一招；还不行就点一下 Alt 解锁再试。
    public static bool ForceForeground(IntPtr window) {
        if (GetForegroundWindow() == window) return true;
        uint target;
        uint foreground = GetWindowThreadProcessId(GetForegroundWindow(), out target);
        uint current = GetCurrentThreadId();
        bool attached = foreground != current && AttachThreadInput(foreground, current, true);
        ShowWindow(window, 5);             // SW_SHOW
        BringWindowToTop(window);
        SetForegroundWindow(window);
        if (attached) AttachThreadInput(foreground, current, false);
        if (GetForegroundWindow() == window) return true;

        SendKey(0x12);                     // VK_MENU，解除前台锁
        SetForegroundWindow(window);
        return GetForegroundWindow() == window;
    }

    public static void SendKey(ushort virtualKey) {
        INPUT[] inputs = new INPUT[2];
        inputs[0].type = 1; inputs[0].u.ki.vk = virtualKey;
        inputs[1].type = 1; inputs[1].u.ki.vk = virtualKey; inputs[1].u.ki.flags = 2; // KEYEVENTF_KEYUP
        SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
    }

    [DllImport("user32.dll")] static extern IntPtr GetFocus();
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);

    // 有的软件（2345看图王）真正收键的是某个子窗口，SendInput 打到顶层窗口没反应。
    // 挂到它的线程上问出焦点窗口，把按键直接投给焦点窗口。
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);

    // 点一下窗口中央：顶层窗口在前台不等于图像控件拿到了键盘焦点。
    public static void ClickAt(int x, int y) {
        SetCursorPos(x, y);
        INPUT[] inputs = new INPUT[2];
        inputs[0].type = 0; inputs[0].u.mi.flags = 0x0002;   // MOUSEEVENTF_LEFTDOWN
        inputs[1].type = 0; inputs[1].u.mi.flags = 0x0004;   // MOUSEEVENTF_LEFTUP
        SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
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

    public static bool PostKeyToFocus(IntPtr window, ushort virtualKey) {
        uint pid;
        uint target = GetWindowThreadProcessId(window, out pid);
        uint me = GetCurrentThreadId();
        bool attached = target != me && AttachThreadInput(target, me, true);
        IntPtr focus = GetFocus();
        if (attached) AttachThreadInput(target, me, false);
        if (focus == IntPtr.Zero) focus = window;
        PostMessage(focus, 0x0100, (IntPtr)virtualKey, IntPtr.Zero);
        PostMessage(focus, 0x0101, (IntPtr)virtualKey, IntPtr.Zero);
        return true;
    }
}
"@

$EXIT_SKIPPED = 3
$VK_RIGHT = 0x27

if ([ViewerCompare]::InputDesktopName() -ne "Default") {
    Write-Output "SKIPPED 屏幕已锁定，无法读取窗口像素"
    exit $EXIT_SKIPPED
}
try {
    $trialBitmap = New-Object System.Drawing.Bitmap 1, 1
    $trialGraphics = [System.Drawing.Graphics]::FromImage($trialBitmap)
    $trialGraphics.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size 1, 1))
    $trialGraphics.Dispose(); $trialBitmap.Dispose()
}
catch {
    Write-Output "SKIPPED 无法读取屏幕像素（远程桌面最小化或已断开）"
    exit $EXIT_SKIPPED
}

# 按键要靠 SendInput 送给前台窗口。整个会话连一个前台窗口都没有时（远程桌面客户端
# 没连上、或刚好处在切换空档），键发出去没人收，量出来的全是「切换失败」。
$foregroundDeadline = [DateTime]::UtcNow.AddSeconds(5)
while ([ViewerCompare]::GetForegroundWindow() -eq [IntPtr]::Zero -and
    [DateTime]::UtcNow -lt $foregroundDeadline) {
    Start-Sleep -Milliseconds 200
}
if ([ViewerCompare]::GetForegroundWindow() -eq [IntPtr]::Zero) {
    Write-Output "SKIPPED 会话里没有前台窗口，按键送不出去（远程桌面未连接或已锁屏）"
    exit $EXIT_SKIPPED
}

if (-not (Test-Path -LiteralPath $ImageDir -PathType Container)) {
    Write-Output "SKIPPED 图片目录不存在：$ImageDir"
    exit $EXIT_SKIPPED
}
$images = Get-ChildItem -LiteralPath $ImageDir -File |
    Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|webp|bmp|tif|tiff)$' } |
    Sort-Object Name
if ($images.Count -lt 2) {
    Write-Output "SKIPPED 图片目录里至少要有两张图才能测切换：$ImageDir"
    exit $EXIT_SKIPPED
}
$firstImage = $images[0].FullName

# ---- 画面采样 ----------------------------------------------------------------
# 只抓窗口中央一小块并压成一个哈希：整窗逐帧抓在 PowerShell 里太慢，
# 中央区域足以判断「画面变了没有」，而采样越快，测出来的延迟分辨率越高。
$sampleWidth = 320
$sampleHeight = 200
$sampleBitmap = New-Object System.Drawing.Bitmap $sampleWidth, $sampleHeight
$sampleGraphics = [System.Drawing.Graphics]::FromImage($sampleBitmap)

function Get-FrameHash([IntPtr]$Window) {
    $rect = New-Object ViewerCompare+RECT
    if (-not [ViewerCompare]::GetWindowRect($Window, [ref]$rect)) { return $null }
    $width = $rect.R - $rect.L
    $height = $rect.B - $rect.T
    if ($width -le 0 -or $height -le 0) { return $null }
    $x = $rect.L + [int](($width - $sampleWidth) / 2)
    $y = $rect.T + [int](($height - $sampleHeight) / 2)
    try {
        $sampleGraphics.CopyFromScreen($x, $y, 0, 0,
            (New-Object System.Drawing.Size $sampleWidth, $sampleHeight))
    }
    catch { return $null }
    $data = $sampleBitmap.LockBits(
        (New-Object System.Drawing.Rectangle 0, 0, $sampleWidth, $sampleHeight),
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
    try {
        $bytes = New-Object byte[] ($data.Stride * $sampleHeight)
        [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    }
    finally { $sampleBitmap.UnlockBits($data) }
    # 每 37 字节取一个点：够敏感，又不至于每帧啃完整块像素
    return [ViewerCompare]::HashBytes($bytes, 37)
}

function Close-ViewerProcesses([string]$ExePath) {
    $name = [IO.Path]::GetFileNameWithoutExtension($ExePath)
    Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            [void]$_.CloseMainWindow()
            if (-not $_.WaitForExit(1500)) { $_.Kill(); [void]$_.WaitForExit(1500) }
        }
        catch { }
    }
    Start-Sleep -Milliseconds 250
}

# 等一个属于该程序的可见窗口出现。有的软件（如 2345看图王）会把文件交给已有实例，
# 启动的那个进程自己就退了，所以按进程名找，而不是只盯 Start-Process 返回的那个。
function Wait-ViewerWindow {
    param([string]$ExePath, [Diagnostics.Stopwatch]$Clock, [double]$TimeoutSeconds)
    $name = [IO.Path]::GetFileNameWithoutExtension($ExePath)
    while ($Clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        foreach ($process in (Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            $window = [ViewerCompare]::MainWindow([uint32]$process.Id)
            if ($window -ne [IntPtr]::Zero) {
                $rect = New-Object ViewerCompare+RECT
                [void][ViewerCompare]::GetWindowRect($window, [ref]$rect)
                if (($rect.R - $rect.L) -ge 320 -and ($rect.B - $rect.T) -ge 200) {
                    return [pscustomobject]@{ Window = $window; Elapsed = $Clock.Elapsed.TotalMilliseconds }
                }
            }
        }
        Start-Sleep -Milliseconds 5
    }
    return $null
}

# 画面稳定 = 连续三次采样哈希相同；返回的是这三次里第一次的时间点。
function Assert-Foreground([IntPtr]$Window) {
    # 关掉上一个软件之后会有一小段「整个会话没有前台窗口」的空档，
    # 这期间 GetForegroundWindow 返回 0、SetForegroundWindow 也不会成功，
    # 多试几次就过去了；一直不成才算真的置不到前台。
    for ($attempt = 0; $attempt -lt 12; $attempt++) {
        if ([ViewerCompare]::GetForegroundWindow() -eq $Window) { return $true }
        if ([ViewerCompare]::ForceForeground($Window)) { return $true }
        Start-Sleep -Milliseconds 50
    }
    return $false
}

# RequireChangeFrom：给「打开」用。窗口刚出现时画的往往还不是图片——JarkViewer 先画
# 启动页，ImageGlass 甚至弹的是模态框——这些画面本身是静止的，只按「连续三帧不变」
# 判定会把启动页当成图片，量出比真实解码快得多的假数据（实测 59 MB 的 PNG「171 ms 就稳了」）。
# 传入窗口刚出现那一帧的哈希，就必须先看到画面变化，才开始算稳定。
# GraceSeconds 内一直没变化，说明首帧画的就是图片，那就认它。
function Wait-FrameSettled {
    param(
        [IntPtr]$Window,
        [Diagnostics.Stopwatch]$Clock,
        [double]$TimeoutSeconds,
        [object]$RequireChangeFrom = $null,
        [double]$GraceSeconds = 2.0
    )
    $previous = $null
    $stableSince = $null
    $stableCount = 0
    $changeSeen = ($null -eq $RequireChangeFrom)
    $graceStart = $Clock.Elapsed.TotalSeconds
    while ($Clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $now = $Clock.Elapsed.TotalMilliseconds
        if (-not (Assert-Foreground $Window)) { Start-Sleep -Milliseconds 20; continue }
        $hash = Get-FrameHash $Window
        if (-not $changeSeen) {
            if ($null -ne $hash -and $hash -ne $RequireChangeFrom) {
                $changeSeen = $true
            }
            elseif (($Clock.Elapsed.TotalSeconds - $graceStart) -gt $GraceSeconds) {
                # 一直没变过：窗口出现时画的就是图片
                return $null
            }
            else {
                Start-Sleep -Milliseconds 5
                continue
            }
        }
        if ($null -ne $hash) {
            if ($null -ne $previous -and $hash -eq $previous) {
                if ($stableCount -eq 0) { $stableSince = $now }
                $stableCount++
                if ($stableCount -ge 2) { return $stableSince }
            }
            else {
                $stableCount = 0
                $stableSince = $null
            }
            $previous = $hash
        }
        Start-Sleep -Milliseconds 5
    }
    return $null
}

function Measure-Open {
    param([string]$ExePath)
    Close-ViewerProcesses $ExePath
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $null = Start-Process -FilePath $ExePath -ArgumentList ('"' + $firstImage + '"') -PassThru
    $appeared = Wait-ViewerWindow -ExePath $ExePath -Clock $clock -TimeoutSeconds $OpenTimeoutSeconds
    if ($null -eq $appeared) { return $null }
    # 有的软件不抢前台，窗口会开在终端后面；不置前就只能抓到终端的像素
    [void](Assert-Foreground $appeared.Window)
    $initialFrame = Get-FrameHash $appeared.Window
    $settled = Wait-FrameSettled -Window $appeared.Window -Clock $clock `
        -TimeoutSeconds $OpenTimeoutSeconds -RequireChangeFrom $initialFrame
    return [pscustomobject]@{
        Window = $appeared.Window
        WindowMs = [math]::Round($appeared.Elapsed, 1)
        # 返回 $null 有两种含义：窗口一出现画的就是图片（稳定时间＝窗口出现时间），
        # 或者到超时都没稳住。用是否超时区分，不能一律记成「测不到」。
        SettledMs = if ($null -ne $settled) { [math]::Round($settled, 1) }
                    elseif ($clock.Elapsed.TotalSeconds -lt $OpenTimeoutSeconds) { [math]::Round($appeared.Elapsed, 1) }
                    else { $null }
        # 趁窗口还在记下来：报告里看到「量的是哪个窗口」才发现得了
        # ImageGlass 卡在模态框、2345看图王 开的是缩略图管理窗口这类情况。
        Title = [ViewerCompare]::Title($appeared.Window)
        Class = [ViewerCompare]::ClassName($appeared.Window)
    }
}

# 返回 @{ Key = 虚拟键码; Post = 是否改用投递给焦点子窗口 }，找不到返回 $null
function Find-NextImageInput {
    param([IntPtr]$Window)
    $candidates = @(
        @{ Name = "Right";    Key = 0x27 },
        @{ Name = "PageDown"; Key = 0x22 },
        @{ Name = "Space";    Key = 0x20 },
        @{ Name = "Down";     Key = 0x28 }
    )
    # 先不点鼠标试一轮；不行再点一下窗口中央（图片区）把键盘焦点交给图像控件，
    # 有的软件（ImageGlass、2345看图王）顶层窗口拿到前台也不等于图像控件有焦点。
    foreach ($click in @($false, $true)) {
      if ($click) {
        $rect = New-Object ViewerCompare+RECT
        if ([ViewerCompare]::GetWindowRect($Window, [ref]$rect)) {
            $centerX = $rect.L + [int](($rect.R - $rect.L) / 2)
            $centerY = $rect.T + [int](($rect.B - $rect.T) / 2)
            [ViewerCompare]::ClickAt($centerX, $centerY)
            Start-Sleep -Milliseconds 400
        }
      }
      foreach ($post in @($false, $true)) {
        foreach ($candidate in $candidates) {
            if (-not (Assert-Foreground $Window)) { return $null }
            $settleClock = [Diagnostics.Stopwatch]::StartNew()
            $null = Wait-FrameSettled -Window $Window -Clock $settleClock -TimeoutSeconds 3
            $before = Get-FrameHash $Window
            if ($null -eq $before) { continue }
            if ($post) { [void][ViewerCompare]::PostKeyToFocus($Window, [uint16]$candidate.Key) }
            else { [ViewerCompare]::SendKey([uint16]$candidate.Key) }
            $deadline = [DateTime]::UtcNow.AddSeconds(5)
            while ([DateTime]::UtcNow -lt $deadline) {
                $after = Get-FrameHash $Window
                if ($null -ne $after -and $after -ne $before) {
                    return @{ Key = [uint16]$candidate.Key; Post = $post
                        Label = $candidate.Name + $(if ($post) { "（投递给焦点窗口）" } else { "" }) }
                }
                Start-Sleep -Milliseconds 30
            }
        }
      }
    }
    return $null
}

# Trigger 是 Find-NextImageInput 标定出来的翻页方式。注意不能叫 $Input：
# 那是 PowerShell 的自动变量（管道枚举器），会被它顶掉。
function Measure-Switches {
    param([IntPtr]$Window, [int]$Count, [hashtable]$Trigger, [string]$ExePath)
    $firstChange = New-Object Collections.ArrayList
    $settle = New-Object Collections.ArrayList
    $missed = 0
    $processName = [IO.Path]::GetFileNameWithoutExtension($ExePath)
    for ($index = 0; $index -lt $Count; $index++) {
        # 有的软件换图时会重建窗口，攥着旧句柄采样就永远「没变化」
        $live = Get-Process -Name $processName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($live) {
            $current = [ViewerCompare]::MainWindow([uint32]$live.Id)
            if ($current -ne [IntPtr]::Zero) { $Window = $current }
        }
        if (-not (Assert-Foreground $Window)) {
            Start-Sleep -Milliseconds 200
            if (-not (Assert-Foreground $Window)) {
                return [pscustomobject]@{ Foreground = $false }
            }
        }
        # 先等画面静下来再取基准。不等的话，有淡入动画或常驻动态元素的软件
        # 会在按键之前就「变化」，量出来的首帧延迟是假的（实测能低到 0.6 ms）。
        $settleClock = [Diagnostics.Stopwatch]::StartNew()
        $null = Wait-FrameSettled -Window $Window -Clock $settleClock -TimeoutSeconds 3
        $baseline = Get-FrameHash $Window
        if ($null -eq $baseline) { $missed++; continue }
        $clock = [Diagnostics.Stopwatch]::StartNew()
        if ($Trigger.Post) { [void][ViewerCompare]::PostKeyToFocus($Window, $Trigger.Key) }
        else { [ViewerCompare]::SendKey($Trigger.Key) }
        $changedAt = $null
        while ($clock.Elapsed.TotalSeconds -lt $SwitchTimeoutSeconds) {
            if (-not (Assert-Foreground $Window)) { Start-Sleep -Milliseconds 10; continue }
            $hash = Get-FrameHash $Window
            # 时间戳取在抓完之后：抓一帧本身要几毫秒，取在抓之前等于系统性少算一帧，
            # 三个软件都量出 1 ms 就是这个原因。取在之后是个诚实的上界。
            $now = $clock.Elapsed.TotalMilliseconds
            if ($null -ne $hash -and $hash -ne $baseline) { $changedAt = $now; break }
            Start-Sleep -Milliseconds 3
        }
        if ($null -eq $changedAt) { $missed++; continue }
        [void]$firstChange.Add($changedAt)
        $settled = Wait-FrameSettled -Window $Window -Clock $clock -TimeoutSeconds $SwitchTimeoutSeconds
        if ($null -ne $settled) { [void]$settle.Add($settled) }
        Start-Sleep -Milliseconds 120
    }
    return [pscustomobject]@{
        Foreground = $true
        FirstChange = $firstChange
        Settled = $settle
        Missed = $missed
    }
}

function Get-Median([object]$Values) {
    if ($null -eq $Values -or $Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $middle = [int]($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return [math]::Round($sorted[$middle], 1) }
    return [math]::Round(($sorted[$middle - 1] + $sorted[$middle]) / 2, 1)
}

# ---- 正式测量 ----------------------------------------------------------------
Write-Host "对比目录：$ImageDir（$($images.Count) 张）"
Write-Host "每个软件：打开 $Repeats 次取中位，连续切换 $Switches 次"
Write-Host ""

$rows = New-Object Collections.ArrayList
foreach ($exe in $Viewer) {
    $resolved = try { (Resolve-Path -LiteralPath $exe -ErrorAction Stop).Path } catch { $exe }
    $label = [IO.Path]::GetFileNameWithoutExtension($resolved)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        Write-Host "[跳过] $label：找不到 $resolved" -ForegroundColor DarkYellow
        continue
    }
    Write-Host "=== $label ===" -ForegroundColor Cyan

    # 先热一遍：第一次启动要读盘、过杀软、建缓存，算进去对先测的那个不公平
    $warm = Measure-Open -ExePath $resolved
    if ($null -eq $warm) {
        Write-Host "  预热时窗口没有出现，跳过" -ForegroundColor DarkYellow
        Close-ViewerProcesses $resolved
        continue
    }
    Close-ViewerProcesses $resolved

    $windowTimes = New-Object Collections.ArrayList
    $settleTimes = New-Object Collections.ArrayList
    $lastOpen = $null
    for ($round = 0; $round -lt $Repeats; $round++) {
        $open = Measure-Open -ExePath $resolved
        if ($null -eq $open) { continue }
        [void]$windowTimes.Add($open.WindowMs)
        if ($null -ne $open.SettledMs) { [void]$settleTimes.Add($open.SettledMs) }
        $lastOpen = $open
        if ($round -lt $Repeats - 1) { Close-ViewerProcesses $resolved }
    }

    $switchResult = $null
    $switchInput = $null
    if ($null -ne $lastOpen) {
        $switchInput = Find-NextImageInput -Window $lastOpen.Window
        if ($null -ne $switchInput) {
            Write-Host ("  翻页方式：{0}" -f $switchInput.Label)
            $switchResult = Measure-Switches -Window $lastOpen.Window -Count $Switches -Trigger $switchInput -ExePath $resolved
        }
        else {
            Write-Host "  没找到能翻页的按键，切换未测" -ForegroundColor DarkYellow
        }
    }
    Close-ViewerProcesses $resolved

    $exeSizeMb = [math]::Round((Get-Item -LiteralPath $resolved).Length / 1MB, 1)
    # 只看 exe 会失真：.NET 或壳程序的 exe 才几百 KB，代码都在同目录的 DLL 里。
    # 目录名带程序名时统计整个安装目录；绿色单文件（如放在公共目录里的 JarkViewer）
    # 只报 exe 自身，免得把目录里别人的文件也算进去。
    $installDirectory = Split-Path -Parent $resolved
    $installSizeMb = $null
    if ((Split-Path -Leaf $installDirectory) -like "*$label*") {
        $installSizeMb = [math]::Round(((Get-ChildItem -LiteralPath $installDirectory -Recurse -File `
            -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum) / 1MB, 1)
    }
    $row = [pscustomobject]@{
        软件          = $label
        程序体积MB    = $exeSizeMb
        安装目录MB    = $installSizeMb
        窗口出现ms    = Get-Median $windowTimes
        画面稳定ms    = Get-Median $settleTimes
        切换首帧ms    = if ($switchResult -and $switchResult.Foreground) { Get-Median $switchResult.FirstChange } else { $null }
        切换稳定ms    = if ($switchResult -and $switchResult.Foreground) { Get-Median $switchResult.Settled } else { $null }
        切换失败次数  = if ($switchResult -and $switchResult.Foreground) { $switchResult.Missed } else { $null }
        翻页方式      = if ($switchInput) { $switchInput.Label } else { "" }
        窗口标题      = if ($lastOpen) { $lastOpen.Title } else { "" }
        窗口类名      = if ($lastOpen) { $lastOpen.Class } else { "" }
        备注          = if ($null -eq $switchInput) { "没找到翻页按键，切换未测" }
                        elseif ($switchResult -and -not $switchResult.Foreground) { "无法置于前台，切换未测" }
                        else { "" }
    }
    [void]$rows.Add($row)
    "  窗口出现 {0} ms   画面稳定 {1} ms   切换首帧 {2} ms   切换稳定 {3} ms" -f `
        $row.窗口出现ms, $row.画面稳定ms, $row.切换首帧ms, $row.切换稳定ms | Write-Host
}

$sampleGraphics.Dispose()
$sampleBitmap.Dispose()

if ($rows.Count -eq 0) {
    Write-Output "SKIPPED 没有任何软件完成测量"
    exit $EXIT_SKIPPED
}

Write-Host ""
$rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host

if ($OutputCsv) {
    $directory = Split-Path -Parent $OutputCsv
    if ($directory -and -not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $rows | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding UTF8
    Write-Host "已写入 $OutputCsv"
}

exit 0
