# 重新生成 README 用的界面预览图（仓库根的 preview.png）。
#
# 界面改了就跑一次，别手工截图——手工截的尺寸、背景、窗口位置每次都不一样，
# 而且很容易把桌面上别的窗口截进去（这事发生过）。
#
# 样图用 raw.pixls.us 的 CC0（公有领域）样本，可以放进公开仓库。选 RAW 是故意的：
# 一张图同时展示「RAW 支持」「真实相机 EXIF」和摄影参数的惯用写法。
#
#   ./tools/make-preview.ps1
#   ./tools/make-preview.ps1 -PhotoDir "D:\Photos"   # 预览图里会显示这个路径
param(
    # 样图落脚的目录。预览图的信息面板里会把这个路径显示出来，所以挑一个像样的
    # ——临时目录那种一长串 GUID 摆在 README 的头图上不好看。
    [string]$PhotoDir = "D:\Photos",
    [string]$Output
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
$viewer = Join-Path $repoRoot "x64\Release\YeImageViewer.exe"
$sampleSource = Join-Path $repoRoot "test\corpus\_local\03-raw\Nikon - COOLPIX P1000 - 12bit 12bit uncompressed (4_3).NRW"
$sample = Join-Path $PhotoDir "sunset.nrw"
if (-not $Output) { $Output = Join-Path $repoRoot "preview.png" }

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace PreviewShot -Name N -MemberDefinition @'
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(System.IntPtr hWnd);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT rect);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool GetClientRect(System.IntPtr hWnd, out RECT rect);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool PostMessageW(System.IntPtr hWnd, uint msg, System.UIntPtr wParam, System.IntPtr lParam);
    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    public static extern int GetWindowTextW(System.IntPtr hWnd, System.Text.StringBuilder buf, int max);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern System.IntPtr GetForegroundWindow();
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(System.IntPtr hWnd, System.IntPtr pid);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool AttachThreadInput(uint attachTo, uint attachFrom, bool attach);
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool BringWindowToTop(System.IntPtr hWnd);
    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);
    public struct RECT { public int Left, Top, Right, Bottom; }
'@

if (-not (Test-Path -LiteralPath $viewer)) {
    throw "先构建：$viewer 不存在（跑 ./buildRelease.ps1）"
}
if (-not (Test-Path -LiteralPath $sampleSource)) {
    throw "样图缺失：$sampleSource（跑 ./scripts/fetch-raw-corpus.ps1 下载 CC0 RAW 样本）"
}

# 先探一下屏幕读不读得到，别等开完程序、等完解码才在 CopyFromScreen 上撞一句
# 「句柄无效」——那句话根本看不出是环境问题还是脚本写错了。
# 远程桌面客户端最小化或断开时，屏幕就是抓不下来，和脚本无关。
try {
    $probeBitmap = New-Object System.Drawing.Bitmap 1, 1
    $probeGraphics = [System.Drawing.Graphics]::FromImage($probeBitmap)
    $probeGraphics.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size 1, 1))
    $probeGraphics.Dispose()
    $probeBitmap.Dispose()
}
catch {
    throw ("读不到屏幕像素，截不了图。远程桌面客户端最小化或已断开时就是这样，" +
        "也可能是锁屏。把会话放到前台再跑一次。")
}
New-Item -ItemType Directory -Force -Path $PhotoDir | Out-Null
Copy-Item -LiteralPath $sampleSource -Destination $sample -Force

$screen = [System.Windows.Forms.Screen]::PrimaryScreen
$work = $screen.WorkingArea
$bounds = $screen.Bounds

# 沉浸显示下图片以外那圈是自绘的半透明压暗层（BackgroundPolicy::PRESENTATION_TINT，
# 六成黑），不受背景设置影响——这是有意的，看图时周围该是暗的而不是一块死色。
# 代价是截图会把背后桌面上的窗口透进来，所以先铺一张纯黑的全屏底。
$backdrop = New-Object System.Windows.Forms.Form
$backdrop.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$backdrop.BackColor = [System.Drawing.Color]::Black
$backdrop.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
$backdrop.Bounds = $bounds
$backdrop.TopMost = $true
$backdrop.ShowInTaskbar = $false
$backdrop.Show()
1..20 | ForEach-Object { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 25 }

