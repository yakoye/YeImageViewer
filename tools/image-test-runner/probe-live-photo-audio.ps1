<#
.SYNOPSIS
    实况照片声音探针：验证「自动播放默认静音」与「主动播放出声」。

.DESCRIPTION
    不靠耳朵：用 Windows Core Audio 读查看器进程音频会话的峰值电平。
      1. 打开 test/live-photo/live-photo.livp，自动播放的那 3 秒里电平必须为 0（默认静音）
      2. 播完后按空格重播，这是主动操作，必须出声
      3. 再把鼠标移到左上角「实况」标记上（与 macOS「照片」相同的交互），同样必须出声
    查看器从临时目录里的一份全新副本运行：设置文件跟随 exe，全新副本就是默认设置，
    不会被开发机上改过的设置干扰。

    没有音频输出设备（例如远程桌面设成不播放声音）时无从判定，记为未执行。
    退出码：0 通过，1 失败，3 未执行。
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\x64\Release\YeImageViewer.exe"),
    [string]$Fixture = (Join-Path $PSScriptRoot "..\..\test\live-photo\live-photo.livp"),
    [string]$Screenshot
)

$ErrorActionPreference = "Stop"
$EXIT_SKIPPED = 3
$SILENT_PEAK = 0.001   # 自动播放期间允许的最大电平：静音就该是 0
$AUDIBLE_PEAK = 0.02   # 主动播放期间至少要到的电平：素材底音幅度 0.12，响声 0.72

foreach ($required in @($Exe, $Fixture)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        Write-Output "SKIPPED 缺少 $required"
        exit $EXIT_SKIPPED
    }
}

if (-not ("LiveAudioProbe" -as [type])) {
    Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    int NotImplEnumAudioEndpoints();
    [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
}

[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams,
        [MarshalAs(UnmanagedType.IUnknown)] out object instance);
}

[ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 {
    int NotImplGetAudioSessionControl();
    int NotImplGetSimpleAudioVolume();
    [PreserveSig] int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
}

[ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator {
    [PreserveSig] int GetCount(out int count);
    [PreserveSig] int GetSession(int index, out IAudioSessionControl2 session);
}

[ComImport, Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
    int NotImplGetState();
    int NotImplGetDisplayName();
    int NotImplSetDisplayName();
    int NotImplGetIconPath();
    int NotImplSetIconPath();
    int NotImplGetGroupingParam();
    int NotImplSetGroupingParam();
    int NotImplRegisterAudioSessionNotification();
    int NotImplUnregisterAudioSessionNotification();
    int NotImplGetSessionIdentifier();
    int NotImplGetSessionInstanceIdentifier();
    [PreserveSig] int GetProcessId(out uint processId);
}

[ComImport, Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioMeterInformation {
    [PreserveSig] int GetPeakValue(out float peak);
}

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
class MMDeviceEnumeratorCom { }

public static class LiveAudioProbe {
    public delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref POINT point);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    // 查看器按显示器感知 DPI、坐标都是物理像素；探针若不感知 DPI，查到的客户区会被系统
    // 按缩放比虚拟化，算出的坐标送过去就落错位置。取几何信息前先切到同一种感知模式。
    public static void UsePhysicalPixels() { SetThreadDpiAwarenessContext(new IntPtr(-4)); }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int length);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    public static IntPtr FindWindow(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processId && IsWindowVisible(window)) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static string Title(IntPtr window) {
        var text = new StringBuilder(512);
        GetWindowText(window, text, text.Capacity);
        return text.ToString();
    }

    static IAudioSessionManager2 Manager() {
        var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDevice device;
        if (enumerator.GetDefaultAudioEndpoint(0 /* eRender */, 1 /* eMultimedia */, out device) != 0 || device == null)
            return null;
        var iid = typeof(IAudioSessionManager2).GUID;
        object manager;
        if (device.Activate(ref iid, 0x17 /* CLSCTX_ALL */, IntPtr.Zero, out manager) != 0)
            return null;
        return (IAudioSessionManager2)manager;
    }

    public static bool HasOutputDevice() {
        try { return Manager() != null; } catch { return false; }
    }

    // 该进程所有音频会话里此刻的最高峰值电平（0~1）；没有会话时为 0
    public static float PeakFor(uint processId) {
        var manager = Manager();
        if (manager == null) return 0f;
        IAudioSessionEnumerator sessions;
        if (manager.GetSessionEnumerator(out sessions) != 0) return 0f;
        int count;
        sessions.GetCount(out count);
        float best = 0f;
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl2 session;
            if (sessions.GetSession(i, out session) != 0 || session == null) continue;
            uint owner;
            if (session.GetProcessId(out owner) != 0 || owner != processId) continue;
            float peak;
            if (((IAudioMeterInformation)session).GetPeakValue(out peak) == 0 && peak > best) best = peak;
        }
        return best;
    }
}
"@
}

if (-not [LiveAudioProbe]::HasOutputDevice()) {
    Write-Output "SKIPPED 没有可用的音频输出设备，无法判定是否出声"
    exit $EXIT_SKIPPED
}

$WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101; $WM_MOUSEMOVE = 0x0200; $VK_SPACE = 0x20

