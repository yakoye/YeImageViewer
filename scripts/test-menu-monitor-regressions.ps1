param(
    [string]$Viewer = (Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),
    [string]$Fixture = (Join-Path $PSScriptRoot '../test/format corpus/files/common.png'),
    [ValidateSet('All', 'Menu', 'Monitor')][string]$Mode = 'All'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# 真窗口回归：每轮用独立 exe、配置和最小图片夹具，不能污染开发目录的设置。
if (-not ('MenuMonitorRegressionNative' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class MenuMonitorRegressionNative {
    public delegate bool WindowCallback(IntPtr window, IntPtr data);
    public delegate bool MonitorCallback(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct GUI {
        public int Size; public uint Flags;
        public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
        public RECT CaretRect;
    }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct MONITOR {
        public int Size; public RECT Rect, Work; public uint Flags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string Device;
    }
    [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder name, int size);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint thread, ref GUI info);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint first, uint second, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int w, int h, uint flags);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorCallback callback, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="GetMonitorInfoW")] public static extern bool GetMonitorInfo(IntPtr monitor, ref MONITOR info);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    public static IntPtr FindWindow(uint process, string expectedClass) {
        IntPtr result=IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr data) {
            uint owner; GetWindowThreadProcessId(window, out owner);
            var name=new StringBuilder(256); GetClassName(window, name, name.Capacity);
            if (owner==process && name.ToString()==expectedClass && IsWindowVisible(window)) {
                result=window; return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static bool MenuOpen(IntPtr window) {
        uint owner; uint thread=GetWindowThreadProcessId(window,out owner);
        var info=new GUI(); info.Size=Marshal.SizeOf(info);
        return GetGUIThreadInfo(thread,ref info) && (info.Flags & 0x1C)!=0 && info.MenuOwner==window;
    }
    public static MONITOR[] Monitors() {
        var result=new List<MONITOR>();
        EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,delegate(IntPtr h,IntPtr dc,IntPtr rect,IntPtr data) {
            var info=new MONITOR(); info.Size=Marshal.SizeOf(info);
            if (GetMonitorInfo(h,ref info)) result.Add(info);
            return true;
        },IntPtr.Zero);
        return result.ToArray();
    }
    public static string WindowMonitor(IntPtr window) {
        var info=new MONITOR(); info.Size=Marshal.SizeOf(info);
        return GetMonitorInfo(MonitorFromWindow(window,2),ref info) ? info.Device : "";
    }
    public static string Title(IntPtr window) {
        var text=new StringBuilder(1024); GetWindowText(window,text,text.Capacity);
        return text.ToString();
    }
    public static void Activate(IntPtr window) {
        SetWindowPos(window,new IntPtr(-1),0,0,0,0,0x0013);
        uint owner; uint foreground=GetWindowThreadProcessId(GetForegroundWindow(),out owner);
        uint current=GetCurrentThreadId();
        bool attached=foreground!=0 && foreground!=current && AttachThreadInput(current,foreground,true);
        try { SetForegroundWindow(window); }
        finally { if (attached) AttachThreadInput(current,foreground,false); }
    }
}
'@
}

function Wait-Condition([scriptblock]$Condition, [string]$Failure) {
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 60
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Failure
}

function Send-WindowMessage([IntPtr]$Window, [uint32]$Message, [uint32]$Value = 0, [long]$Data = 0) {
    [void][MenuMonitorRegressionNative]::PostMessage($Window, $Message, [UIntPtr]$Value, [IntPtr]$Data)
}

