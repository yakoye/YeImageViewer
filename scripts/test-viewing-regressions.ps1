param(
    [string]$Viewer = (Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),
    [string]$Animation = (Join-Path $PSScriptRoot '../test/format corpus/files/animated.gif'),
    [switch]$KeepEvidence,
    [switch]$SettingsDpiOnly,
    [switch]$PanOnly,
    [switch]$SettingsAuditOnly,
    [switch]$ActualSizeOnly,
    [switch]$ReaderPanOnly,
    [switch]$ShortcutAuditOnly,
    [switch]$RapidPanOnly,
    [switch]$PanVisualOnly
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
    $counts = @(3,3,3,2,2,4,2,2,2,4,2,2,3,3,2,2,2,3,3,2,2,2,2,2,9)
    $naturalY = 52 + 216 + $Row*48 + 19
    $steps = [Math]::Max(0,[int][Math]::Ceiling(($naturalY-500)/72.0))
    for ($step=0; $step -lt $steps; $step++) { Message $settings 0x020A 4287102976 }
    $scale = [ViewingRegressionNative]::GetDpiForWindow($settings)/96.0
    $cell = [Math]::Floor(406/$counts[$Row])
    $x = [int][Math]::Round((176 + $cell*($Option+0.5))*$scale)
    $y = [int][Math]::Round(($naturalY-$steps*72)*$scale)
    Click $settings $x $y
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
function Screen-Pixel([int]$X,[int]$Y) {
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
    Start-Sleep -Milliseconds 60
    Click $window $x $y
    $s=Wait-State {param($v) $v.panPopover -eq '1'} '倍率浮层未打开'
    if ($Level -lt 0) {
        Click $window ([int]((Number $s 'panPanelX')+(Number $s 'panPanelW')/2)) ([int]((Number $s 'panPanelY')+(Number $s 'panPanelH')/4))
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
    $counts=@(3,3,3,2,2,4,2,2,2,4,2,2,3,3,2,2,2,3,3,2,2,2,2,2,9)
    $fields=@('settingSwitchAnimation','settingTheme','settingLanguage','settingRightClick',
        'pref325','pref259','pref258','pref261','pref263','pref262','pref264','pref265',
        'pref323','pref324','pref322','pref326','pref327','pref328','pref329','pref330',
        'pref331','pref332','pref333','pref334','pref356')
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
        $graphics.FillRectangle([Drawing.Brushes]::Red,0,0,300,200)
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
    if ($SettingsAuditOnly) {Test-SettingsAudit; return}
    if ($ReaderPanOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-ReaderPan; return}
    if ($RapidPanOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-RapidPan; return}
    if ($PanVisualOnly) {Stop-Viewer; $s=Start-Viewer $tall; Test-PanVisual; return}
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
    $s = Set-Option 12 1 323
    $s = Set-Option 13 0 324
    $s = Set-Option 14 1 322
    Message $window 0x0100 0x27
    $s = Wait-State {param($v) $v.image -eq '2' -and $v.loading -eq '0'} '锁定后的下一张未加载'
    $expected = [Math]::Round([Math]::Min(1,[Math]::Min((Number $s 'width')/800,(Number $s 'height')/1200))*100)
    Assert-True ($s.shape -eq '1' -and [Math]::Abs((Number $s 'zoom')-$expected) -le 1) '切图保持沉浸和适应窗口，按新分辨率计算比例'
    Message $window 0x0100 0x26
    Message $window 0x0101 0x26
    Start-Sleep -Milliseconds 350
    $larger = Read-State
    Assert-True ((Number $larger 'zoom') -gt $expected) '手动放大当前图片可用'
    Message $window 0x0100 0x27
    $s = Wait-State {param($v) $v.image -eq '3' -and $v.loading -eq '0'} '切图未恢复锁定规则'
    $expected = [Math]::Round([Math]::Min(1,[Math]::Min((Number $s 'width')/1200,(Number $s 'height')/900))*100)
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
    Message $window 0x020A 7864320
    $s = Wait-State {param($v) [Math]::Abs((Number $v 'readerOffset')-$initialOffset) -lt 2} '反向滚轮没有回到原阅读位置'
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
    Message $window 0x020A 7864320
    $s = Wait-State {param($v) [Math]::Abs((Number $v 'readerOffset')-$initialOffset) -lt 2} '横向反向滚轮没有恢复位置'
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
    Assert-True ([Math]::Abs($pixel.R-128) -le 3 -and [Math]::Abs($pixel.G-128) -le 3 -and [Math]::Abs($pixel.B-128) -le 3) '高质量缩小的实际屏幕棋盘消除锯齿，面积平均为中灰'
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
