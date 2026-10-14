param(
    [string]$Viewer = (Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),
    [string]$Animation = (Join-Path $PSScriptRoot '../test/format corpus/files/animated.gif'),
    [switch]$KeepEvidence,
    [switch]$SettingsDpiOnly,
    [switch]$PanOnly,
    [switch]$KeyboardPanOnly,
    [switch]$KeyboardSmoothOnly,
    [switch]$SensitivitySlidersOnly,
    [switch]$SettingsAuditOnly,
    [switch]$ActualSizeOnly,
    [switch]$ReaderPanOnly,
    [switch]$ShortcutAuditOnly,
    [switch]$RapidPanOnly,
    [switch]$PanVisualOnly,
    [switch]$InertiaOnly,
    [switch]$InertiaAxesOnly,
    [switch]$InertiaDecelerationOnly,
    [switch]$InertiaPrecisionOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
if (-not ('ViewingRegressionNative' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ViewingRegressionNative {
    public delegate bool Callback(IntPtr hwnd, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd,out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out RECT rect);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd,StringBuilder name,int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd,uint msg,UIntPtr w,IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,UIntPtr w,IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd,ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int w,int h,uint flags);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd,int id);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd,int command);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint first,uint second,bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr data);
    public static IntPtr Find(uint pid,string cls) {
        IntPtr result=IntPtr.Zero;
        EnumWindows(delegate(IntPtr h,IntPtr d) { uint p; GetWindowThreadProcessId(h,out p);
            var name=new StringBuilder(256); GetClassName(h,name,name.Capacity);
            if(p==pid && name.ToString()==cls && IsWindowVisible(h)) {result=h;return false;} return true;
        },IntPtr.Zero);return result;
    }
    public static void Activate(IntPtr h) {
        SetWindowPos(h,new IntPtr(-1),0,0,0,0,0x0013);
        uint pid; uint foreground=GetWindowThreadProcessId(GetForegroundWindow(),out pid);
        uint current=GetCurrentThreadId();
        uint target=GetWindowThreadProcessId(h,out pid);
        bool attached=foreground!=0 && foreground!=current && AttachThreadInput(current,foreground,true);
        bool targetAttached=target!=current && target!=foreground && AttachThreadInput(current,target,true);
        try {ShowWindow(h,9); SetForegroundWindow(h); SetFocus(h);}
        finally {if(targetAttached) AttachThreadInput(current,target,false); if(attached) AttachThreadInput(current,foreground,false);}
    }
}
'@
}