# 采样一段时间里该进程的最高电平
function Measure-Peak([uint32]$ProcessId, [int]$Milliseconds) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $peak = 0.0
    while ($watch.ElapsedMilliseconds -lt $Milliseconds) {
        $peak = [Math]::Max($peak, [LiveAudioProbe]::PeakFor($ProcessId))
        Start-Sleep -Milliseconds 30
    }
    return $peak
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-LiveAudio-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $work | Out-Null
$viewerCopy = Join-Path $work "YeImageViewer.exe"
Copy-Item -LiteralPath $Exe -Destination $viewerCopy
$process = $null
$failures = @()
try {
    $process = Start-Process -FilePath $viewerCopy -ArgumentList ('"' + [IO.Path]::GetFullPath($Fixture) + '"') -PassThru
    $window = [IntPtr]::Zero
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 50
        $window = [LiveAudioProbe]::FindWindow([uint32]$process.Id)
    }
    if ($window -eq [IntPtr]::Zero) {
        Write-Output "FAIL 查看器窗口没有出现"
        exit 1
    }

    # 1. 自动播放：默认静音
    $autoPeak = Measure-Peak $process.Id 3200
    Write-Output ("自动播放期间最高电平  {0:N4}  （要求 <= {1}）" -f $autoPeak, $SILENT_PEAK)
    if ($autoPeak -gt $SILENT_PEAK) { $failures += "自动播放出声了，默认应当静音" }
    Start-Sleep -Milliseconds 500

    # 2. 空格重播：主动操作，出声
    [void][LiveAudioProbe]::PostMessage($window, $WM_KEYDOWN, [IntPtr]$VK_SPACE, [IntPtr]0)
    [void][LiveAudioProbe]::PostMessage($window, $WM_KEYUP, [IntPtr]$VK_SPACE, [IntPtr]0)
    $replayPeak = Measure-Peak $process.Id 2500
    Write-Output ("空格重播期间最高电平  {0:N4}  （要求 >= {1}）" -f $replayPeak, $AUDIBLE_PEAK)
    if ($replayPeak -lt $AUDIBLE_PEAK) { $failures += "空格重播没有出声" }
    Start-Sleep -Milliseconds 1200

    # 3. 悬停「实况」标记：标记贴在图片左上角内侧 12 逻辑像素处。图片按标题里的缩放比显示，
    #    但沉浸模式下纵向可能带偏移，不能假定正中——在预计位置周围逐点移动鼠标，
    #    任何一点进入标记都会触发出声播放；已在出声播放时再次进入不会重启，只触发一次。
    [LiveAudioProbe]::UsePhysicalPixels()
    $client = New-Object LiveAudioProbe+RECT
    [void][LiveAudioProbe]::GetClientRect($window, [ref]$client)
    $dpi = [LiveAudioProbe]::GetDpiForWindow($window)
    $title = [LiveAudioProbe]::Title($window)
    $zoom = if ($title -match '(\d+)%') { [int]$Matches[1] / 100.0 } else { 1.0 }
    $shownWidth = [Math]::Round(640 * $zoom)
    $shownHeight = [Math]::Round(480 * $zoom)
    $imageLeft = [Math]::Round(($client.Right - $shownWidth) / 2.0)
    $imageTop = [Math]::Round(($client.Bottom - $shownHeight) / 2.0)
    $margin = [Math]::Floor((12 * $dpi + 48) / 96)
    $badgeX = [Math]::Max($margin, $imageLeft + $margin) + [Math]::Floor((20 * $dpi + 48) / 96)
    $badgeY = [Math]::Max($margin, $imageTop + $margin) + [Math]::Floor((12 * $dpi + 48) / 96)
    $spread = [Math]::Floor((48 * $dpi + 48) / 96)
    $step = [Math]::Max(1, [Math]::Floor((4 * $dpi + 48) / 96))
    for ($scanY = $badgeY - $spread; $scanY -le $badgeY + $spread; $scanY += $step) {
        $position = [IntPtr](([int][Math]::Max(0, $scanY) -shl 16) -bor ([int]$badgeX -band 0xFFFF))
        [void][LiveAudioProbe]::PostMessage($window, $WM_MOUSEMOVE, [IntPtr]0, $position)
    }
    Start-Sleep -Milliseconds 400
    if ($Screenshot) {
        # 截图只供人看观感，不参与判定；远程桌面最小化或锁屏时截不到，跳过即可
        try {
            Add-Type -AssemblyName System.Drawing
            $origin = New-Object LiveAudioProbe+POINT
            [void][LiveAudioProbe]::ClientToScreen($window, [ref]$origin)
            $bitmap = New-Object System.Drawing.Bitmap $client.Right, $client.Bottom
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
            $bitmap.Save($Screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
            $graphics.Dispose(); $bitmap.Dispose()
        }
        catch {
            Write-Output "NOTE 截不到屏幕（远程桌面最小化或锁屏），跳过截图"
        }
    }
    $hoverPeak = Measure-Peak $process.Id 2100
    Write-Output ("悬停标记期间最高电平  {0:N4}  （要求 >= {1}；客户区 {2}x{3}，DPI {4}，缩放 {5:P0}）" -f `
        $hoverPeak, $AUDIBLE_PEAK, $client.Right, $client.Bottom, $dpi, $zoom)
    if ($hoverPeak -lt $AUDIBLE_PEAK) { $failures += "悬停「实况」标记没有出声" }
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
    # 刚退出的 exe 可能被系统组件短暂占用，稍候重试删除
    $cleanup = [Diagnostics.Stopwatch]::StartNew()
    while ((Test-Path -LiteralPath $work) -and $cleanup.Elapsed.TotalSeconds -lt 10) {
        try { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction Stop } catch { Start-Sleep -Milliseconds 250 }
    }
}

if ($failures.Count -gt 0) {
    Write-Output ("FAIL " + ($failures -join "；"))
    exit 1
}
Write-Output "PASS 自动播放默认静音，空格重播与悬停「实况」标记都会出声"
exit 0