function Click-Setting([IntPtr]$Window, [int]$X, [int]$Y) {
    $scale = [MenuMonitorRegressionNative]::GetDpiForWindow($Window) / 96.0
    $point = ([int][Math]::Round($X * $scale) -band 65535) -bor
        (([int][Math]::Round($Y * $scale) -band 65535) -shl 16)
    Send-WindowMessage $Window 0x0201 1 $point
    Send-WindowMessage $Window 0x0202 0 $point
    Start-Sleep -Milliseconds 150
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempRoot = Join-Path $tempBase ('YeImageViewer-MenuMonitor-' + [guid]::NewGuid().ToString('N'))
$originalMonitorOverride = $env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR
$originalDpi = [MenuMonitorRegressionNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$originalCursor = New-Object MenuMonitorRegressionNative+POINT
[void][MenuMonitorRegressionNative]::GetCursorPos([ref]$originalCursor)
$process = $null
try {
    $env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR = $null
    [void](New-Item -ItemType Directory -Path $tempRoot)
    $testViewer = Join-Path $tempRoot 'YeImageViewer.exe'
    $image = Join-Path $tempRoot 'common.png'
    Copy-Item -LiteralPath $Viewer -Destination $testViewer
    Copy-Item -LiteralPath $Fixture -Destination $image
    if ($Mode -ne 'Monitor') {
        $keyResult = Join-Path $tempRoot 'menu-key.txt'
        $keyTest = Start-Process -FilePath $testViewer -ArgumentList @('--menu-key-selftest',
            ('"' + $keyResult + '"')) -WindowStyle Hidden -Wait -PassThru
        if ($keyTest.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $keyResult) -or
            (Get-Content -LiteralPath $keyResult -Raw) -notmatch 'SUMMARY\s+8\s+0') {
            throw '生产键盘路由自检失败：Shift + F10 未保持固定菜单入口'
        }
        Write-Host 'PASS 生产 OnKeyDown 的 8 项固定菜单路由自检（包含旧配置冲突）全部通过。'
    }
    function Start-TestViewer([bool]$WithImage = $true) {
        if ($WithImage) {
            $script:process = Start-Process -FilePath $testViewer -ArgumentList ('"' + $image + '"') -PassThru
        }
        else {
            $script:process = Start-Process -FilePath $testViewer -PassThru
        }
        Wait-Condition { [MenuMonitorRegressionNative]::FindWindow($process.Id, 'D3D11WndClass') -ne [IntPtr]::Zero } '程序未创建窗口'
        Start-Sleep -Milliseconds 300
        return [MenuMonitorRegressionNative]::FindWindow($process.Id, 'D3D11WndClass')
    }
    function Close-TestViewer([IntPtr]$Window) {
        Send-WindowMessage $Window 0x0010
        if (-not $process.WaitForExit(6000)) { throw '关闭窗口后进程未退出' }
    }
    function Open-Settings([IntPtr]$Window) {
        Send-WindowMessage $Window 0x0100 0x70
        Wait-Condition { [MenuMonitorRegressionNative]::FindWindow($process.Id, 'YeImageViewerSettingWnd') -ne [IntPtr]::Zero } '设置窗口未打开'
        return [MenuMonitorRegressionNative]::FindWindow($process.Id, 'YeImageViewerSettingWnd')
    }
    function Close-Settings([IntPtr]$Window) {
        Send-WindowMessage $Window 0x0010
        Wait-Condition { [MenuMonitorRegressionNative]::FindWindow($process.Id, 'YeImageViewerSettingWnd') -eq [IntPtr]::Zero } '设置窗口未关闭'
        Start-Sleep -Milliseconds 200
    }

    if ($Mode -ne 'Monitor') {
        $window = Start-TestViewer
        $settings = Open-Settings $window
        # 常规第 4 组的第 2 项：右键退出。几何对应 SettingLayout / SettingCommand。
        Click-Setting $settings 480 431
        Close-Settings $settings
        $point = 200 -bor (150 -shl 16)
        foreach ($releaseControlFirst in @($false, $true)) {
            Send-WindowMessage $window 0x0204 10 $point
            Send-WindowMessage $window 0x0205 $(if ($releaseControlFirst) { 0 } else { 4 }) $point
            Wait-Condition { [MenuMonitorRegressionNative]::MenuOpen($window) } 'Ctrl + 右键未打开菜单（或误执行了退出）'
            Send-WindowMessage $window 0x001F
            Wait-Condition { -not [MenuMonitorRegressionNative]::MenuOpen($window) } '取消菜单后仍处于菜单循环'
        }
        if ([MenuMonitorRegressionNative]::GetForegroundWindow() -ne [IntPtr]::Zero) {
            [MenuMonitorRegressionNative]::Activate($window)
        }
        if ([MenuMonitorRegressionNative]::GetForegroundWindow() -eq $window) {
            [MenuMonitorRegressionNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
            try {
                Start-Sleep -Milliseconds 150
                Send-WindowMessage $window 0x0204 10 $point
                Send-WindowMessage $window 0x0205 4 $point
                Wait-Condition { [MenuMonitorRegressionNative]::MenuOpen($window) } '真实 Ctrl 按下时菜单未打开'
            }
            finally { [MenuMonitorRegressionNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }
            Start-Sleep -Milliseconds 150
            Send-WindowMessage $window 0x001F
            Wait-Condition { -not [MenuMonitorRegressionNative]::MenuOpen($window) } 'Ctrl 菜单未取消'
            Send-WindowMessage $window 0x0100 0x51
            Wait-Condition { [MenuMonitorRegressionNative]::Title($window).Contains('90') } '取消菜单后 Ctrl 状态卡住，普通 Q 旋转快捷键失效'
            [MenuMonitorRegressionNative]::Activate($window)
            Wait-Condition { [MenuMonitorRegressionNative]::GetForegroundWindow() -eq $window } '键盘菜单测试前，窗口未获得前台'
            [MenuMonitorRegressionNative]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero)
            try {
                Start-Sleep -Milliseconds 100
                [MenuMonitorRegressionNative]::keybd_event(0x79, 0, 0, [UIntPtr]::Zero)
                [MenuMonitorRegressionNative]::keybd_event(0x79, 0, 2, [UIntPtr]::Zero)
                Wait-Condition { [MenuMonitorRegressionNative]::MenuOpen($window) } 'Shift + F10 未打开菜单'
            }
            finally { [MenuMonitorRegressionNative]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }
            Write-Host 'PASS Shift + F10 真键盘输入打开菜单，菜单取消后普通快捷键可用。'
        }
        else {
            Write-Host 'SKIPPED Shift + F10 真键盘输入：测试窗口无法获得前台，不向其他窗口注入按键。'
            # 自检已验证 OnKeyDown 产生此消息，再验证键盘菜单位置及命令下游。
            Send-WindowMessage $window 0x007B 0 -1
            Wait-Condition { [MenuMonitorRegressionNative]::MenuOpen($window) } '键盘菜单消息未打开原生菜单'
        }
        # 从底部的退出往上三项是设置；原生菜单忽略分隔线。
        Send-WindowMessage $window 0x0100 0x23
        1..3 | ForEach-Object { Send-WindowMessage $window 0x0100 0x26 }
        Send-WindowMessage $window 0x0100 0x0D
        Wait-Condition { [MenuMonitorRegressionNative]::FindWindow($process.Id, 'YeImageViewerSettingWnd') -ne [IntPtr]::Zero } '固定菜单入口中的设置命令未执行'
        Close-Settings ([MenuMonitorRegressionNative]::FindWindow($process.Id, 'YeImageViewerSettingWnd'))
        Send-WindowMessage $window 0x0204 2 $point
        Send-WindowMessage $window 0x0205 0 $point
        if (-not $process.WaitForExit(6000)) { throw '未修饰右键没有保留退出行为' }
        Write-Host 'PASS 右键退出模式下 Ctrl + 右键及菜单命令可用；先松 Ctrl 不误退出，普通右键仍退出。'
    }

    if ($Mode -ne 'Menu') {
        $monitors = @([MenuMonitorRegressionNative]::Monitors())
        if ($monitors.Count -lt 2) {
            Write-Host 'SKIPPED 多显示器真窗口测试：当前只有一块显示器；选屏策略仍由单元测试覆盖。'
        }
        else {
            $primary = @($monitors | Where-Object { ($_.Flags -band 1) -ne 0 })[0]
            $secondary = @($monitors | Where-Object { ($_.Flags -band 1) -eq 0 })[0]
            foreach ($maximize in @($false, $true)) {
                $window = Start-TestViewer
                # 从默认沉浸显示切回带边框窗口，再移到副屏。
                Send-WindowMessage $window 0x0100 0x46
                Start-Sleep -Milliseconds 150
                [void][MenuMonitorRegressionNative]::SetWindowPos($window, [IntPtr]::Zero,
                    $secondary.Work.Left + 50, $secondary.Work.Top + 50, 650, 500, 0x0014)
                Wait-Condition { [MenuMonitorRegressionNative]::WindowMonitor($window) -eq $secondary.Device } '窗口未移到副屏'
                if ($maximize) { Send-WindowMessage $window 0x0112 0xF030; Start-Sleep -Milliseconds 250 }
                Close-TestViewer $window
                [void][MenuMonitorRegressionNative]::SetCursorPos($primary.Work.Left + 100, $primary.Work.Top + 100)
                $window = Start-TestViewer
                if ([MenuMonitorRegressionNative]::WindowMonitor($window) -ne $secondary.Device) {
                    throw "记忆屏被鼠标所在屏覆盖（最大化=$maximize）"
                }
                Close-TestViewer $window
            }
            $window = Start-TestViewer $false
            if ([MenuMonitorRegressionNative]::WindowMonitor($window) -ne $secondary.Device) { throw '无图片启动未记住最后显示器' }
            $settings = Open-Settings $window
            # 常规第 6 个复选框：记住最后使用显示器。默认选中，这里关闭。
            Click-Setting $settings 420 194
            Close-Settings $settings
            Close-TestViewer $window
            $window = Start-TestViewer
            if ([MenuMonitorRegressionNative]::WindowMonitor($window) -ne $primary.Device) { throw '关闭记忆选项后未跟随鼠标所在屏' }
            Close-TestViewer $window
            Write-Host 'PASS 普通/最大化退出及无图片启动记住副屏；关闭记忆后跟随鼠标所在屏。'
        }
    }
}
finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        [void]$process.WaitForExit(3000)
    }
    [void][MenuMonitorRegressionNative]::SetCursorPos($originalCursor.X, $originalCursor.Y)
    [void][MenuMonitorRegressionNative]::SetThreadDpiAwarenessContext($originalDpi)
    $env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR = $originalMonitorOverride
    $resolvedRoot = [IO.Path]::GetFullPath($tempRoot)
    if ($resolvedRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedRoot).StartsWith('YeImageViewer-MenuMonitor-')) {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