$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = Join-Path $tempBase ('YeViewer-Viewing-' + [Guid]::NewGuid().ToString('N'))
$statePath = Join-Path $testRoot 'state.txt'
$oldProbe = $env:YEIMAGEVIEWER_INTERACTION_PROBE
$oldMonitor = $env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR
$oldRegistrySandbox = $env:YEIMAGEVIEWER_TEST_REGISTRY_SANDBOX
$registrySandbox = 'Software\YeImageViewerTests\Native-' + [Guid]::NewGuid().ToString('N')
$oldDpi = [ViewingRegressionNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$oldCursor = New-Object ViewingRegressionNative+POINT
[void][ViewingRegressionNative]::GetCursorPos([ref]$oldCursor)
$script:process = $null
$script:window = [IntPtr]::Zero
$script:passed = 0
$script:lastState = @{}
$script:lastReadError = ''
$evidence = Join-Path $PSScriptRoot '../artifacts/viewing-regression'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:passed++
    Write-Host ('PASS ' + $Message)
}
function Message([IntPtr]$Target, [uint32]$Id, [uint32]$W = 0, [long]$L = 0) {
    [void][ViewingRegressionNative]::SendMessage($Target,$Id,[UIntPtr]$W,[IntPtr]$L)
}
function Read-State {
    $result = @{}
    if (Test-Path -LiteralPath $statePath) {
        try {
            foreach ($line in [IO.File]::ReadAllLines($statePath)) {
                $pair = $line.Split('=',2)
                if ($pair.Count -eq 2) { $result[$pair[0]] = $pair[1] }
            }
        } catch { $script:lastReadError = $_.Exception.Message; return $script:lastState }
    }
    if ($result.Keys.Count -gt 5) { $script:lastState = $result }
    return $result
}
function Wait-State([scriptblock]$Condition,[string]$Message) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $value = Read-State
        if ($value.Keys.Count -gt 5 -and (& $Condition $value)) { return $value }
        if ($null -ne $process -and $process.HasExited) { throw ('程序提前退出：' + $Message) }
        Start-Sleep -Milliseconds 60
    } while ([DateTime]::UtcNow -lt $deadline)
    throw ($Message + '，最后状态：' + ($script:lastState | ConvertTo-Json -Compress) + '，读取错误：' + $script:lastReadError)
}
function Start-Viewer([string]$Image) {
    if (Test-Path -LiteralPath $statePath) { Remove-Item -LiteralPath $statePath -Force }
    $script:lastState = @{}
    $script:process = Start-Process -FilePath $testViewer -ArgumentList ('"' + $Image + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        $script:window = [ViewingRegressionNative]::Find($process.Id,'D3D11WndClass')
        if ($window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 80
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { throw '测试窗口未创建' }
    [ViewingRegressionNative]::Activate($window)
    return Wait-State { param($s) $s.loading -eq '0' } '图片未完成加载'
}
function Stop-Viewer {
    if ($null -ne $process -and -not $process.HasExited) {
        Message $window 0x0010
        if (-not $process.WaitForExit(8000)) { throw '测试程序关闭后未退出' }
    }
}
function Click([IntPtr]$Target,[int]$X,[int]$Y) {
    $point = ($X -band 65535) -bor (($Y -band 65535) -shl 16)
    Message $Target 0x0201 1 $point
    Message $Target 0x0202 0 $point
}
function Save-ClientEvidence([IntPtr]$Target,[string]$Name) {
    [void][ViewingRegressionNative]::SetWindowPos($Target,[IntPtr](-1),0,0,0,0,0x0013)
    Start-Sleep -Milliseconds 60
    $client=New-Object ViewingRegressionNative+RECT
    $origin=New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::GetClientRect($Target,[ref]$client)
    [void][ViewingRegressionNative]::ClientToScreen($Target,[ref]$origin)
    $bitmap=[Drawing.Bitmap]::new($client.Right,$client.Bottom)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($origin.X,$origin.Y,0,0,$bitmap.Size)
        $bitmap.Save((Join-Path $evidence $Name),[Drawing.Imaging.ImageFormat]::Png)
    } finally {$graphics.Dispose(); $bitmap.Dispose()}
}
function Set-Option([int]$Row,[int]$Option,[int]$Slot,[string]$Field='') {
    Message $window 0x0100 0x70
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $settings = [ViewingRegressionNative]::Find($process.Id,'YeImageViewerSettingWnd')
        if ($settings -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($settings -eq [IntPtr]::Zero) { throw '设置窗口未打开' }
    $counts = @(3,3,3,2,2,4,2,2,2,4,2,2,3,3,2,2,2,3,3,2,2,2,2,2,20,2,20)
    $naturalY = 52 + 216 + $Row*48 + 19
    $steps = [Math]::Max(0,[int][Math]::Ceiling(($naturalY-500)/72.0))
    for ($step=0; $step -lt $steps; $step++) { Message $settings 0x020A 4287102976 }
    $scale = [ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
    $cell = [Math]::Floor(406/$counts[$Row])
    $x = [int][Math]::Round((176 + $cell*($Option+0.5))*$scale)
    if($Row -eq 24 -or $Row -eq 26) {$x=[int][Math]::Round((188 + [Math]::Round(315*$Option/19))*$scale)}
    $y = [int][Math]::Round(($naturalY-$steps*72)*$scale)
    Click $settings $x $y
    if ($InertiaOnly -and $Row -eq 26 -and $Option -eq 4) {
        Start-Sleep -Milliseconds 100
        Save-ClientEvidence $settings '惯性设置第五档.png'
    }
    Message $settings 0x0010
    # 窗口销毁先于设置线程释放 isWorking；等待线程收尾后才能再次打开。
    Start-Sleep -Milliseconds 200
    if (-not $Field) {$Field='pref'+$Slot}
    $state = Wait-State { param($s) $s.ContainsKey($Field) -and [int]$s[$Field] -eq $Option } ('设置单选行 '+$Row+' 未生效')
    Assert-True ([int]$state[$Field] -eq $Option) ('设置行 '+$Row+' 的选项 '+$Option+' 可点击并生效')
    return $state
}
function Command([uint32]$Id) { Message $window 0x0111 $Id }
function Number($State,[string]$Name) {
    return [double]::Parse($State[$Name],[Globalization.CultureInfo]::InvariantCulture)
}
function Expected-FitPercent([double]$Width,[double]$Height,[double]$ImageWidth,[double]$ImageHeight) {
    # 1 必须为浮点数，否则 PowerShell 可能选择整数 Min 重载并把比例舍入为 0 或 1。
    return [Math]::Round([Math]::Min(1.0,[Math]::Min($Width/$ImageWidth,$Height/$ImageHeight))*100)
}
function Test-KeyboardSmooth {
    $s=Set-Option 12 0 323
    [void][ViewingRegressionNative]::SetWindowPos($window,[IntPtr]::Zero,20,20,800,650,0x0014)
    Message $window 0x0100 0x31; Message $window 0x0101 0x31; Start-Sleep -Milliseconds 400
    $before=Read-State; $step=[Math]::Floor(((Number $before 'width')+(Number $before 'height'))/16)
    Message $window 0x0100 0x57
    $samples=[Collections.Generic.List[double]]::new()
    for($i=0;$i -lt 20;$i++) {Start-Sleep -Milliseconds 30; $samples.Add((Number (Read-State) 'slideY'))}
    $s=Read-State
    Assert-True ((Number $before 'slideY')-(Number $s 'slideY') -gt 2*$step) 'W 长按不依赖系统重复事件，持续向上移动而非只走一步'
    $nonzero=0
    for($i=1;$i -lt $samples.Count;$i++) {if($samples[$i] -lt $samples[$i-1]) {$nonzero++}}
    Assert-True ($nonzero -ge 16) ("长按期间绝大多数采样持续移动，不反复重启动画造成停顿：$nonzero/19")
    Message $window 0x0101 0x57; Start-Sleep -Milliseconds 100; $released=Read-State
    Start-Sleep -Milliseconds 300; $s=Read-State
    Assert-True ($s.keyboardPanActive -eq '0' -and $s.slideY -eq $released.slideY) '长按松开后立即停止，不误用鼠标惯性'
    Message $window 0x0100 0x35; Message $window 0x0101 0x35; Start-Sleep -Milliseconds 350
    Message $window 0x0100 0x31; Message $window 0x0101 0x31; Start-Sleep -Milliseconds 350
    Message $window 0x0100 0x44; Start-Sleep -Milliseconds 250
    Message $window 0x0100 0x57; Start-Sleep -Milliseconds 250
    $before=Read-State
    Message $window 0x0101 0x44; Start-Sleep -Milliseconds 100; $after=Read-State
    Start-Sleep -Milliseconds 200; $s=Read-State
    Assert-True ((Number $s 'slideY') -lt (Number $after 'slideY') -and [Math]::Abs((Number $s 'slideX')-(Number $after 'slideX')) -le 2) '组合键松开 D 后 W 继续移动，横轴不继续漂移'
    Message $window 0x0101 0x57
    Message $window 0x0100 0x44; Start-Sleep -Milliseconds 200
    Message $window 0x0008; Start-Sleep -Milliseconds 100; $after=Read-State
    Start-Sleep -Milliseconds 200; $s=Read-State
    Assert-True ($s.keyboardPanActive -eq '0' -and $s.slideX -eq $after.slideX) '失焦取消连续平移，不依赖收到按键释放'
    Write-Host ("WASD 连续平移回归通过：$script:passed 项")
}

function Test-KeyboardPan {
    $s=Set-Option 12 0 323
    $s=Set-Option 22 0 333
    [void][ViewingRegressionNative]::SetWindowPos($window,[IntPtr]::Zero,20,20,800,650,0x0014)
    Start-Sleep -Milliseconds 250
    foreach($mode in @('普通窗口','沉浸窗口','旋转后')) {
        if($mode -eq '沉浸窗口') {$s=Set-Option 12 1 323}
        if($mode -eq '旋转后') {
            $s=Set-Option 12 0 323
            Message $window 0x0100 0x45; Message $window 0x0101 0x45
            Start-Sleep -Milliseconds 350
        }
        foreach($direction in @(
            @{Key=0x57; Name='W 向上'; X=0; Y=-1},
            @{Key=0x53; Name='S 向下'; X=0; Y=1},
            @{Key=0x41; Name='A 向左'; X=-1; Y=0},
            @{Key=0x44; Name='D 向右'; X=1; Y=0})) {
            Message $window 0x0100 0x35; Message $window 0x0101 0x35
            Start-Sleep -Milliseconds 350
            Message $window 0x0100 0x31; Message $window 0x0101 0x31
            Start-Sleep -Milliseconds 350
            $before=Read-State
            [ViewingRegressionNative]::Activate($window)
            $step=[Math]::Max(1,[Math]::Floor(((Number $before 'width')+(Number $before 'height'))/16))
            Assert-True ($before.zoom -eq '100' -and [Math]::Abs((Number $before 'slideX')) -le 2 -and [Math]::Abs((Number $before 'slideY')) -le 2) ($mode+' '+$direction.Name+' 测试从居中 100% 开始')
            Message $window 0x0100 $direction.Key; Message $window 0x0101 $direction.Key
            Start-Sleep -Milliseconds 400
            $s=Read-State
            Assert-True ([Math]::Abs((Number $s 'slideX')-(Number $before 'slideX')-$direction.X*$step) -le 1 -and
                [Math]::Abs((Number $s 'slideY')-(Number $before 'slideY')-$direction.Y*$step) -le 1) ($mode+' '+$direction.Name+' 按屏幕方向移动且另一轴不变')
            $x=[int]([Math]::Round((Number $s 'width')/2+$direction.X*$step))
            $y=[int]([Math]::Round((Number $s 'height')/2+$direction.Y*$step))
            $pixel=Screen-Pixel $x $y
            $opposite=Screen-Pixel ([int]([Math]::Round((Number $s 'width')/2-$direction.X*$step))) ([int]([Math]::Round((Number $s 'height')/2-$direction.Y*$step)))
            if($direction.Name -eq 'W 向上') {Save-ClientEvidence $window 'WASD-上移定位.png'}
            Assert-True ($pixel.R -gt 220 -and $pixel.G -lt 30 -and $opposite.G -gt 100) ($mode+' '+$direction.Name+' 红色定位标记实际绘制在正确方向：'+$pixel.ToString()+' / '+$opposite.ToString())
            Message $window 0x0100 $direction.Key; Message $window 0x0101 $direction.Key
            Start-Sleep -Milliseconds 400
            Message $window 0x0100 $direction.Key; Message $window 0x0101 $direction.Key
            Start-Sleep -Milliseconds 400; $s=Read-State
            Assert-True ([Math]::Abs((Number $s 'slideX')-(Number $before 'slideX')-$direction.X*$step*3) -le 1 -and
                [Math]::Abs((Number $s 'slideY')-(Number $before 'slideY')-$direction.Y*$step*3) -le 1) ($mode+' '+$direction.Name+' 连续按键保持同一方向')
        }
    }
    $settings=Open-ShortcutSettings
    $source=Get-Content (Join-Path $PSScriptRoot '../YeImageViewer/include/Setting.h') -Raw
    $rows=@([regex]::Matches($source,'ShortcutItem\{\s*ShortcutConfig::Action::(\w+),') | ForEach-Object {$_.Groups[1].Value})
    $row=[Array]::IndexOf($rows,'PanUp')
    $natural=52+288+40*$row+20
    $steps=[Math]::Max(0,[int][Math]::Ceiling(($natural-500)/72.0))
    for($i=0;$i -lt $steps;$i++) {Message $settings 0x020A 4287102976}
    $scale=[ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
    Click $settings ([int](417*$scale)) ([int](($natural-$steps*72)*$scale))
    Message $settings 0x0100 0x74; Message $settings 0x0101 0x74
    Message $settings 0x0010; Start-Sleep -Milliseconds 250
    Stop-Viewer; $s=Start-Viewer $keyboardFixture
    Assert-True ($s.binding12 -eq '116') '向上动作自定义 F5 在修复后跨重启保留，不覆盖用户绑定'
    Message $window 0x0100 0x35; Message $window 0x0101 0x35; Start-Sleep -Milliseconds 350
    Message $window 0x0100 0x31; Message $window 0x0101 0x31; Start-Sleep -Milliseconds 350
    $before=Read-State
    Message $window 0x0100 0x57; Message $window 0x0101 0x57; Start-Sleep -Milliseconds 400
    $s=Read-State
    Assert-True ($s.slideX -eq $before.slideX -and $s.slideY -eq $before.slideY) '改绑后原 W 不再触发向上，也不会落入旧硬编码路径'
    Message $window 0x0100 0x74; Message $window 0x0101 0x74; Start-Sleep -Milliseconds 400
    $s=Read-State
    Assert-True ((Number $s 'slideY') -lt (Number $before 'slideY') -and $s.slideX -eq $before.slideX) '自定义 F5 的向上动作同样使用修正后的屏幕方向'
    Write-Host ("键盘平移真实窗口回归通过：$script:passed 项")
}
function Screen-Pixel([int]$X,[int]$Y) {
    [void][ViewingRegressionNative]::SetWindowPos($window,[IntPtr](-1),0,0,0,0,0x0013)
    Start-Sleep -Milliseconds 30
    $origin = New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::ClientToScreen($window,[ref]$origin)
    $bitmap = New-Object Drawing.Bitmap 1,1
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($origin.X+$X,$origin.Y+$Y,0,0,$bitmap.Size)
        return $bitmap.GetPixel(0,0)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
function Verify-PagePixel($State,[string]$Side,[Drawing.Color]$Expected) {
    Start-Sleep -Milliseconds 140
    $x = Number $State ('page'+$Side+'X')
    $y = Number $State ('page'+$Side+'Y')
    $w = Number $State ('page'+$Side+'W')
    $h = Number $State ('page'+$Side+'H')
    $sampleX = [int][Math]::Round([Math]::Max(4,[Math]::Min((Number $State 'width')-4,$x+$w/2)))
    $sampleY = [int][Math]::Round([Math]::Max(4,[Math]::Min((Number $State 'height')-60,$y+$h/2)))
    $color = Screen-Pixel $sampleX $sampleY
    Assert-True ([Math]::Abs($color.R-$Expected.R) -lt 40 -and [Math]::Abs($color.G-$Expected.G) -lt 40 -and
        [Math]::Abs($color.B-$Expected.B) -lt 40) ('双页 '+$Side+' 实际屏幕颜色对应正确文件，而非只检查状态')
}
function Drag([int]$X,[int]$Y,[int]$DX,[int]$DY) {
    $origin = New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::ClientToScreen($window,[ref]$origin)
    [void][ViewingRegressionNative]::SetCursorPos($origin.X+$X,$origin.Y+$Y)
    Start-Sleep -Milliseconds 100
    Message $window 0x0201 1 (($X -band 65535) -bor (($Y -band 65535) -shl 16))
    [void][ViewingRegressionNative]::SetCursorPos($origin.X+$X+$DX,$origin.Y+$Y+$DY)
    Start-Sleep -Milliseconds 80
    Message $window 0x0200 1 ((($X+$DX) -band 65535) -bor ((($Y+$DY) -band 65535) -shl 16))
    Message $window 0x0202 0 ((($X+$DX) -band 65535) -bor ((($Y+$DY) -band 65535) -shl 16))
    Start-Sleep -Milliseconds 320
}

function Set-Pan([int]$Level = -1) {
    $s = Read-State
    $x=[int]((Number $s 'panButtonX')+(Number $s 'panButtonW')/2)
    $y=[int]((Number $s 'panButtonY')+(Number $s 'panButtonH')/2)
    $origin=New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::ClientToScreen($window,[ref]$origin)
    [void][ViewingRegressionNative]::SetCursorPos($origin.X+$x,$origin.Y+$y)
    Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
    $s=Wait-State {param($v) $v.toolbarVisible -eq '1' -or $v.reader -eq '1' -or $v.panPopover -eq '1'} '工具栏未在实际鼠标悬停后显示'
    Click $window $x $y
    $s=Wait-State {param($v) $v.panPopover -eq '1'} '倍率浮层未打开'
    if ($Level -lt 0) {
        Click $window ([int]((Number $s 'panAutoX')+(Number $s 'panAutoW')/2)) ([int]((Number $s 'panAutoY')+(Number $s 'panAutoH')/2))
    } else {
        Click $window ([int]((Number $s 'panTrackX')+(Number $s 'panTrackW')*$Level/11)) ([int]((Number $s 'panTrackY')+(Number $s 'panTrackH')/2))
    }
    Message $window 0x0100 0x1B
    return Wait-State {param($v) $v.panPopover -eq '0' -and $v.panMode -eq $(if($Level -lt 0){'0'}else{'1'}) -and
        ($Level -lt 0 -or [int]$v.panFixed -eq $Level)} '倍率选项未生效'
}

function Pan-Drag([int]$Dx,[int]$Dy,[int]$Flags=1,[int]$Steps=1,[int]$Delay=20) {
    $s=Read-State; $x=[int]((Number $s 'width')/2); $y=[int]((Number $s 'height')/2)
    $origin=New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::ClientToScreen($window,[ref]$origin)
    [void][ViewingRegressionNative]::SetCursorPos($origin.X+$x,$origin.Y+$y)
    Message $window 0x0200 0 (($x -band 65535) -bor (($y -band 65535) -shl 16))
    Start-Sleep -Milliseconds 330
    Message $window 0x0201 $Flags (($x -band 65535) -bor (($y -band 65535) -shl 16))
    if ($ReaderPanOnly) {Start-Sleep -Milliseconds 80; $debug=Read-State; Write-Host ("DOWN 拖动=$($debug.imageDragging)，窗口=$($debug.windowDragging)，锚点=$($debug.readerAnchor)")}
    for($i=1;$i -le $Steps;$i++) {
        Start-Sleep -Milliseconds $Delay
        $mx=$x+[int][Math]::Round($Dx*$i/$Steps); $my=$y+[int][Math]::Round($Dy*$i/$Steps)
        Message $window 0x0200 $Flags (($mx -band 65535) -bor (($my -band 65535) -shl 16))
        if ($ReaderPanOnly) {Start-Sleep -Milliseconds 80; $debug=Read-State; Write-Host ("MOVE 拖动=$($debug.imageDragging)，增量=$($debug.panDeltaY)，次数=$($debug.panMoves)，offset=$($debug.readerOffset)")}
    }
    Message $window 0x0202 0 (($mx -band 65535) -bor (($my -band 65535) -shl 16))
    Start-Sleep -Milliseconds 100
    return Read-State
}

function Test-SensitivitySliders {
    $s=Set-Option 12 0 323
    foreach($kind in @(1,2)) {
        $prefix=if($kind -eq 1){'autoSensitivity'}else{'inertiaSensitivityTrack'}
        $field=if($kind -eq 1){'panSensitivity'}else{'inertiaSensitivity'}
        foreach($level in 0..19) {
            $s=Set-Pan 4
            $x=[int]((Number $s 'panButtonX')+(Number $s 'panButtonW')/2)
            $y=[int]((Number $s 'panButtonY')+(Number $s 'panButtonH')/2)
            Click $window $x $y
            $s=Wait-State {param($v) $v.panPopover -eq '1'} '滑杆测试弹层未打开'
            $sx=[int]((Number $s ($prefix+'X'))+(Number $s ($prefix+'W'))*$level/19)
            $sy=[int]((Number $s ($prefix+'Y'))+(Number $s ($prefix+'H'))/2)
            Click $window $sx $sy
            $s=Wait-State {param($v) [int]$v[$field] -eq $level} '工具栏滑杆档位未更新'
            Assert-True ([int]$s[$field] -eq $level -and $s.panMode -eq '1' -and $s.panFixed -eq '4') ("工具栏 $field 第 $($level+1) 档实际生效，不改固定倍率")
            if($level -eq 19) {Start-Sleep -Milliseconds 120; Save-ClientEvidence $window ('二十档工具栏-'+$kind+'.png')}
            Message $window 0x0100 0x1B
        }
        $s=Set-Option $(if($kind -eq 1){24}else{26}) 7 $(if($kind -eq 1){356}else{363})
        Assert-True ($s[$field] -eq '7') ("设置页滑杆与工具栏 $field 共用配置")
        $s=Set-Pan 4
        Click $window ([int]((Number $s 'panButtonX')+(Number $s 'panButtonW')/2)) ([int]((Number $s 'panButtonY')+(Number $s 'panButtonH')/2))
        $s=Wait-State {param($v) $v.panPopover -eq '1'} '微调测试弹层未打开'
        $sx=[int]((Number $s ($prefix+'X'))+(Number $s ($prefix+'W'))/2)
        $sy=[int]((Number $s ($prefix+'Y'))+(Number $s ($prefix+'H'))/2)
        Click $window $sx $sy
        Message $window 0x0100 0x24; $s=Wait-State {param($v) $v[$field] -eq '0'} 'Home 未选择第一档'
        Message $window 0x0100 0x23; $s=Wait-State {param($v) $v[$field] -eq '19'} 'End 未选择第二十档'
        Message $window 0x0100 0x25; $s=Wait-State {param($v) $v[$field] -eq '18'} '左键微调未减一档'
        Assert-True ($s[$field] -eq '18') ("工具栏 $field 支持 Home／End／方向键微调")
        $moves=$s.panMoves
        foreach($edge in @(-100,[int](Number $s 'width')+100)) {
            Message $window 0x0201 1 (($sx -band 65535) -bor (($sy -band 65535) -shl 16))
            Message $window 0x0200 1 (($edge -band 65535) -bor (($sy -band 65535) -shl 16))
            Message $window 0x0202 0 (($edge -band 65535) -bor (($sy -band 65535) -shl 16))
            $expected=if($edge -lt 0){0}else{19}
            $s=Wait-State {param($v) [int]$v[$field] -eq $expected} '捕获外滑杆未限位'
            Assert-True ($s.panMoves -eq $moves -and [int]$s[$field] -eq $expected) ("工具栏 $field 拖出轨道限位，不穿透拖动图片")
        }
        Message $window 0x0100 0x1B
        $s=Set-Option $(if($kind -eq 1){24}else{26}) 7 $(if($kind -eq 1){356}else{363})
        Stop-Viewer; $s=Start-Viewer $paths[0]
        Assert-True ($s[$field] -eq '7') ("工具栏／设置页 $field 跨重启保存")
    }
    $s=Set-Pan 4
    Click $window ([int]((Number $s 'panButtonX')+(Number $s 'panButtonW')/2)) ([int]((Number $s 'panButtonY')+(Number $s 'panButtonH')/2))
    $s=Wait-State {param($v) $v.panPopover -eq '1'} '惯性开关测试弹层未打开'
    Click $window ([int]((Number $s 'inertiaToggleX')+(Number $s 'inertiaToggleW')/2)) ([int]((Number $s 'inertiaToggleY')+(Number $s 'inertiaToggleH')/2))
    $s=Wait-State {param($v) $v.inertiaEnabled -eq '1'} '工具栏惯性开关未开启'
    Assert-True ($s.inertiaEnabled -eq '1') '工具栏可直接开启惯性，与设置页共用开关'
    Message $window 0x0100 0x1B
    Write-Host ("二十档滑杆真实窗口回归通过：$script:passed 项")
}

function Test-RapidPan {
    $s=Set-Option 12 0 323
    $s=Set-Option 22 0 333
    $s=Set-Pan 4
    $s=Read-State; $x=[int]((Number $s 'width')/2); $y=[int]((Number $s 'height')/2)
    $before=Number $s 'slideY'
    $origin=New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::ClientToScreen($window,[ref]$origin)
    [void][ViewingRegressionNative]::SetCursorPos($origin.X+$x,$origin.Y+$y)
    Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
    Start-Sleep -Milliseconds 330
    for($i=0;$i -lt 2;$i++) {
        Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
        Message $window 0x0201 1 (($x -band 65535) -bor ($y -shl 16))
        Start-Sleep -Milliseconds 30
        Message $window 0x0200 1 (($x -band 65535) -bor (($y-50) -shl 16))
        Message $window 0x0202 0 (($x -band 65535) -bor (($y-50) -shl 16))
        Start-Sleep -Milliseconds 50
    }
    Start-Sleep -Milliseconds 150; $s=Read-State
    Assert-True ($s.shape -eq '0' -and [Math]::Abs((Number $s 'slideY')-$before+100) -le 1) `
        ("TB-24 快速连续拖动不会误触双击或改变窗口形态：形态=$($s.shape)，位移=$((Number $s 'slideY')-$before)，次数=$($s.panMoves)")
}

function Test-PanVisual {
    $s=Set-Option 12 0 323
    foreach($width in @(340,1000)) {
        [void][ViewingRegressionNative]::SetWindowPos($window,[IntPtr]::Zero,20,20,$width,700,0x0014)
        Start-Sleep -Milliseconds 250
        $s=Set-Pan 11
        $x=[int]((Number $s 'panButtonX')+(Number $s 'panButtonW')/2)
        $y=[int]((Number $s 'panButtonY')+(Number $s 'panButtonH')/2)
        Click $window $x $y
        $s=Wait-State {param($v) $v.panPopover -eq '1'} '窄窗倍率浮层未打开'
        Assert-True ((Number $s 'panPanelX') -ge 0 -and (Number $s 'panPanelY') -ge 0 -and
            (Number $s 'panPanelX')+(Number $s 'panPanelW') -le (Number $s 'width') -and
            (Number $s 'panPanelY')+(Number $s 'panPanelH') -le (Number $s 'height')) `
            ("TB-21 宽 $width 像素的靠边窗口浮层不裁剪，9× 可点击")
        Start-Sleep -Milliseconds 100
        Save-ClientEvidence $window ("拖拽浮层-$width.png")
        if ($width -eq 1000) {
            $bitmap=[Drawing.Bitmap]::new((Join-Path $evidence "拖拽浮层-$width.png"))
            try {
                $columns=0
                for($ix=[int](Number $s 'panButtonX')+2;$ix -lt [int](Number $s 'panButtonX')+30;$ix++) {
                    $ink=$false
                    for($iy=[int](Number $s 'panButtonY')+3;$iy -lt [int](Number $s 'panButtonY')+[int](Number $s 'panButtonH')-3;$iy++) {
                        $color=$bitmap.GetPixel($ix,$iy)
                        if($color.R -gt 190 -and $color.G -gt 190 -and $color.B -gt 190) {$ink=$true;break}
                    }
                    if($ink){$columns++}
                }
                # 24 格 viewBox 中手掌约占 16 格；20px 图标扣除抗锯齿边缘应至少占 11 列。
                # 旧的二次缩小只剩约 8px 图标，其手掌最多约 6 列，无法满足此下限。
                Assert-True ($columns -ge 11) ("TB-01 手掌图标使用与现有按钮一致的可读尺寸，不重复缩小：$columns 个着色列")
            } finally {$bitmap.Dispose()}
        }
        Message $window 0x0100 0x1B
    }
    Stop-Viewer
}

function Test-Pan {
    $s=Set-Option 12 0 323
    $s=Set-Option 22 0 333
    $s=Set-Option 14 0 322
    $initialMode=$s.panMode; $initialFixed=$s.panFixed; $initialWidth=$s.width; $initialHeight=$s.height
    $x=[int]((Number $s 'panButtonX')+(Number $s 'panButtonW')/2)
    $y=[int]((Number $s 'panButtonY')+(Number $s 'panButtonH')/2)
    $origin=New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::ClientToScreen($window,[ref]$origin)
    [void][ViewingRegressionNative]::SetCursorPos($origin.X+$x,$origin.Y+$y)
    Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16)); Start-Sleep -Milliseconds 60
    Click $window $x $y
    $s=Wait-State {param($v) $v.panPopover -eq '1'} '初始倍率浮层未打开'
    Assert-True ($s.panMode -eq $initialMode -and $s.panFixed -eq $initialFixed -and
        $s.width -eq $initialWidth -and $s.height -eq $initialHeight) 'TB-01/02 打开手掌浮层不改变模式、固定档位或窗口大小'
    Assert-True ((Number $s 'panPanelX') -ge 0 -and (Number $s 'panPanelY') -ge 0 -and
        (Number $s 'panPanelX')+(Number $s 'panPanelW') -le (Number $s 'width')) 'TB-21 当前显示器倍率浮层完整位于窗口内'
    Message $window 0x0100 0x1B
    $factors=@(.1,.25,.5,.75,1,1.25,1.5,2,3,5,7,9)
    for($level=0;$level -lt 12;$level++) {
        $s=Set-Pan 4
        Message $window 0x0100 0x31
        Start-Sleep -Milliseconds 300
        # 精度测试先回到图像中部，避免正反向动作被边界截断。
        $s=Read-State
        $s=Pan-Drag 0 (-[int](Number $s 'slideY'))
        $s=Set-Pan $level
        $s=Read-State; $before=Number $s 'slideY'
        $s=Pan-Drag 100 -100
        Assert-True ([Math]::Abs((Number $s 'slideY')-$before+100*$factors[$level]) -le 1 -and $s.slideX -eq '0') `
            ("TB-03/15 固定 $($factors[$level])× 移动准确，未溢出水平轴锁定")
        $before=Number $s 'slideY'
        $s=Pan-Drag 0 100 5
        Assert-True ([Math]::Abs((Number $s 'slideY')-$before-25) -le 1 -and [int]$s.panFixed -eq $level) `
            ("TB-10 固定 $($factors[$level])× 的 Shift 临时倍率是 0.25×，原设置不变")
    }
    $s=Set-Pan 0
    $before=Number $s 'slideY'
    $s=Pan-Drag 0 100 1 100 5
    Assert-True ([Math]::Abs((Number $s 'slideY')-$before-10) -le 1) 'TB-04 0.1× 每次移动 1px 的残差正确累计'
    $s=Set-Pan 8
    $before=Number $s 'slideY'; $s=Pan-Drag 0 100 1 10 50
    $slow=(Number $s 'slideY')-$before
    $before=Number $s 'slideY'; $s=Pan-Drag 0 -100 1 10 5
    Assert-True ([Math]::Abs($slow-300) -le 1 -and [Math]::Abs((Number $s 'slideY')-$before+300) -le 1) `
        'TB-05 固定倍率不随拖动速度改变，不叠加自动加速'
    $s=Set-Option 24 0 356
    $before=Number $s 'slideY'; $s=Pan-Drag 0 -50
    Assert-True ([Math]::Abs((Number $s 'slideY')-$before+150) -le 1) 'TB-19 调整自动灵敏度不影响固定 3× 的实际位移'
    $s=Set-Pan -1
    $s=Set-Option 24 8 356
    $before=Number $s 'slideY'; $s=Pan-Drag 0 -400 1 400 20
    Assert-True ([Math]::Abs((Number $s 'slideY')-$before+400) -le 1) 'TB-06/08 自动最高灵敏度慢速长距离仍严格 1:1'
    Message $window 0x0100 0x31; Start-Sleep -Milliseconds 300
    $s=Read-State; $before=Number $s 'slideY'; $s=Pan-Drag 0 -200 1 10 15
    Assert-True ((Number $s 'slideY')-$before -lt -250) 'TB-07 自动快速拖动产生平滑加速'
    $before=Number $s 'slideY'; $s=Pan-Drag 0 100 5
    Assert-True ([Math]::Abs((Number $s 'slideY')-$before-25) -le 1 -and $s.panMode -eq '0') 'TB-10 自动模式也被 Shift 临时 0.25× 覆盖，保存模式不变'
    $s=Read-State; $x=[int]((Number $s 'width')/2); $y=[int]((Number $s 'height')/2)
    Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16)); Start-Sleep -Milliseconds 330
    Message $window 0x0201 1 (($x -band 65535) -bor ($y -shl 16))
    for($i=1;$i -le 8;$i++) {
        Start-Sleep -Milliseconds 20
        Message $window 0x0200 1 (($x -band 65535) -bor (($y-20*$i) -shl 16))
    }
    Start-Sleep -Milliseconds 20
    Message $window 0x0200 1 (($x -band 65535) -bor (($y-150) -shl 16))
    Start-Sleep -Milliseconds 60; $s=Read-State
    Assert-True ([int]$s.panDeltaY -ge 9 -and [int]$s.panDeltaY -le 11) 'TB-09 自动加速手势中反向立即回落到跟手位移'
    Start-Sleep -Milliseconds 200
    Message $window 0x0200 1 (($x -band 65535) -bor (($y-149) -shl 16))
    Start-Sleep -Milliseconds 60; $s=Read-State
    Assert-True ([int]$s.panDeltaY -ge 0 -and [int]$s.panDeltaY -le 2) 'TB-09 自动加速手势中减速恢复 1:1'
    Message $window 0x0202 0 (($x -band 65535) -bor (($y-149) -shl 16))
    $s=Set-Pan 4
    $s=Pan-Drag 0 (-[int](Number $s 'slideY'))
    $s=Read-State; $x=[int]((Number $s 'width')/2); $y=[int]((Number $s 'height')/2)
    $before=Number $s 'slideY'; Start-Sleep -Milliseconds 330
    Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
    Message $window 0x0201 1 (($x -band 65535) -bor ($y -shl 16))
    foreach($part in @(@(100,1),@(200,5),@(300,1))) {
        Message $window 0x0200 $part[1] (($x -band 65535) -bor ((($y-$part[0]) -band 65535) -shl 16))
        Start-Sleep -Milliseconds 60
    }
    Message $window 0x0202 0 (($x -band 65535) -bor ((($y-300) -band 65535) -shl 16))
    Start-Sleep -Milliseconds 100; $s=Read-State
    Assert-True ([Math]::Abs((Number $s 'slideY')-$before+225) -le 1) 'TB-11/12 手势中按下和松开 Shift 只影响后续增量，无位置跳变'
    $before=Number $s 'slideY'; Start-Sleep -Milliseconds 350; $s=Read-State
    Assert-True ((Number $s 'slideY') -eq $before) 'TB-09 松手后没有新增惯性或漂移'
    # 在同一图片放大到双轴均溢出，不能继续无条件锁死长图 X。
    $s=Set-Option 13 1 324
    for($i=0;$i -lt 16;$i++) {Message $window 0x0100 0x26; Message $window 0x0101 0x26; Start-Sleep -Milliseconds 40}
    Start-Sleep -Milliseconds 350; $s=Read-State
    $beforeX=Number $s 'slideX'; $beforeY=Number $s 'slideY'
    $s=Pan-Drag 30 -30
    Assert-True ((Number $s 'right')-(Number $s 'left') -gt (Number $s 'width') -and
        [Math]::Abs((Number $s 'slideX')-$beforeX-30) -le 1 -and [Math]::Abs((Number $s 'slideY')-$beforeY+30) -le 1) `
        'TB-16 长图放大到双轴溢出后两轴均可拖动'
    $s=Pan-Drag 10000 10000
    Assert-True ((Number $s 'left') -le 1 -and (Number $s 'top') -le 1) 'TB-17 拖到边界后图片仍覆盖视口，不露出越界空白'
    # 缩回 100% 后，水平轴不再溢出，应立即居中，而不是等下一次拖动才纠正。
    Message $window 0x0100 0x31; Start-Sleep -Milliseconds 350; $s=Read-State
    Assert-True ($s.zoom -eq '100' -and $s.slideX -eq '0') 'TB-18 缩放变化立即重算轴锁与边界'
    Message $window 0x0100 0x51; Start-Sleep -Milliseconds 350; $s=Read-State
    $beforeX=Number $s 'slideX'; $dx=if($beforeX -lt 0){30}else{-30}
    $s=Pan-Drag $dx 100
    Assert-True ($s.rotation -eq '1' -and $s.slideY -eq '0' -and
        [Math]::Abs((Number $s 'slideX')-$beforeX-$dx) -le 1 -and $s.panFixed -eq '4') `
        'TB-18 旋转后变成横向长图，垂直轴立即锁定，倍率不变'
    Message $window 0x0100 0x45; Start-Sleep -Milliseconds 350
    Test-RapidPan
    # 打开浮层和滑块捕获后经过生产失焦消息，关闭浮层、释放手势并刷新。
    $s=Read-State; $x=[int]((Number $s 'panButtonX')+(Number $s 'panButtonW')/2)
    $y=[int]((Number $s 'panButtonY')+(Number $s 'panButtonH')/2)
    Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
    Click $window $x $y
    $s=Wait-State {param($v) $v.panPopover -eq '1'} '失焦测试浮层未打开'
    $x=[int](Number $s 'panTrackX'); $y=[int]((Number $s 'panTrackY')+(Number $s 'panTrackH')/2)
    Message $window 0x0201 1 (($x -band 65535) -bor ($y -shl 16))
    Message $window 0x0008 0
    $s=Wait-State {param($v) $v.panPopover -eq '0' -and $v.imageDragging -eq '0'} '失焦后浮层或拖拽状态未释放'
    $before=Number $s 'slideY'
    Message $window 0x0200 1 (($x -band 65535) -bor (($y-100) -shl 16))
    Start-Sleep -Milliseconds 100; $s=Read-State
    Assert-True ((Number $s 'slideY') -eq $before -and $s.panPopover -eq '0') 'TB-23 失焦后刷新浮层并释放捕获，无残留拖拽'
    $s=Set-Pan 11; $s=Set-Option 24 2 356
    Stop-Viewer; $s=Start-Viewer $tall
    Assert-True ($s.panMode -eq '1' -and $s.panFixed -eq '11' -and $s.panSensitivity -eq '2') `
        'TB-20 模式、十二档倍率与独立灵敏度跨进程保存，Shift 临时状态不保存'
    $s=Set-Option 19 1 330
    $s=Set-Pan 7
    Start-Sleep -Milliseconds 300
    $s=Read-State
    $before=Number $s 'readerOffset'; $s=Pan-Drag 0 -50
    Assert-True ([Math]::Abs((Number $s 'readerOffset')-$before-100) -le 2) ("连续阅读也使用同一套固定拖拽倍率：$before → $($s.readerOffset)")
    Stop-Viewer
    Write-Host ("拖拽真实窗口回归通过：$script:passed 项")
}

function Inertia-Flick([int]$Flags=1,[int]$Step=20,[int]$Delay=20,[int]$Pause=0,[int]$ReleaseExtra=0,[int]$DxStep=0,[bool]$MidShift=$false,[bool]$FinalSlow=$false,[bool]$CaptureBeforeRelease=$false) {
    $s=Read-State; $releaseCount=[int]$s.panReleases
    $x=[int]((Number $s 'width')/2); $y=[int]((Number $s 'height')/2)
    $origin=New-Object ViewingRegressionNative+POINT
    [void][ViewingRegressionNative]::ClientToScreen($window,[ref]$origin)
    [void][ViewingRegressionNative]::SetCursorPos($origin.X+$x,$origin.Y+$y)
    Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
    Start-Sleep -Milliseconds 330
    Message $window 0x0201 $Flags (($x -band 65535) -bor ($y -shl 16))
    for($i=1;$i -le 8;$i++) {
        Start-Sleep -Milliseconds $Delay
        $moveFlags=if($MidShift -and ($i -eq 4 -or $i -eq 5)){5}else{$Flags}
        Message $window 0x0200 $moveFlags ((($x+$DxStep*$i) -band 65535) -bor (($y-$Step*$i) -shl 16))
        if($MidShift -and $i -eq 5) {
            # 中途按键会清理速度采样，但不能清掉本次手势已使用精细模式的事实。
            Message $window 0x0100 0x10; Message $window 0x0101 0x10
        }
    }
    $lastY=$y-$Step*8
    if($FinalSlow) {
        Start-Sleep -Milliseconds 60; $lastY--
        Message $window 0x0200 $Flags ((($x+$DxStep*8) -band 65535) -bor ($lastY -shl 16))
    }
    if($Pause){Start-Sleep -Milliseconds $Pause}
    if($CaptureBeforeRelease) {$script:beforeReleasePixel=Screen-Pixel $x 180}
    Message $window 0x0202 ($Flags -band 4) ((($x+$DxStep*8) -band 65535) -bor (($lastY-$ReleaseExtra) -shl 16))
    return Wait-State {param($v) [int]$v.panReleases -gt $releaseCount} '甩动释放事件未完成'
}

function Test-Inertia {
    $s=Read-State
    Assert-True ($s.ContainsKey('inertiaEnabled') -and $s.inertiaEnabled -eq '0' -and $s.inertiaSensitivity -eq '4') `
        '惯性默认关闭且灵敏度为第五档，旧拖拽功能不变'
    $s=Set-Option 12 0 323
    $s=Set-Option 22 0 333
    $s=Set-Pan 4
    $beforeY=Number $s 'slideY'; $s=Inertia-Flick 1 20 20 0 20
    Assert-True ([Math]::Abs((Number $s 'inertiaReleaseY')-$beforeY+180) -le 1) '松手坐标补齐最后合并增量，不丢失最后位置'
    $release=Number $s 'inertiaReleaseY'; Start-Sleep -Milliseconds 400; $s=Read-State
    Assert-True ($s.inertiaActive -eq '0' -and (Number $s 'slideY') -eq $release) '默认关闭时快拖松手即停，不改变旧行为'
    $s=Set-Option 25 1 362
    $results=[Collections.Generic.List[object]]::new()
    for($level=0;$level -lt 20;$level++) {
        $s=Set-Option 26 $level 363
        $s=Set-Option 22 0 333
        $starts=[int]$s.inertiaStarts
        $s=Inertia-Flick
        Assert-True ([int]$s.inertiaStarts -gt $starts -and $s.slideX -eq '0') ("惯性第 $($level+1) 档快拖后实际启动，未溢出的 X 轴仍锁定")
        $s=Wait-State {param($v) $v.inertiaActive -eq '0'} '惯性没有在有限时间内停止'
        $distance=(Number $s 'inertiaReleaseY')-(Number $s 'slideY')
        $seconds=(Number $s 'inertiaDuration')
        $velocity=-(Number $s 'inertiaVY')
        Assert-True ($distance -gt 20 -and [Math]::Abs($distance-$velocity*$seconds/3) -le 2) `
            ("惯性第 $($level+1) 档终点符合减速曲线，松手后额外移动 $distance px，不重复乘拖拽倍率")
        $results.Add([pscustomobject]@{档位=$level+1;时长=$seconds;松手速度=$velocity;滑行距离=$distance})
    }
    $results | Export-Csv -LiteralPath (Join-Path $evidence '惯性九档实际位移.csv') -NoTypeInformation -Encoding utf8BOM
    foreach($mode in @(-1,8)) {
        $s=Set-Pan $mode
        $s=Set-Option 22 0 333
        $starts=[int]$s.inertiaStarts; $s=Inertia-Flick
        Assert-True ([int]$s.inertiaStarts -gt $starts) ("拖拽模式 $mode 与松手惯性可独立配合")
        $s=Wait-State {param($v) $v.inertiaActive -eq '0'} '独立拖拽模式惯性未停止'
        $distance=(Number $s 'inertiaReleaseY')-(Number $s 'slideY')
        Assert-True ([Math]::Abs($distance+(Number $s 'inertiaVY')*(Number $s 'inertiaDuration')/3) -le 2) `
            ("拖拽模式 $mode 的松手距离不重复叠乘倍率")
    }
    $s=Set-Pan 11
    $starts=[int]$s.inertiaStarts; $s=Inertia-Flick 1 1 40
    Assert-True ([int]$s.inertiaStarts -eq $starts) '固定 9× 的慢拖不会被图片速度误判成甩动'
    $s=Set-Pan 4
    $s=Set-Option 22 0 333
    $starts=[int]$s.inertiaStarts; $s=Inertia-Flick 1 1 40 0 0 20
    Assert-True ([int]$s.inertiaStarts -eq $starts) '长图锁定 X 轴的快速横移不会让慢速 Y 轴误触惯性'
    $s=Set-Option 22 0 333
    $s=Inertia-Flick 1 20 20 0 0 0 $false $false $true
    $s=Wait-State {param($v) $v.inertiaActive -eq '0'} '像素核验前惯性未结束'
    $afterPixel=Screen-Pixel ([int]((Number $s 'width')/2)) 180
    Assert-True ($script:beforeReleasePixel.R -gt 220 -and $script:beforeReleasePixel.G -lt 30 -and
        [Math]::Abs($afterPixel.R-$afterPixel.G) -lt 5 -and $afterPixel.G -gt 150) `
        '真实屏幕像素从红色标记变为灰色，验证松手惯性确实绘制而非只更新状态'
    Save-ClientEvidence $window '惯性停止后实际画面.png'
    foreach($kind in @('slow','pause','precision','midShift','finalSlow')) {
        $s=Set-Option 22 0 333
        $starts=[int]$s.inertiaStarts
        $s=switch($kind) {
            slow {Inertia-Flick 1 1 40}
            pause {Inertia-Flick 1 20 20 150}
            precision {Inertia-Flick 5}
            midShift {Inertia-Flick 1 20 20 0 0 0 $true}
            finalSlow {Inertia-Flick 1 60 20 0 0 0 $false $true}
        }
        $release=Number $s 'inertiaReleaseY'; Start-Sleep -Milliseconds 300; $s=Read-State
        Assert-True ([int]$s.inertiaStarts -eq $starts -and $s.inertiaActive -eq '0' -and
            (Number $s 'slideY') -eq $release) ("$kind 手势松手即停，不误触惯性")
    }
    foreach($cancel in @('mouse','wheel','key','focus','capture','resize','menu','image','rotation')) {
        $s=Set-Option 22 0 333
        $s=Inertia-Flick
        Assert-True ($s.inertiaActive -eq '1') ("$cancel 打断前惯性确实在运行")
        switch($cancel) {
            mouse {
                $x=[int]((Number $s 'width')/2); $y=[int]((Number $s 'height')/2)
                Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
                Message $window 0x0201 1 (($x -band 65535) -bor ($y -shl 16))
                Message $window 0x0202 0 (($x -band 65535) -bor ($y -shl 16))
            }
            wheel {Message $window 0x020A 7864320}
            key {Message $window 0x0100 0x26; Message $window 0x0101 0x26}
            focus {Message $window 0x0008}
            capture {Message $window 0x0215}
            resize {[void][ViewingRegressionNative]::SetWindowPos($window,[IntPtr]::Zero,0,0,[int]$s.width,[int]$s.height+50,0x0016)}
            menu {Command 1004; Command 1004}
            image {Message $window 0x0100 0x27; Start-Sleep -Milliseconds 150; Message $window 0x0100 0x25}
            rotation {Message $window 0x0100 0x51; Start-Sleep -Milliseconds 350; Message $window 0x0100 0x45}
        }
        $s=Wait-State {param($v) $v.inertiaActive -eq '0'} '输入后惯性未及时停止'
        Start-Sleep -Milliseconds 350; $s=Read-State
        Assert-True ($s.inertiaActive -eq '0') ("$cancel 立即停止惯性且不再复活")
    }
    # 在底边前 200px 开始快拖，释放后仍有约40px可走，应触边即停且不回弹。
    $s=Set-Option 22 0 333
    $s=Set-Option 25 0 362
    $limit=[Math]::Floor(((Number $s 'bottom')-(Number $s 'top')-(Number $s 'height'))/2)
    $s=Pan-Drag 0 ([int](-$limit+200-(Number $s 'slideY')))
    $s=Set-Option 25 1 362
    $s=Inertia-Flick
    $s=Wait-State {param($v) $v.inertiaActive -eq '0'} '触边后惯性没有停止'
    Assert-True ([Math]::Abs((Number $s 'bottom')-(Number $s 'height')) -le 1 -and $s.slideX -eq '0') '惯性到图像底边立即停止，不露空、不越界'
    $edge=Number $s 'slideY'; Start-Sleep -Milliseconds 300; $s=Read-State
    Assert-True ((Number $s 'slideY') -eq $edge) '惯性触边后不回弹、不抖动'
    $starts=[int]$s.inertiaStarts; $s=Inertia-Flick
    Assert-True ([int]$s.inertiaStarts -eq $starts) '已经触边的持续拖动不会产生虚假释放速度'
    $s=Set-Option 26 6 363
    Stop-Viewer; $s=Start-Viewer $tall
    Assert-True ($s.inertiaEnabled -eq '1' -and $s.inertiaSensitivity -eq '6' -and $s.inertiaActive -eq '0') `
        '惯性开关与独立灵敏度跨进程保留，运行中动画不持久化'
    $s=Set-Option 12 0 323
    $s=Set-Option 22 0 333
    $s=Set-Option 19 1 330
    $s=Set-Pan 4
    $starts=[int]$s.inertiaStarts; $s=Inertia-Flick
    Assert-True ([int]$s.inertiaStarts -gt $starts) '连续阅读也能启动相同惯性策略'
    $s=Wait-State {param($v) $v.inertiaActive -eq '0'} '连续阅读惯性未结束'
    Assert-True ((Number $s 'readerOffset') -gt (Number $s 'inertiaReleaseReaderOffset')+20) '连续阅读松手后实际继续向下浏览，不是仅记录启动状态'
    Stop-Viewer
    Write-Host ("惯性真实窗口回归通过：$script:passed 项")
}

function Test-ReaderPan {
    $s=Set-Option 12 0 323
    $s=Set-Option 13 1 324
    $s=Set-Option 19 1 330
    $s=Set-Pan 7
    Start-Sleep -Milliseconds 300
    $s=Read-State; $before=Number $s 'readerOffset'
    $s=Pan-Drag 0 -50
    Assert-True ([Math]::Abs((Number $s 'readerOffset')-$before-100) -le 2) `
        ("阅读拖拽：$before → $($s.readerOffset)，增量 $($s.panDeltaY)，调用 $($s.panMoves)，锚点 $($s.readerAnchor)，脏 $($s.readerDirty)")
    Stop-Viewer
}

function Test-ActualSize {
    Message $window 0x0100 0x70
    $settings=[IntPtr]::Zero; $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do {$settings=[ViewingRegressionNative]::Find($process.Id,'YeImageViewerSettingWnd'); Start-Sleep -Milliseconds 50}
    while($settings -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    $scale=[ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
    Click $settings ([int](169*$scale)) ([int](194*$scale))
    Message $settings 0x0010; Start-Sleep -Milliseconds 400
    $s=Read-State
    Assert-True ($s.settingActualPreferred -eq '1' -and $s.zoom -eq '100') '默认实际大小开关实际改变大图显示为 100%，不是只保存勾选状态'
    Stop-Viewer
    $s=Start-Viewer (Join-Path $bigFolder '02.png')
    Assert-True ($s.settingActualPreferred -eq '1' -and $s.zoom -eq '100') '默认实际大小在另一张大图及重启后真正生效'
    $s=Set-Option 13 0 324
    Assert-True ($s.settingActualPreferred -eq '0' -and [int]$s.zoom -lt 100) '选择适应窗口会同步取消旧实际大小开关，两个入口不会互相覆盖'
    Stop-Viewer
}

function Open-ShortcutSettings {
    Command 1010
    $settings=[IntPtr]::Zero; $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do {$settings=[ViewingRegressionNative]::Find($process.Id,'YeImageViewerSettingWnd'); Start-Sleep -Milliseconds 50}
    while($settings -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if($settings -eq [IntPtr]::Zero) {throw '快捷键审计窗口未打开'}
    $scale=[ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
    Click $settings ([int](387*$scale)) ([int](26*$scale))
    return $settings
}

function Test-ShortcutAudit {
    $report=[Collections.Generic.List[object]]::new()
    # 界面顺序不是枚举存储顺序，从两个生产声明建立映射，不能手抄一张顺序表。
    $configSource=Get-Content (Join-Path $PSScriptRoot '../YeImageViewer/include/ShortcutConfig.h') -Raw
    $settingSource=Get-Content (Join-Path $PSScriptRoot '../YeImageViewer/include/Setting.h') -Raw
    $enumBody=[regex]::Match($configSource,'enum class Action : uint32_t\s*\{([\s\S]*?)\};').Groups[1].Value
    $actionNumbers=@{}; $number=0
    foreach($name in ($enumBody -split ',')) {
        $name=$name.Trim()
        if($name -and $name -ne 'Count') {$actionNumbers[$name]=$number; $number++}
    }
    $rows=@([regex]::Matches($settingSource,'ShortcutItem\{\s*ShortcutConfig::Action::(\w+),') | ForEach-Object {$_.Groups[1].Value})
    Assert-True ($rows.Count -eq 34 -and $actionNumbers.Count -eq 34 -and (@($rows | Select-Object -Unique).Count -eq 34)) '快捷键审计清单覆盖全部生产界面动作且无重复'
    for($bindingIndex=0;$bindingIndex -lt $rows.Count;$bindingIndex++) {
        $settings=Open-ShortcutSettings
        $scale=[ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
        $natural=52+288+40*$bindingIndex+20
        $steps=[Math]::Max(0,[int][Math]::Ceiling(($natural-500)/72.0))
        for($step=0;$step -lt $steps;$step++) {Message $settings 0x020A 4287102976}
        $cy=[int](($natural-$steps*72)*$scale)
        Click $settings ([int](417*$scale)) $cy
        Message $settings 0x0100 0x74
        Message $settings 0x0101 0x74
        Message $settings 0x0010; Start-Sleep -Milliseconds 200
        $field='binding'+$actionNumbers[$rows[$bindingIndex]]
        $s=Wait-State {param($v) [int]$v[$field] -eq 116} ('快捷键行 '+$bindingIndex+' 未录入 F5')
        Stop-Viewer; $s=Start-Viewer $paths[1]
        Assert-True ([int]$s[$field] -eq 116) ('快捷键行 '+$bindingIndex+' 改绑、换图与重启后恢复 F5')
        $settings=Open-ShortcutSettings
        for($step=0;$step -lt $steps;$step++) {Message $settings 0x020A 4287102976}
        Click $settings ([int](563*$scale)) $cy
        Message $settings 0x0010; Start-Sleep -Milliseconds 200
        $s=Wait-State {param($v) $v[$field] -eq '0'} ('快捷键行 '+$bindingIndex+' 未清空')
        Stop-Viewer; $s=Start-Viewer $paths[0]
        Assert-True ($s[$field] -eq '0') ('快捷键行 '+$bindingIndex+' 清空值跨进程保存')
        $report.Add([pscustomobject]@{类型='快捷键';项目=$rows[$bindingIndex];选项='F5 / 未绑定';改绑重启='通过';清空重启='通过';行为='专门回归'})
    }
    # 恢复默认必须重新提供 F1/F3 等入口；后续使用固定菜单命令不依赖改绑。
    $settings=Open-ShortcutSettings
    $scale=[ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
    Click $settings ([int](494*$scale)) ([int](274*$scale))
    Message $settings 0x0010; Start-Sleep -Milliseconds 200
    $s=Read-State
    Assert-True ($s.binding27 -eq '114' -and $s.wheel0 -eq '1' -and $s.wheel1 -eq '0' -and $s.wheel2 -eq '2') '快捷键页恢复默认同时还原键盘与滚轮'
    $s=Set-Pan 11
    for($modifier=0;$modifier -lt 3;$modifier++) {
        for($wheelChoice=0;$wheelChoice -lt 4;$wheelChoice++) {
            $settings=Open-ShortcutSettings
            $scale=[ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
            $s=Read-State; $field='wheel'+$modifier
            $current=[int]$s[$field]
            $clicks=($wheelChoice-$current+4)%4
            for($clickIndex=0;$clickIndex -lt $clicks;$clickIndex++) {
                Click $settings ([int](436*$scale)) ([int]((52+93+42*$modifier)*$scale))
            }
            Message $settings 0x0010; Start-Sleep -Milliseconds 200
            $s=Wait-State {param($v) [int]$v[$field] -eq $wheelChoice} '滚轮设置未切换'
            Stop-Viewer; $s=Start-Viewer $paths[1]
            Assert-True ([int]$s[$field] -eq $wheelChoice) ("滚轮组合 $modifier 的动作 $wheelChoice 换图并重启后保留")
            $before=$s
            $x=[int]((Number $s 'width')/2); $y=[int]((Number $s 'height')/2)
            Message $window 0x0200 0 (($x -band 65535) -bor ($y -shl 16))
            $flags=@(0,8,4)[$modifier]
            Message $window 0x020A (7864320 -bor $flags)
            Start-Sleep -Milliseconds 350; $s=Read-State
            $panStep=[Math]::Floor(((Number $before 'width')+(Number $before 'height'))/16)
            $worked=switch($wheelChoice) {
                0 {(Number $s 'zoom') -gt (Number $before 'zoom')}
                1 {[Math]::Abs((Number $s 'slideY')-(Number $before 'slideY')-$panStep) -le 2}
                2 {[Math]::Abs((Number $s 'slideX')-(Number $before 'slideX')-$panStep) -le 2}
                3 {$s.image -ne $before.image}
            }
            Assert-True ([bool]$worked -and $s.panMode -eq '1' -and $s.panFixed -eq '11') `
                ("TB-13/14/22 滚轮组合 $modifier 的动作 $wheelChoice 实际生效，不受 9× 拖拽或 Shift 精细模式影响")
            $report.Add([pscustomobject]@{类型='滚轮';项目=$modifier;选项=$wheelChoice;改绑重启='通过';清空重启='不适用';行为='通过'})
        }
    }
    Stop-Viewer
    $report | Export-Csv -LiteralPath (Join-Path $evidence '快捷键与滚轮持久化审计.csv') -NoTypeInformation -Encoding utf8BOM
    Write-Host ("快捷键／滚轮设置审计通过：$script:passed 项断言；动作行为由单独用例验证。")
}

function Test-SettingsAudit {
    $counts=@(3,3,3,2,2,4,2,2,2,4,2,2,3,3,2,2,2,3,3,2,2,2,2,2,20,2,20)
    $fields=@('settingSwitchAnimation','settingTheme','settingLanguage','settingRightClick',
        'pref325','pref259','pref258','pref261','pref263','pref262','pref264','pref265',
        'pref323','pref324','pref322','pref326','pref327','pref328','pref329','pref330',
        'pref331','pref332','pref333','pref334','pref356','pref362','pref363')
    $checks=@('settingRotateAnimation','settingZoomAnimation','settingDeleteConfirm','settingColor',
        'settingActualPreferred','settingRememberMonitor')
    $report=[Collections.Generic.List[object]]::new()
    for($index=0;$index -lt $checks.Count;$index++) {
        foreach($choice in @(0,1)) {
            $s=Read-State
            if ([int]$s[$checks[$index]] -ne $choice) {
                Message $window 0x0100 0x70
                $settings=[IntPtr]::Zero; $deadline=[DateTime]::UtcNow.AddSeconds(5)
                do {$settings=[ViewingRegressionNative]::Find($process.Id,'YeImageViewerSettingWnd'); Start-Sleep -Milliseconds 50}
                while($settings -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
                if($settings -eq [IntPtr]::Zero) {throw '设置审计窗口未打开'}
                $scale=[ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
                Click $settings ([int]((169+280*($index%2))*$scale)) ([int]((52+70+36*[Math]::Floor($index/2))*$scale))
                Message $settings 0x0010
                Start-Sleep -Milliseconds 250
                $s=Wait-State {param($v) [int]$v[$checks[$index]] -eq $choice} ('复选项未更新：'+$checks[$index])
            }
            Assert-True ([int]$s[$checks[$index]] -eq $choice) ($checks[$index]+' 关闭设置后保留 '+$choice)
            Message $window 0x0100 0x24; Start-Sleep -Milliseconds 200
            Message $window 0x0100 0x27; Start-Sleep -Milliseconds 250
            $s=Read-State
            Assert-True ([int]$s[$checks[$index]] -eq $choice) ($checks[$index]+' 切图后保留 '+$choice)
            Stop-Viewer; $s=Start-Viewer $paths[0]
            Assert-True ([int]$s[$checks[$index]] -eq $choice) ($checks[$index]+' 退出重启后保留 '+$choice)
            $report.Add([pscustomobject]@{类型='复选项';项目=$checks[$index];选项=$choice;关闭设置='通过';切图='通过';重启='通过'})
        }
    }
    for($row=0;$row -lt $counts.Count;$row++) {
        for($choice=0;$choice -lt $counts[$row];$choice++) {
            $s=Set-Option $row $choice 0 $fields[$row]
            Message $window 0x0100 0x24; Start-Sleep -Milliseconds 160
            Message $window 0x0100 0x27; Start-Sleep -Milliseconds 250
            $s=Read-State
            Assert-True ([int]$s[$fields[$row]] -eq $choice) ('单选行 '+$row+' 切图后保留 '+$choice)
            Stop-Viewer; $s=Start-Viewer $paths[0]
            Assert-True ([int]$s[$fields[$row]] -eq $choice) ('单选行 '+$row+' 退出重启后保留 '+$choice)
            $report.Add([pscustomobject]@{类型='单选项';项目=$fields[$row];选项=$choice;关闭设置='通过';切图='通过';重启='通过'})
        }
    }
    Stop-Viewer
    $report | Export-Csv -LiteralPath (Join-Path $evidence '全部常规设置持久化审计.csv') -NoTypeInformation -Encoding utf8BOM
    Write-Host ("常规设置审计完成：$($report.Count) 个选项组合，$script:passed 项断言；业务行为另由专门回归验证。")
}

function Test-AssociationDpi {
    Message $window 0x0100 0x70
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $settings = [ViewingRegressionNative]::Find($process.Id,'YeImageViewerSettingWnd')
        if ($settings -ne [IntPtr]::Zero) {break}
        Start-Sleep -Milliseconds 60
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($settings -eq [IntPtr]::Zero) {throw 'DPI 回归设置窗口未打开'}
    [ViewingRegressionNative]::Activate($settings)
    $client = New-Object ViewingRegressionNative+RECT
    $outer = New-Object ViewingRegressionNative+RECT
    [void][ViewingRegressionNative]::GetClientRect($settings,[ref]$client)
    [void][ViewingRegressionNative]::GetWindowRect($settings,[ref]$outer)
    $frameW = $outer.Right-$outer.Left-$client.Right
    $frameH = $outer.Bottom-$outer.Top-$client.Bottom
    $baseColor = $null
    $wheelBaseColor = $null
    foreach ($dpi in @(96,120,144,192)) {
        $rect = New-Object ViewingRegressionNative+RECT
        $rect.Left=20; $rect.Top=20
        $rect.Right=20+[int](620*$dpi/96)+$frameW
        $rect.Bottom=20+[int](620*$dpi/96)+$frameH
        $memory = [Runtime.InteropServices.Marshal]::AllocHGlobal([Runtime.InteropServices.Marshal]::SizeOf($rect))
        try {
            [Runtime.InteropServices.Marshal]::StructureToPtr($rect,$memory,$false)
            Message $settings 0x02E0 ($dpi -bor ($dpi -shl 16)) $memory.ToInt64()
        } finally {[Runtime.InteropServices.Marshal]::FreeHGlobal($memory)}
        Start-Sleep -Milliseconds 350
        $scale = $dpi/96.0
        Click $settings ([int](232*$scale)) ([int](26*$scale))
        Start-Sleep -Milliseconds 250
        if ($dpi -eq 96) {
            # 仅选择全格式，不点击关联，不修改用户注册表。
            Click $settings 233 486
            Start-Sleep -Milliseconds 250
        }
        $origin = New-Object ViewingRegressionNative+POINT
        [void][ViewingRegressionNative]::ClientToScreen($settings,[ref]$origin)
        $bmp = [Drawing.Bitmap]::new(1,1)
        $g = [Drawing.Graphics]::FromImage($bmp)
        try {
            # 最后一行第十个标签底部内侧，避开字体、圆角及边框。
            $x=[int](474*$scale); $y=[int](367*$scale)
            $g.CopyFromScreen($origin.X+$x,$origin.Y+$y,0,0,$bmp.Size)
            $color=$bmp.GetPixel(0,0)
            if ($null -eq $baseColor) {$baseColor=$color}
            Assert-True ([Math]::Abs($color.R-$baseColor.R) -le 3 -and [Math]::Abs($color.G-$baseColor.G) -le 3 -and
                [Math]::Abs($color.B-$baseColor.B) -le 3) ("文件关联网格在 $dpi DPI 按物理像素正确绘制，最后一行标签可见")
        } finally {$g.Dispose(); $bmp.Dispose()}
        Save-ClientEvidence $settings ("文件关联-$dpi-DPI.png")
        # 点击实际显示的“立即关联”，真实生产注册只写进该进程的 HKCU 沙箱。
        $position=([int](531*$scale) -band 65535) -bor ([int](486*$scale) -shl 16)
        [void][ViewingRegressionNative]::PostMessage($settings,0x0201,[UIntPtr]1,[IntPtr]$position)
        [void][ViewingRegressionNative]::PostMessage($settings,0x0202,[UIntPtr]0,[IntPtr]$position)
        $dialog=[IntPtr]::Zero; $deadline=[DateTime]::UtcNow.AddSeconds(5)
        do {$dialog=[ViewingRegressionNative]::Find($process.Id,'#32770'); Start-Sleep -Milliseconds 50}
        while($dialog -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
        Assert-True ($dialog -ne [IntPtr]::Zero) ("$dpi DPI 点击可见关联按钮会显示结果对话框")
        $root=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($registrySandbox+'\Software\YeImageViewer\Capabilities\FileAssociations')
        try {Assert-True ($null -ne $root -and $root.GetValue('.png') -eq 'YeImageViewer.ImageFile.png') `
            ("$dpi DPI 关联按钮实际登记 PNG 能力，非只检测弹框")} finally {if($root){$root.Dispose()}}
        $no=[ViewingRegressionNative]::GetDlgItem($dialog,7)
        if ($no -eq [IntPtr]::Zero) {throw '关联结果缺少取消打开系统设置的按钮'}
        Message $no 0x00F5
        Start-Sleep -Milliseconds 100
        Click $settings ([int](387*$scale)) ([int](26*$scale))
        Start-Sleep -Milliseconds 200
        $wheelBitmap=[Drawing.Bitmap]::new(1,1)
        $wheelGraphics=[Drawing.Graphics]::FromImage($wheelBitmap)
        try {
            $wheelGraphics.CopyFromScreen($origin.X+[int](540*$scale),$origin.Y+[int](144*$scale),0,0,$wheelBitmap.Size)
            $wheelColor=$wheelBitmap.GetPixel(0,0)
            if($null -eq $wheelBaseColor) {$wheelBaseColor=$wheelColor}
            Assert-True ([Math]::Abs($wheelColor.R-$wheelBaseColor.R) -le 3 -and
                [Math]::Abs($wheelColor.G-$wheelBaseColor.G) -le 3 -and [Math]::Abs($wheelColor.B-$wheelBaseColor.B) -le 3) `
                ("$dpi DPI 快捷键页滚轮选项的右侧卡片完整缩放，不挤在左上角")
        } finally {$wheelGraphics.Dispose(); $wheelBitmap.Dispose()}
    }
    Message $settings 0x0010
    Stop-Viewer
}

try {
    [void](New-Item -ItemType Directory -Path $testRoot)
    [void](New-Item -ItemType Directory -Path $evidence -Force)
    $testViewer = Join-Path $testRoot 'YeImageViewer.exe'
    Copy-Item -LiteralPath $Viewer -Destination $testViewer
    $pictures = Join-Path $testRoot 'pages'
    [void](New-Item -ItemType Directory -Path $pictures)
    $colors = @([Drawing.Color]::FromArgb(220,40,40),[Drawing.Color]::FromArgb(40,220,40),
        [Drawing.Color]::FromArgb(40,40,220),[Drawing.Color]::FromArgb(220,180,40),
        [Drawing.Color]::FromArgb(180,40,220),[Drawing.Color]::FromArgb(40,180,220))
    $paths = @()
    for ($i=0;$i -lt 6;$i++) {
        $path = Join-Path $pictures ('{0:00}.png' -f ($i+1))
        $paths += $path
        $bitmap = [Drawing.Bitmap]::new($(if ($i%2) {800} else {1200}),$(if ($i%2) {1200} else {900}))
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try { $graphics.Clear($colors[$i]); $bitmap.Save($path,[Drawing.Imaging.ImageFormat]::Png) }
        finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
    $tall = Join-Path $pictures '07-long.png'
    $bitmap = New-Object Drawing.Bitmap 300,6000
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([Drawing.Color]::Silver)
        $graphics.FillRectangle([Drawing.Brushes]::Red,0,0,300,$(if($InertiaOnly){400}else{200}))
        $graphics.FillRectangle([Drawing.Brushes]::Blue,0,5800,300,200)
        $bitmap.Save($tall,[Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
    $small = Join-Path $pictures '08-small.png'
    $bitmap = New-Object Drawing.Bitmap 40,30
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try { $graphics.Clear([Drawing.Color]::Yellow); $bitmap.Save($small,[Drawing.Imaging.ImageFormat]::Png) }
    finally { $graphics.Dispose(); $bitmap.Dispose() }
    $animated = Join-Path $pictures '09-animated.gif'
    Copy-Item -LiteralPath $Animation -Destination $animated
    $env:YEIMAGEVIEWER_INTERACTION_PROBE = $statePath
    $env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR = '1'
    if ($SettingsDpiOnly) {$env:YEIMAGEVIEWER_TEST_REGISTRY_SANDBOX=$registrySandbox}

    $s = Start-Viewer $paths[0]
    if ($SettingsDpiOnly) {Test-AssociationDpi; return}
    if ($PanOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-Pan; return}
    if ($KeyboardPanOnly -or $KeyboardSmoothOnly) {
        Stop-Viewer
        $keyboardFixture=Join-Path $pictures '10-keyboard-directions.svg'
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/pan/keyboard-directions.svg') -Destination $keyboardFixture
        $s=Start-Viewer $keyboardFixture
        if($KeyboardSmoothOnly) {Test-KeyboardSmooth} else {Test-KeyboardPan}
        return
    }
    if ($SettingsAuditOnly) {Test-SettingsAudit; return}
    if ($SensitivitySlidersOnly) {Test-SensitivitySliders; return}
    if ($ReaderPanOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-ReaderPan; return}
    if ($RapidPanOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-RapidPan; return}
    if ($PanVisualOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-PanVisual; return}
    if ($InertiaOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-Inertia; return}
    if ($InertiaAxesOnly) {
        Stop-Viewer; $s=Start-Viewer $tall
        $s=Set-Option 12 0 323; $s=Set-Option 22 0 333; $s=Set-Pan 4; $s=Set-Option 25 1 362
        $starts=[int]$s.inertiaStarts; $s=Inertia-Flick 1 1 40 0 0 20
        Assert-True ([int]$s.inertiaStarts -eq $starts) '锁定轴横移不贡献慢速纵向的甩动速度'
        return
    }
    if ($InertiaDecelerationOnly) {
        Stop-Viewer; $s=Start-Viewer $tall
        $s=Set-Option 12 0 323; $s=Set-Option 22 0 333; $s=Set-Pan 4; $s=Set-Option 25 1 362
        $starts=[int]$s.inertiaStarts; $s=Inertia-Flick 1 60 20 0 0 0 $false $true
        Assert-True ([int]$s.inertiaStarts -eq $starts) '最后明确减速的手势不被之前的快速历史误判为甩动'
        return
    }
    if ($InertiaPrecisionOnly) {
        Stop-Viewer; $s=Start-Viewer $tall
        $s=Set-Option 12 0 323; $s=Set-Option 22 0 333; $s=Set-Pan 4; $s=Set-Option 25 1 362
        $starts=[int]$s.inertiaStarts; $s=Inertia-Flick 1 20 20 0 0 0 $true
        Assert-True ([int]$s.inertiaStarts -eq $starts) '中途按键不能使已使用 Shift 的精细手势误触惯性'
        return
    }
    if ($ShortcutAuditOnly) {Test-ShortcutAudit; return}
    if ($ActualSizeOnly) {
        Stop-Viewer
        $bigFolder=Join-Path $testRoot 'big-pages'
        [void](New-Item -ItemType Directory -Path $bigFolder)
        foreach($index in @(1,2)) {
            $bitmap=[Drawing.Bitmap]::new(4000,3000)
            $graphics=[Drawing.Graphics]::FromImage($bitmap)
            try {$graphics.Clear([Drawing.Color]::Silver); $bitmap.Save((Join-Path $bigFolder ('{0:00}.png' -f $index)),[Drawing.Imaging.ImageFormat]::Png)}
            finally {$graphics.Dispose(); $bitmap.Dispose()}
        }
        $s=Start-Viewer (Join-Path $bigFolder '01.png')
        Test-ActualSize; return
    }
    Assert-True ((Expected-FitPercent 800 600 1600 1200) -eq 50 -and (Expected-FitPercent 2000 1400 1600 1200) -eq 100) '适应窗口期望采用浮点比例，小窗口为 50% 而非整数舍入'
    $s = Set-Option 12 1 323
    $s = Set-Option 13 0 324
    $s = Set-Option 14 1 322
    Message $window 0x0100 0x27
    $s = Wait-State {param($v) $v.image -eq '2' -and $v.loading -eq '0' -and $v.viewSettled -eq '1'} '锁定后的下一张未加载或观看动画未完成'
    $expected = Expected-FitPercent (Number $s 'width') (Number $s 'height') 800 1200
    Assert-True ($s.shape -eq '1' -and [Math]::Abs((Number $s 'zoom')-$expected) -le 1) '切图保持沉浸和适应窗口，按新分辨率计算比例'
    Message $window 0x0100 0x26
    Message $window 0x0101 0x26
    Start-Sleep -Milliseconds 350
    $larger = Read-State
    Assert-True ((Number $larger 'zoom') -gt $expected) '手动放大当前图片可用'
    Message $window 0x0100 0x27
    $s = Wait-State {param($v) $v.image -eq '3' -and $v.loading -eq '0' -and $v.viewSettled -eq '1'} '切图未恢复锁定规则或观看动画未完成'
    $expected = Expected-FitPercent (Number $s 'width') (Number $s 'height') 1200 900
    Assert-True ([Math]::Abs((Number $s 'zoom')-$expected) -le 1) '临时缩放不会污染下一张的锁定规则'
    $s = Set-Option 12 2 323
    Assert-True ($s.shape -eq '2') '全屏作为独立窗口形态可选'
    $s = Set-Option 12 1 323
    Stop-Viewer
    $s = Start-Viewer $paths[1]
    Assert-True ($s.lock -eq '1' -and $s.shape -eq '1') '锁定形态跨进程重启保留'

    $s = Set-Option 16 0 327
    Message $window 0x0100 0x24
    $s = Wait-State {param($v) $v.image -eq '1' -and $v.loading -eq '0'} 'Home 未到第一张'
    Message $window 0x0100 0x25
    Start-Sleep -Milliseconds 160
    Assert-True ((Read-State).image -eq '1') '关闭循环后第一张不会跳到末尾'
    Message $window 0x0100 0x23
    $s = Wait-State {param($v) $v.image -eq $v.count -and $v.loading -eq '0'} 'End 未到最后一张'
    Message $window 0x0100 0x27
    Start-Sleep -Milliseconds 160
    Assert-True ((Read-State).image -eq (Read-State).count) '关闭循环后最后一张不会跳到开头'
    $s = Set-Option 12 0 323
    Command 1004
    $s = Wait-State {param($v) $v.info -eq '1' -and $v.histogram -eq '1'} '普通窗口直方图仍隐藏'
    Assert-True ($s.shape -eq '0' -and $s.footer -eq '0') '普通窗口信息面板显示直方图，底栏及占位均移除'
    Command 1004
    Stop-Viewer

    $s = Start-Viewer $tall
    $s = Set-Option 22 0 333
    $s = Set-Option 23 1 334
    $s = Set-Pan 8
    Assert-True ($s.long -eq '1' -and $s.zoom -eq '100' -and [Math]::Abs((Number $s 'top')) -le 1) '长图原始比例 100% 从顶部开始，不按窗口宽度拉伸'
    $s = Read-State
    $beforeTop = Number $s 'top'
    Drag ([int]((Number $s 'width')/2)) ([int]((Number $s 'height')/2)) 30 -40
    $after = Read-State
    Assert-True ($after.slideX -eq '0' -and [Math]::Abs((Number $after 'top')-$beforeTop+120) -le 3) '长图斜向拖动保持水平居中，3 倍加速只增加纵向位移'
    $s = Set-Pan 4
    $beforeTop = Number $s 'top'
    Drag ([int]((Number $s 'width')/2)) ([int]((Number $s 'height')/2)) 20 -40
    $after = Read-State
    Assert-True ($after.slideX -eq '0' -and [Math]::Abs((Number $after 'top')-$beforeTop+40) -le 3) '1 倍拖拽恢复 1:1，修改加速选项不重置当前取景'
    $s = Set-Pan 7
    $beforeTop = Number $s 'top'
    Drag ([int]((Number $s 'width')/2)) ([int]((Number $s 'height')/2)) 20 -40
    $after = Read-State
    Assert-True ($after.slideX -eq '0' -and [Math]::Abs((Number $after 'top')-$beforeTop+80) -le 3) '2 倍拖拽产生双倍位移'
    $beforeTop = Number $after 'top'
    [void][ViewingRegressionNative]::SetWindowPos($window,[IntPtr]::Zero,0,0,[int]$after.width,[int]$after.height-100,0x0016)
    $s = Wait-State {param($v) $v.height -ne $after.height} '长图调整窗口尺寸未生效'
    Assert-True ([Math]::Abs((Number $s 'top')-$beforeTop) -le 2 -and $s.zoom -eq '100') '窗口变高或变矮保留长图取景和原始比例，不跳回顶部'
    Message $window 0x0100 0x26
    Message $window 0x0101 0x26
    Start-Sleep -Milliseconds 320
    $s = Read-State
    Drag ([int]((Number $s 'width')/2)) ([int]((Number $s 'height')/2)) 30 -20
    Assert-True ((Read-State).slideX -eq '0') '长图手动放大后斜向拖动仍沿窗口中心线'
    $s = Set-Option 22 1 333
    Assert-True ([int]$s.zoom -lt 100 -and (Number $s 'top') -ge -1) '长图可选择整图适应窗口'
    $s = Set-Option 14 0 322
    Stop-Viewer
    $s = Start-Viewer $tall
    Assert-True ($s.lock -eq '0' -and [int]$s.zoom -lt 100 -and (Number $s 'top') -ge -1) '长图适应窗口跨重启生效，不依赖观看锁定'
    $s = Set-Option 14 1 322
    Stop-Viewer

    $s = Start-Viewer $small
    $s = Set-Option 15 0 326
    Assert-True ($s.zoom -eq '100') '默认适应窗口不放大小图'
    $s = Set-Option 15 1 326
    Assert-True ([int]$s.zoom -gt 100) '开启延展小影像后保持比例放大到适应窗口'
    Message $window 0x0100 0x31
    $s = Wait-State {param($v) $v.zoom -eq '100'} '实际大小命令未回到 100%'
    Assert-True ($s.zoom -eq '100') '实际大小仍为 100%，不受小图放大选项影响'
    $s = Set-Option 13 0 324
    $s = Set-Option 15 0 326
    $s = Set-Option 17 0 328
    $s = Set-Option 17 1 328
    $s = Set-Option 17 2 328
    Stop-Viewer

    $s = Start-Viewer $animated
    $s = Set-Option 18 2 329
    Assert-True ([int]$s.frames -gt 1 -and $s.frameBar -eq '1') '动图帧号与进度条常显'
    Message $window 0x0100 0x4C
    $s = Wait-State {param($v) $v.paused -eq '1'} '逐帧操作没有自动暂停'
    Assert-True ($s.paused -eq '1') '播放时按下一帧会自动暂停'
    $padding = [int](12*(Number $s 'dpi')/96)
    $left = [int]$s.frameBarX + $padding
    $width = [int]$s.frameBarW - $padding*2 - [int](80*(Number $s 'dpi')/96)
    $y = [int]$s.frameBarY + [int]((Number $s 'frameBarH')*0.75)
    Click $window ($left+$width-1) $y
    $s = Wait-State {param($v) $v.frame -eq $v.frames -and $v.paused -eq '1'} '进度条未定位到末帧'
    Assert-True ($s.frame -eq $s.frames) '帧进度条准确定位最后一帧且保持暂停'
    Click $window $left $y
    $s = Wait-State {param($v) $v.frame -eq '1'} '进度条未定位到首帧'
    Message $window 0x0100 0x4B
    $s = Wait-State {param($v) $v.paused -eq '0'} 'K 未恢复播放'
    Assert-True ($s.paused -eq '0') '从所选帧继续按原始帧时长播放'
    $s = Set-Option 18 0 329
    Assert-True ($s.frameBar -eq '0') '静态隐藏选项关闭动图控制栏'
    $s = Set-Option 18 1 329
    Stop-Viewer

    $s = Start-Viewer $paths[1]
    $s = Set-Option 12 1 323
    $s = Set-Option 19 1 330
    $s = Set-Option 20 0 331
    $s = Set-Option 21 1 332
    Command 1306
    $s = Wait-State {param($v) $v.pageLeftIndex -eq '1' -and $v.pageRightIndex -eq '2' -and $v.readerWaiting -eq '0'} '当前第 2 页放右未得到 1/2 配对'
    Verify-PagePixel $s 'Left' $colors[0]
    Verify-PagePixel $s 'Right' $colors[1]
    Command 1305
    $s = Wait-State {param($v) $v.pageLeftIndex -eq '2' -and $v.pageRightIndex -eq '3' -and $v.readerWaiting -eq '0'} '当前第 2 页放左未得到 2/3 配对'
    Assert-True ($s.image -eq '2' -and [int]$s.group -gt 0) '配对纠正保持当前页，不跳回第一页'
    Verify-PagePixel $s 'Left' $colors[1]
    Verify-PagePixel $s 'Right' $colors[2]
    $initialOffset = Number $s 'readerOffset'
    Message $window 0x020A 4287102976
    $s = Wait-State {param($v) (Number $v 'readerOffset') -gt $initialOffset} '纵向滚轮未连续移动画布'
    Assert-True ((Number $s 'readerOffset') -gt $initialOffset) '纵向滚轮连续滚动，而非硬切图'
    $expectedBack = [Math]::Max(0.0,(Number $s 'readerOffset')-((Number $s 'height')-52)/5.0)
    Message $window 0x020A 7864320
    $s = Wait-State {param($v) [Math]::Abs((Number $v 'readerOffset')-$expectedBack) -lt 2} '反向滚轮没有按视口步长及边界返回'
    $s = Set-Option 20 1 331
    $s = Wait-State {param($v) $v.axis -eq '1' -and $v.readerWaiting -eq '0'} '横向连续阅读未完成布局'
    Assert-True ($s.pageLeftIndex -eq '2' -and $s.pageRightIndex -eq '3') '横向切换保留当前页和配对'
    Command 1308
    $s = Wait-State {param($v) $v.image -eq '4' -and $v.readerWaiting -eq '0'} '下一组未移到 4/5'
    Assert-True ($s.pageLeftIndex -eq '4' -and $s.pageRightIndex -eq '5') '双页下一组按配对前进，不重复当前页'
    $x = [int]((Number $s 'pageRightX')+(Number $s 'pageRightW')/2)
    $y = [int]((Number $s 'pageRightY')+(Number $s 'pageRightH')/2)
    Click $window $x $y
    $s = Wait-State {param($v) $v.image -eq '5' -and $v.loading -eq '0'} '点击右页没有选定对应文件'
    Assert-True ($s.image -eq '5' -and $s.shape -eq '1') '点击页只切换当前文件，不退出沉浸'
    $initialOffset = Number $s 'readerOffset'
    Message $window 0x020A 4287102976
    $s = Wait-State {param($v) (Number $v 'readerOffset') -gt $initialOffset} '横向滚轮未连续移动画布'
    Assert-True ((Number $s 'readerOffset') -gt $initialOffset) '横向滚轮沿阅读轴连续滚动'
    $expectedBack = [Math]::Max(0.0,(Number $s 'readerOffset')-(Number $s 'width')/5.0)
    Message $window 0x020A 7864320
    $s = Wait-State {param($v) [Math]::Abs((Number $v 'readerOffset')-$expectedBack) -lt 2} '横向反向滚轮没有按视口步长及边界返回'
    Command 1006
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $dialog = [ViewingRegressionNative]::Find($process.Id,'#32770')
        if ($dialog -ne [IntPtr]::Zero) {break}
        Start-Sleep -Milliseconds 60
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($dialog -eq [IntPtr]::Zero) {throw '连续阅读删除当前页未默认确认'}
    Message ([ViewingRegressionNative]::GetDlgItem($dialog,6)) 0x00F5
    $s = Wait-State {param($v) $v.count -eq '8' -and $v.loading -eq '0' -and $v.readerWaiting -eq '0'} '删除当前页后阅读布局未恢复'
    Assert-True (-not (Test-Path -LiteralPath $paths[4]) -and (Test-Path -LiteralPath $paths[3])) '连续阅读删除只移除选中的右页，左页与文件顺序保留'
    Command 1307
    $s = Wait-State {param($v) $v.image -eq '2' -and $v.readerWaiting -eq '0'} '删除后前一组未恢复 2/3'
    Command 1308
    $s = Wait-State {param($v) $v.image -eq '4' -and $v.readerWaiting -eq '0'} '删除后下一组未恢复 4/5'
    $s = Set-Option 21 0 332
    $s = Wait-State {param($v) $v.double -eq '0' -and $v.readerWaiting -eq '0'} '单页布局未完成'
    Assert-True ($s.pageLeftIndex -eq '4' -and $s.pageRightIndex -eq '0') '横向单页不保留幽灵右页'
    $s = Set-Option 20 0 331
    Stop-Viewer
    $s = Start-Viewer $paths[1]
    Assert-True ($s.reader -eq '1' -and $s.axis -eq '0' -and $s.shift -eq '1') '阅读布局与当前文件集的配对跨重启保留'
    Command 1300
    $s = Wait-State {param($v) $v.reader -eq '0'} '退出连续阅读未回到单图'

    [ViewingRegressionNative]::Activate($window)
    Start-Sleep -Milliseconds 150
    if ([ViewingRegressionNative]::GetForegroundWindow() -ne $window) { throw '无法安全验证真实长按键，测试窗口未获得前台' }
    [ViewingRegressionNative]::keybd_event(0x26,0,0,[UIntPtr]::Zero)
    try {
        Start-Sleep -Milliseconds 400
        $firstZoom = Number (Read-State) 'zoomExact'
        $firstLevels = Number (Read-State) 'zoomLevels'
        Start-Sleep -Milliseconds 500
        $secondZoom = Number (Read-State) 'zoomExact'
        Assert-True ($secondZoom -gt $firstZoom*1.3) '真实长按按时间连续放大，而非只重复一次分档'
        Assert-True ((Number (Read-State) 'zoomLevels') -le $firstLevels+2) '长按缩放不把每帧比例累积进离散档位表'
    } finally { [ViewingRegressionNative]::keybd_event(0x26,0,2,[UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 160
    $releasedZoom = Number (Read-State) 'zoomExact'
    Start-Sleep -Milliseconds 350
    Assert-True ([Math]::Abs((Number (Read-State) 'zoomExact')-$releasedZoom) -lt 0.005) '松开缩放快捷键后及时停止'
    Stop-Viewer

    # 周期 2 像素的黑白棋盘：50% 时精确面积过滤应成中灰，最近邻保持黑/白。
    $qualityFolder = Join-Path $testRoot 'quality'
    [void](New-Item -ItemType Directory -Path $qualityFolder)
    $checkerPath = Join-Path $qualityFolder 'checker.png'
    $tile = [Drawing.Bitmap]::new(8,8)
    $tileGraphics = [Drawing.Graphics]::FromImage($tile)
    $checker = [Drawing.Bitmap]::new(1024,1024)
    $checkerGraphics = [Drawing.Graphics]::FromImage($checker)
    $brush = $null
    try {
        $tileGraphics.Clear([Drawing.Color]::Black)
        for ($row=0;$row -lt 8;$row++) {for ($col=0;$col -lt 8;$col++) {
            if (($row+$col)%2) {$tileGraphics.FillRectangle([Drawing.Brushes]::White,$col,$row,1,1)}
        }}
        $brush = [Drawing.TextureBrush]::new($tile)
        $checkerGraphics.FillRectangle($brush,0,0,1024,1024)
        $checker.Save($checkerPath,[Drawing.Imaging.ImageFormat]::Png)
    } finally {
        if ($null -ne $brush) {$brush.Dispose()}
        $tileGraphics.Dispose(); $tile.Dispose(); $checkerGraphics.Dispose(); $checker.Dispose()
    }
    $s = Start-Viewer $checkerPath
    $s = Set-Option 17 0 328
    for ($step=0;$step -lt 10 -and [int]$s.zoom -gt 50;$step++) {
        Message $window 0x0100 0x28; Message $window 0x0101 0x28
        Start-Sleep -Milliseconds 350
        $s = Read-State
    }
    Assert-True ($s.zoom -eq '50') '棋盘格真窗口按 50% 显示'
    $x = [int]((Number $s 'width')/2)+5
    $y = [int]((Number $s 'height')/2)+5
    $pixel = Screen-Pixel $x $y
    $deadline=[DateTime]::UtcNow.AddSeconds(3)
    while(([Math]::Abs($pixel.R-128) -gt 3 -or [Math]::Abs($pixel.G-128) -gt 3 -or [Math]::Abs($pixel.B-128) -gt 3) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 60; $pixel=Screen-Pixel $x $y
    }
    Save-ClientEvidence $window '棋盘格高质量实际画面.png'
    Assert-True ([Math]::Abs($pixel.R-128) -le 3 -and [Math]::Abs($pixel.G-128) -le 3 -and [Math]::Abs($pixel.B-128) -le 3) ('高质量缩小的实际屏幕棋盘消除锯齿，面积平均为中灰：'+$pixel.ToString())
    $s = Set-Option 17 2 328
    $pixel = Screen-Pixel $x $y
    Assert-True ($pixel.R -le 3 -or $pixel.R -ge 252) '像素清晰模式实际屏幕保留最近邻黑白，不混入灰色'
    Stop-Viewer

    $manyFolder = Join-Path $testRoot 'many-pages'
    [void](New-Item -ItemType Directory -Path $manyFolder)
    for ($page=1;$page -le 100;$page++) {
        Copy-Item -LiteralPath $paths[0] -Destination (Join-Path $manyFolder ('{0:000}.png' -f $page))
    }
    $s = Start-Viewer (Join-Path $manyFolder '001.png')
    $s = Set-Option 19 1 330
    Assert-True ($s.shift -eq '0') '换文件集不会沿用另一个目录的配对纠正'
    $s = Set-Option 17 0 328
    $peakWorkingSet = 0L
    $maximumTiles = 0
    $timer = [Diagnostics.Stopwatch]::StartNew()
    for ($page=2;$page -le 50;$page++) {
        Command 1308
        $s = Wait-State {param($v) [int]$v.image -eq $page -and $v.readerWaiting -eq '0' -and $v.loading -eq '0'} '连续翻页压力测试未稳定'
        $process.Refresh()
        $peakWorkingSet = [Math]::Max($peakWorkingSet,$process.WorkingSet64)
        $maximumTiles = [Math]::Max($maximumTiles,[int]$s.tiles)
    }
    $timer.Stop()
    Assert-True ($maximumTiles -le 4) '持续阅读 100 张文件集时只保留附近可见栅格，不堆积全部画布'
    Assert-True ($peakWorkingSet -lt 768MB) '连续翻阅 50 页的工作集有界，小于 768 MiB'
    Write-Host ('PERF 49 次连续翻页耗时 {0:N2} 秒，观察峰值工作集 {1:N1} MiB，最多 {2} 个可见栅格' -f $timer.Elapsed.TotalSeconds,($peakWorkingSet/1MB),$maximumTiles)
    Stop-Viewer
    Copy-Item -LiteralPath $statePath -Destination (Join-Path $evidence 'last-state.txt') -Force
    Write-Host ("SUMMARY $passed passed, 0 failed")
}
finally {
    if ($null -ne $process -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force; [void]$process.WaitForExit(3000) }
    $env:YEIMAGEVIEWER_INTERACTION_PROBE = $oldProbe
    $env:YEIMAGEVIEWER_TEST_PRIMARY_MONITOR = $oldMonitor
    $env:YEIMAGEVIEWER_TEST_REGISTRY_SANDBOX=$oldRegistrySandbox
    if ($SettingsDpiOnly -and $registrySandbox -match '^Software\\YeImageViewerTests\\Native-[a-f0-9]{32}$') {
        [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($registrySandbox,$false)
    }
    [void][ViewingRegressionNative]::SetCursorPos($oldCursor.X,$oldCursor.Y)
    [void][ViewingRegressionNative]::SetThreadDpiAwarenessContext($oldDpi)
    if (Test-Path -LiteralPath $statePath) { Copy-Item -LiteralPath $statePath -Destination (Join-Path $evidence 'last-state.txt') -Force }
    if (-not $KeepEvidence) {
        $resolved = [IO.Path]::GetFullPath($testRoot)
        if ($resolved.StartsWith($tempBase,[StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolved).StartsWith('YeViewer-Viewing-')) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    } else { Write-Host ('保留隔离测试目录：'+$testRoot) }
}