$process = Start-Process -FilePath $viewer -ArgumentList ('"' + $sample + '"') -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do { Start-Sleep -Milliseconds 300; $process.Refresh() }
    while (-not $process.HasExited -and $process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited -or $process.MainWindowHandle -eq 0) { throw "看图程序没开起来" }
    $window = $process.MainWindowHandle

    # 等真图解完：标题里的尺寸要变成 RAW 的真实像素，不能截到系统缩略图那一帧
    $titleBuffer = New-Object Text.StringBuilder 1024
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 400
        [void][PreviewShot.N]::GetWindowTextW($window, $titleBuffer, $titleBuffer.Capacity)
    } while ($titleBuffer.ToString() -notmatch "4624x3470" -and [DateTime]::UtcNow -lt $deadline)
    if ($titleBuffer.ToString() -notmatch "4624x3470") {
        throw "等不到真图解码完成，标题还是：$($titleBuffer.ToString())"
    }
    Write-Host "标题：$($titleBuffer.ToString())"

    # 保持沉浸显示（无边框）：完整的 EXIF 面板只在这个模式下出
    # （main.cpp: presentationMode ? Mode::Full : Mode::Compact），带边框窗口下
    # 只有格式/大小/分辨率那几项基础信息，相机、光圈、快门全看不到。
    # 无边框也顺带避开了 Win11 的圆角和 DWM 阴影——那圈不可见边框会把背后的窗口截进来。

    # 窗口整体收一点，让 README 里不用缩太多还能看清字。
    # HWND_TOPMOST(-1) + SWP_SHOWWINDOW：截图时别的程序的窗口可能压在上面，
    # 而从后台进程调 SetForegroundWindow 会被前台锁拒掉。
    $windowWidth = [Math]::Min(1400, $work.Width - 40)
    $windowHeight = [Math]::Min(880, $work.Height - 20)
    $windowX = $work.X + [int](($work.Width - $windowWidth) / 2)
    $windowY = $work.Y + [int](($work.Height - $windowHeight) / 2)
    [void][PreviewShot.N]::SetWindowPos($window, [IntPtr](-1), $windowX, $windowY,
        $windowWidth, $windowHeight, 0x0040)
    Start-Sleep -Milliseconds 1200

    # 打开信息面板：ContextMenu::toggleExifDisplay = 1004
    [void][PreviewShot.N]::PostMessageW($window, 0x0111, [UIntPtr]1004, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 1500

    # 鼠标留在画面上会触发悬停提示，也会被截进去
    [void][PreviewShot.N]::SetCursorPos($bounds.Width - 2, $bounds.Height - 2)

    $foregroundThread = [PreviewShot.N]::GetWindowThreadProcessId(
        [PreviewShot.N]::GetForegroundWindow(), [IntPtr]::Zero)
    $selfThread = [PreviewShot.N]::GetCurrentThreadId()
    [void][PreviewShot.N]::AttachThreadInput($selfThread, $foregroundThread, $true)
    [void][PreviewShot.N]::BringWindowToTop($window)
    [void][PreviewShot.N]::SetForegroundWindow($window)
    [void][PreviewShot.N]::AttachThreadInput($selfThread, $foregroundThread, $false)
    Start-Sleep -Milliseconds 1500

    $rect = New-Object PreviewShot.N+RECT
    [void][PreviewShot.N]::GetWindowRect($window, [ref]$rect)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    Write-Host "窗口矩形：$($rect.Left),$($rect.Top) ${width}x${height}（屏幕 $($bounds.Width)x$($bounds.Height)）"
    # 窗口有一点落在屏幕外，CopyFromScreen 就抛「句柄无效」，而且不说是为什么
    if ($rect.Left -lt $bounds.X -or $rect.Top -lt $bounds.Y -or
        $rect.Right -gt ($bounds.X + $bounds.Width) -or $rect.Bottom -gt ($bounds.Y + $bounds.Height)) {
        throw "窗口超出屏幕，截不了：$($rect.Left),$($rect.Top) ${width}x${height}"
    }

    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
                (New-Object System.Drawing.Size $width, $height))
        }
        finally { $graphics.Dispose() }

        # 存成 24 位：截图没有半透明像素，带上 alpha 通道白白多三成体积，
        # 而这张图是 README 的头图，谁打开仓库都要下一次。
        $opaque = $bitmap.Clone(
            (New-Object System.Drawing.Rectangle 0, 0, $width, $height),
            [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        try { $opaque.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png) }
        finally { $opaque.Dispose() }
    }
    finally { $bitmap.Dispose() }

    Write-Host ("已写出 $Output（{0:N0} 字节）" -f (Get-Item -LiteralPath $Output).Length)
}
finally {
    $backdrop.Close()
    $backdrop.Dispose()
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(6000)) { Stop-Process -Id $process.Id -Force }
    }
}
