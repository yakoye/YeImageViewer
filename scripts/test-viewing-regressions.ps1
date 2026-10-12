param(
    [string]$Viewer = (Join-Path $PSScriptRoot '../x64/Release/YeImageViewer.exe'),
    [string]$Animation = (Join-Path $PSScriptRoot '../test/format corpus/files/animated.gif'),
    [switch]$KeepEvidence
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
function Set-Option([int]$Row,[int]$Option,[int]$Slot) {
    Message $window 0x0100 0x70
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $settings = [ViewingRegressionNative]::Find($process.Id,'YeImageViewerSettingWnd')
        if ($settings -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($settings -eq [IntPtr]::Zero) { throw '设置窗口未打开' }
    $counts = @(3,3,3,2,2,4,2,2,2,4,2,2,3,3,2,2,2,3,3,2,2,2,2,2,3)
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
    $state = Wait-State { param($s) $s.ContainsKey('pref'+$Slot) -and [int]$s['pref'+$Slot] -eq $Option } ('设置单选行 '+$Row+' 未生效')
    Assert-True ([int]$state['pref'+$Slot] -eq $Option) ('设置行 '+$Row+' 的选项 '+$Option+' 可点击并生效')
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

    $s = Start-Viewer $paths[0]
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
    $s = Set-Option 24 2 335
    Assert-True ($s.long -eq '1' -and $s.zoom -eq '100' -and [Math]::Abs((Number $s 'top')) -le 1) '长图原始比例 100% 从顶部开始，不按窗口宽度拉伸'
    $s = Read-State
    $beforeTop = Number $s 'top'
    Drag ([int]((Number $s 'width')/2)) ([int]((Number $s 'height')/2)) 30 -40
    $after = Read-State
    Assert-True ($after.slideX -eq '0' -and [Math]::Abs((Number $after 'top')-$beforeTop+120) -le 3) '长图斜向拖动保持水平居中，3 倍加速只增加纵向位移'
    $s = Set-Option 24 0 335
    $beforeTop = Number $s 'top'
    Drag ([int]((Number $s 'width')/2)) ([int]((Number $s 'height')/2)) 20 -40
    $after = Read-State
    Assert-True ($after.slideX -eq '0' -and [Math]::Abs((Number $after 'top')-$beforeTop+40) -le 3) '1 倍拖拽恢复 1:1，修改加速选项不重置当前取景'
    $s = Set-Option 24 1 335
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
    [void][ViewingRegressionNative]::SetCursorPos($oldCursor.X,$oldCursor.Y)
    [void][ViewingRegressionNative]::SetThreadDpiAwarenessContext($oldDpi)
    if (Test-Path -LiteralPath $statePath) { Copy-Item -LiteralPath $statePath -Destination (Join-Path $evidence 'last-state.txt') -Force }
    if (-not $KeepEvidence) {
        $resolved = [IO.Path]::GetFullPath($testRoot)
        if ($resolved.StartsWith($tempBase,[StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolved).StartsWith('YeViewer-Viewing-')) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    } else { Write-Host ('保留隔离测试目录：'+$testRoot) }
}
