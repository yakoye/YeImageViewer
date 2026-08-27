param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot
$releaseDir = Join-Path $repoRoot "x64\Release"
$viewer = Join-Path $releaseDir "YeImageViewer.exe"
$unitTests = Join-Path $releaseDir "YeImageViewerTests.exe"
$crashFixture = Join-Path $repoRoot "test\Image crash\dji_export_photo_20260809221510044.jpg"
$hdrFixture = Join-Path $repoRoot "test\HDR color error\HDR.hdr"
$sharpSvgFixture = Join-Path $repoRoot "test\SVG Blurring\SittingHuman.svg"
$textSvgFixture = Join-Path $repoRoot "test\severely jagged\cachetest.drawio.svg"
$jaggedFixture = Join-Path $repoRoot "test\severely jagged\cachetest.drawio.png"
$currentRestoreFixtures = @(
    (Join-Path $repoRoot "test\current image restore\01-small.svg"),
    (Join-Path $repoRoot "test\current image restore\02-landscape.svg"),
    (Join-Path $repoRoot "test\current image restore\03-square.svg"),
    (Join-Path $repoRoot "test\current image restore\04-wide.svg"),
    (Join-Path $repoRoot "test\current image restore\05-tall-capped.svg")
)
$toolbarIcons = @(
    (Join-Path $repoRoot "YeImageViewer\file\icons\previous.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\next.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\rotate-left.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\rotate-right.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\settings.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\close.svg")
)

if (-not $SkipBuild) {
    $shell = Join-Path $PSHOME "pwsh.exe"
    if (-not (Test-Path -LiteralPath $shell)) {
        $shell = Join-Path $PSHOME "powershell.exe"
    }

    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "buildRelease.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed with exit code $LASTEXITCODE."
    }
}

foreach ($requiredFile in @($viewer, $unitTests, $crashFixture, $hdrFixture, $sharpSvgFixture, $textSvgFixture, $jaggedFixture) + $toolbarIcons + $currentRestoreFixtures) {
    if (-not (Test-Path -LiteralPath $requiredFile)) {
        throw "Required regression-test file is missing: $requiredFile"
    }
}

$expectedFileVersion = "1.36.13.0"
$actualFileVersion = (Get-Item -LiteralPath $viewer).VersionInfo.FileVersion
if ($actualFileVersion -ne $expectedFileVersion) {
    throw "Viewer file version mismatch: expected $expectedFileVersion, got $actualFileVersion."
}
Write-Host "PASS viewer file version is $expectedFileVersion."

$expectedCommitId = (& git -C $repoRoot rev-parse --short=12 HEAD).Trim()
$viewerAscii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($viewer))
if (-not $viewerAscii.Contains($expectedCommitId)) {
    throw "Viewer build metadata does not contain current commit ID $expectedCommitId."
}
Write-Host "PASS viewer embeds current commit ID $expectedCommitId."

Write-Host "Running unit regression tests..."
$expectedHdrHash = "1A1A661E0A22BECBE019B6C095004315351F28600D9BD7600BD933BEB351E5D5"
$actualHdrHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $hdrFixture).Hash
if ($actualHdrHash -ne $expectedHdrHash) {
    throw "HDR regression fixture hash mismatch: expected $expectedHdrHash, got $actualHdrHash."
}

$expectedSharpSvgHash = "86F5955DB6C420148EE317189D40E394DAC9999F54BB038EF744F012BFFB3759"
$actualSharpSvgHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sharpSvgFixture).Hash
if ($actualSharpSvgHash -ne $expectedSharpSvgHash) {
    throw "Sharp SVG regression fixture hash mismatch: expected $expectedSharpSvgHash, got $actualSharpSvgHash."
}

$expectedTextSvgHash = "52DFB4983923CA525D8B92A78122D3ADB75F72DBAFFBAA40DB2CCBD6B7E5FE08"
$actualTextSvgHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $textSvgFixture).Hash
if ($actualTextSvgHash -ne $expectedTextSvgHash) {
    throw "Text SVG regression fixture hash mismatch: expected $expectedTextSvgHash, got $actualTextSvgHash."
}

$expectedJaggedHash = "14FD50F84BCD0576FB55D3C34848B840C808F141C9009BF01A2FED372742BF10"
$actualJaggedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $jaggedFixture).Hash
if ($actualJaggedHash -ne $expectedJaggedHash) {
    throw "Jagged-image regression fixture hash mismatch: expected $expectedJaggedHash, got $actualJaggedHash."
}

& $unitTests $hdrFixture $sharpSvgFixture $textSvgFixture @toolbarIcons
if ($LASTEXITCODE -ne 0) {
    throw "Unit regression tests failed with exit code $LASTEXITCODE."
}

if (-not ("YeImageViewerTestNativeV1365" -as [type])) {
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class YeImageViewerTestNativeV1365
{
    public delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct MONITORINFO
    {
        public int Size;
        public RECT Monitor;
        public RECT Work;
        public uint Flags;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct GUITHREADINFO
    {
        public int Size;
        public uint Flags;
        public IntPtr Active;
        public IntPtr Focus;
        public IntPtr Capture;
        public IntPtr MenuOwner;
        public IntPtr MoveSize;
        public IntPtr Caret;
        public RECT CaretRect;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsZoomed(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool IsWindowEnabled(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, IntPtr processId);

    [DllImport("user32.dll", EntryPoint = "GetWindowThreadProcessId")]
    public static extern uint GetWindowThreadProcessIdForEnum(IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    public static extern bool GetGUIThreadInfo(uint threadId, ref GUITHREADINFO info);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr window, StringBuilder className, int maximumLength);

    public static IntPtr FindProcessWindow(uint processId, string expectedClassName)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr parameter) {
            uint ownerProcessId;
            GetWindowThreadProcessIdForEnum(window, out ownerProcessId);
            if (ownerProcessId == processId) {
                StringBuilder className = new StringBuilder(256);
                GetClassName(window, className, className.Capacity);
                if (className.ToString() == expectedClassName) {
                    found = window;
                    return false;
                }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr window, out RECT rect);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

    [DllImport("user32.dll")]
    public static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr window, StringBuilder text, int maximumLength);
}
"@
}

Write-Host "Checking fresh-install window defaults..."
$freshDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Fresh-" + [Guid]::NewGuid().ToString("N"))
$freshViewer = Join-Path $freshDirectory "YeImageViewer.exe"
$freshProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $freshDirectory)
    Copy-Item -LiteralPath $viewer -Destination $freshViewer
    $freshProcess = Start-Process -FilePath $freshViewer -ArgumentList ('"' + $sharpSvgFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $freshProcess.Refresh()
    } while (-not $freshProcess.HasExited -and $freshProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($freshProcess.HasExited -or $freshProcess.MainWindowHandle -eq 0 -or -not $freshProcess.Responding) {
        throw "Fresh-install regression failed: viewer did not open a responsive window."
    }
    $freshWindow = [IntPtr]$freshProcess.MainWindowHandle
    if ([YeImageViewerTestNativeV1365]::IsZoomed($freshWindow)) {
        throw "Fresh-install regression failed: a new installation opened maximized."
    }
    $freshClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($freshWindow, [ref]$freshClientRect)
    $freshMonitor = [YeImageViewerTestNativeV1365]::MonitorFromWindow($freshWindow, 2)
    $freshMonitorInfo = New-Object YeImageViewerTestNativeV1365+MONITORINFO
    $freshMonitorInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($freshMonitorInfo)
    [void][YeImageViewerTestNativeV1365]::GetMonitorInfo($freshMonitor, [ref]$freshMonitorInfo)
    $freshWorkWidth = $freshMonitorInfo.Work.Right - $freshMonitorInfo.Work.Left
    $freshWorkHeight = $freshMonitorInfo.Work.Bottom - $freshMonitorInfo.Work.Top
    $freshStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -16).ToInt64()
    $freshExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -20).ToInt64()
    if (($freshStyle -band 0x00C00000) -ne 0 -or
        ($freshExtendedStyle -band 0x00200000) -eq 0 -or
        [Math]::Abs(($freshClientRect.Right - $freshClientRect.Left) - $freshWorkWidth) -gt 1 -or
        [Math]::Abs(($freshClientRect.Bottom - $freshClientRect.Top) - $freshWorkHeight) -gt 1) {
        throw "Fresh-install regression failed: image did not open in the borderless monitor work area."
    }
    Write-Host "PASS fresh install opens in the borderless immersive work area."
    Start-Sleep -Milliseconds 300
    $freshInitialTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $freshInitialTitle, $freshInitialTitle.Capacity)
    $freshInitialZoomMatch = [regex]::Match($freshInitialTitle.ToString(), '(\d+)%')
    if (-not $freshInitialZoomMatch.Success) {
        throw "Fresh-install regression failed: presentation title did not report its zoom percentage."
    }

    $freshCenterX = [int](($freshClientRect.Right - $freshClientRect.Left) / 2)
    $freshCenterY = [int](($freshClientRect.Bottom - $freshClientRect.Top) / 2)
    $freshCenterPosition = [IntPtr](($freshCenterY -shl 16) -bor ($freshCenterX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0200, [UIntPtr]::Zero, $freshCenterPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0201, [UIntPtr]1, $freshCenterPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0202, [UIntPtr]0, $freshCenterPosition)
    Start-Sleep -Milliseconds 250
    $freshImageClickStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -16).ToInt64()
    if (($freshImageClickStyle -band 0x00C00000) -ne 0) {
        throw "Fresh-install regression failed: clicking the image unexpectedly left presentation mode."
    }
    Write-Host "PASS clicking the image keeps presentation mode available for dragging."

    $freshBackgroundX = 120
    $freshBackgroundY = 120
    $freshBackgroundPosition = [IntPtr](($freshBackgroundY -shl 16) -bor ($freshBackgroundX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0200, [UIntPtr]::Zero, $freshBackgroundPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0201, [UIntPtr]1, $freshBackgroundPosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0202, [UIntPtr]0, $freshBackgroundPosition)
    Start-Sleep -Milliseconds 500
    $freshFramedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -16).ToInt64()
    $freshFramedExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($freshWindow, -20).ToInt64()
    $freshFramedRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($freshWindow, [ref]$freshFramedRect)
    $freshFramedWidth = $freshFramedRect.Right - $freshFramedRect.Left
    $freshFramedHeight = $freshFramedRect.Bottom - $freshFramedRect.Top
    if (($freshFramedStyle -band 0x00C00000) -eq 0 -or
        ($freshFramedExtendedStyle -band 0x00200000) -eq 0 -or
        $freshFramedWidth -ge $freshWorkWidth -or $freshFramedHeight -ge $freshWorkHeight) {
        throw "Fresh-install regression failed: clicking the background did not return to an image-sized framed window."
    }
    if (-not [YeImageViewerTestNativeV1365]::IsWindowEnabled($freshWindow)) {
        throw "Fresh-install regression failed: framed window remained disabled after leaving presentation mode."
    }
    $freshThreadId = [YeImageViewerTestNativeV1365]::GetWindowThreadProcessId($freshWindow, [IntPtr]::Zero)
    $freshThreadInfo = New-Object YeImageViewerTestNativeV1365+GUITHREADINFO
    $freshThreadInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($freshThreadInfo)
    if (-not [YeImageViewerTestNativeV1365]::GetGUIThreadInfo($freshThreadId, [ref]$freshThreadInfo) -or
        $freshThreadInfo.Active -ne $freshWindow -or $freshThreadInfo.Focus -ne $freshWindow -or
        $freshThreadInfo.Capture -ne [IntPtr]::Zero) {
        throw "Fresh-install regression failed: framed window did not restore active mouse and keyboard interaction."
    }
    Write-Host "PASS background click restores an active, enabled framed window."

    # WM_MOUSEWHEEL packs modifier flags in the low word and the signed wheel
    # delta in the high word. Keep the cursor in the image area so an
    # unmodified wheel exercises the normal zoom path.
    $freshFramedCenterX = [int]($freshFramedWidth / 2)
    $freshFramedCenterY = [int]($freshFramedHeight / 2)
    $freshFramedCenterPosition = [IntPtr](($freshFramedCenterY -shl 16) -bor ($freshFramedCenterX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x0200, [UIntPtr]::Zero, $freshFramedCenterPosition)

    $wheelStartTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $wheelStartTitle, $wheelStartTitle.Capacity)
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]4287102984, $freshFramedCenterPosition)
    Start-Sleep -Milliseconds 700
    $wheelNextTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $wheelNextTitle, $wheelNextTitle.Capacity)
    if ($wheelNextTitle.ToString() -eq $wheelStartTitle.ToString()) {
        throw "Wheel regression failed: Ctrl + wheel down did not switch to the next image."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]0x00780008, $freshFramedCenterPosition)
    Start-Sleep -Milliseconds 700
    $wheelReturnedTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $wheelReturnedTitle, $wheelReturnedTitle.Capacity)
    if ($wheelReturnedTitle.ToString() -ne $wheelStartTitle.ToString()) {
        throw "Wheel regression failed: Ctrl + wheel up did not return to the previous image."
    }

    $wheelInitialZoom = [regex]::Match($wheelReturnedTitle.ToString(), '(\d+)%')
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]0x00780000, $freshFramedCenterPosition)
    Start-Sleep -Milliseconds 350
    $wheelZoomTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $wheelZoomTitle, $wheelZoomTitle.Capacity)
    $wheelChangedZoom = [regex]::Match($wheelZoomTitle.ToString(), '(\d+)%')
    if (-not $wheelInitialZoom.Success -or -not $wheelChangedZoom.Success -or
        $wheelChangedZoom.Groups[1].Value -eq $wheelInitialZoom.Groups[1].Value) {
        throw "Wheel regression failed: ordinary wheel no longer zooms the current image."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $freshWindow, 0x020A, [UIntPtr][uint64]4287102976, $freshFramedCenterPosition)
    Start-Sleep -Milliseconds 350
    Write-Host "PASS Ctrl + wheel switches images and ordinary wheel keeps zooming."

    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $freshSwitchedRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($freshWindow, [ref]$freshSwitchedRect)
    if (($freshSwitchedRect.Right - $freshSwitchedRect.Left) -ne $freshFramedWidth -or
        ($freshSwitchedRect.Bottom - $freshSwitchedRect.Top) -ne $freshFramedHeight) {
        throw "Fresh-install regression failed: browsing images changed the anchored framed window size."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0100, [UIntPtr]0x25, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0101, [UIntPtr]0x25, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $freshReturnedTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText(
        $freshWindow, $freshReturnedTitle, $freshReturnedTitle.Capacity)
    $freshReturnedZoomMatch = [regex]::Match($freshReturnedTitle.ToString(), '(\d+)%')
    if (-not $freshReturnedZoomMatch.Success -or
        $freshReturnedZoomMatch.Groups[1].Value -ne $freshInitialZoomMatch.Groups[1].Value) {
        throw "Fresh-install regression failed: returning to the first image changed its immersive zoom percentage."
    }
    Write-Host "PASS background exit anchors the frame while browsing preserves per-image zoom."

    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0100, [UIntPtr]0x71, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    $unexpectedF2Window = [YeImageViewerTestNativeV1365]::FindProcessWindow(
        [uint32]$freshProcess.Id, "YeImageViewerSettingWnd")
    if ($unexpectedF2Window -ne [IntPtr]::Zero) {
        throw "Settings-shortcut regression failed: F2 still opened Settings."
    }
    Write-Host "PASS F2 no longer opens Settings."

    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0111, [UIntPtr]1010, [IntPtr]::Zero)
    $settingDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $settingWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$freshProcess.Id, "YeImageViewerSettingWnd")
    } while ($settingWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $settingDeadline)
    if ($settingWindow -eq [IntPtr]::Zero) {
        throw "Settings-layout regression failed: Settings window did not open."
    }
    $settingRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($settingWindow, [ref]$settingRect)
    $settingDpi = [YeImageViewerTestNativeV1365]::GetDpiForWindow($settingWindow)
    $expectedSettingWidth = [int][Math]::Round(1000 * 96.0 / $settingDpi)
    $expectedSettingHeight = [int][Math]::Round(700 * 96.0 / $settingDpi)
    if (($settingRect.Right - $settingRect.Left) -ne $expectedSettingWidth -or
        ($settingRect.Bottom - $settingRect.Top) -ne $expectedSettingHeight -or
        -not [YeImageViewerTestNativeV1365]::IsWindowEnabled($settingWindow)) {
        throw "Settings-layout regression failed: Settings canvas dimensions or interaction state changed."
    }
    Write-Host "PASS Settings opens with the tested compact 1000x700 layout."

    [void][YeImageViewerTestNativeV1365]::SendMessage($freshWindow, 0x0112, [UIntPtr]0xF060, [IntPtr]::Zero)
    if (-not $freshProcess.WaitForExit(3000)) {
        throw "Window-close regression failed: restored title-bar close left a residual process."
    }
    Write-Host "PASS restored title-bar close exits without a residual process."
    $freshProcess = Start-Process -FilePath $freshViewer -ArgumentList ('"' + $hdrFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $freshProcess.Refresh()
    } while (-not $freshProcess.HasExited -and $freshProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($freshProcess.HasExited -or $freshProcess.MainWindowHandle -eq 0 -or -not $freshProcess.Responding) {
        throw "Rotation restart regression failed: landscape HDR did not open."
    }

    $rotationWindow = [IntPtr]$freshProcess.MainWindowHandle
    $landscapeRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($rotationWindow, [ref]$landscapeRect)
    $landscapeWidth = $landscapeRect.Right - $landscapeRect.Left
    $landscapeHeight = $landscapeRect.Bottom - $landscapeRect.Top
    if ($landscapeWidth -le $landscapeHeight) {
        throw "Rotation restart regression failed: HDR did not start in landscape orientation."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0100, [UIntPtr]0x51, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0101, [UIntPtr]0x51, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    [void]$freshProcess.CloseMainWindow()
    if (-not $freshProcess.WaitForExit(3000)) {
        throw "Rotation restart regression failed: rotated viewer did not close."
    }

    $freshProcess = Start-Process -FilePath $freshViewer -ArgumentList ('"' + $hdrFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $freshProcess.Refresh()
    } while (-not $freshProcess.HasExited -and $freshProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($freshProcess.HasExited -or $freshProcess.MainWindowHandle -eq 0 -or -not $freshProcess.Responding) {
        throw "Rotation restart regression failed: persisted portrait HDR did not reopen."
    }
    $rotationWindow = [IntPtr]$freshProcess.MainWindowHandle
    $rotationTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText($rotationWindow, $rotationTitle, $rotationTitle.Capacity)
    if (-not $rotationTitle.ToString().Contains("90")) {
        throw "Rotation restart regression failed: reopened HDR title did not report the saved quarter-turn."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0100, [UIntPtr]0x45, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($rotationWindow, 0x0101, [UIntPtr]0x45, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    Write-Host "PASS image rotation persists across a real process restart."
}
finally {
    if ($freshProcess -and -not $freshProcess.HasExited) {
        [void]$freshProcess.CloseMainWindow()
        if (-not $freshProcess.WaitForExit(3000)) {
            Stop-Process -Id $freshProcess.Id -Force
            $freshProcess.WaitForExit()
        }
    }
    foreach ($freshFile in @(
        $freshViewer,
        (Join-Path $freshDirectory "YeImageViewer.db"),
        (Join-Path $freshDirectory "YeImageViewer.rotations.db"),
        (Join-Path $freshDirectory "YeImageViewer.rotations.db.tmp")
    )) {
        if (Test-Path -LiteralPath $freshFile) {
            Remove-Item -LiteralPath $freshFile -Force
        }
    }
    if (Test-Path -LiteralPath $freshDirectory) {
        Remove-Item -LiteralPath $freshDirectory
    }
}

Write-Host "Checking current-image window restoration..."
$restoreTestDirectory = Join-Path ([IO.Path]::GetTempPath()) ("YeImageViewer-Current-Restore-" + [Guid]::NewGuid().ToString("N"))
$restoreTestViewer = Join-Path $restoreTestDirectory "YeImageViewer.exe"
$restoreTestProcess = $null
try {
    [void](New-Item -ItemType Directory -Path $restoreTestDirectory)
    Copy-Item -LiteralPath $viewer -Destination $restoreTestViewer
    $restoreTestProcess = Start-Process -FilePath $restoreTestViewer -ArgumentList ('"' + $currentRestoreFixtures[0] + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        Start-Sleep -Milliseconds 200
        $restoreTestProcess.Refresh()
    } while (-not $restoreTestProcess.HasExited -and $restoreTestProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($restoreTestProcess.HasExited -or $restoreTestProcess.MainWindowHandle -eq 0 -or -not $restoreTestProcess.Responding) {
        throw "Current-image restore regression failed: viewer did not open the five-image fixture set."
    }

    $restoreWindow = [IntPtr]$restoreTestProcess.MainWindowHandle
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    $firstRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($restoreWindow, [ref]$firstRect)
    $firstWidth = $firstRect.Right - $firstRect.Left
    $firstHeight = $firstRect.Bottom - $firstRect.Top
    if ($firstWidth -le 0 -or $firstHeight -le 0) {
        throw "Current-image restore regression failed: first image did not produce a framed client."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0112, [UIntPtr]0xF030, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
    for ($index = 0; $index -lt 4; $index++) {
        [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
        [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 350
    }
    $fifthTitle = New-Object Text.StringBuilder 2048
    [void][YeImageViewerTestNativeV1365]::GetWindowText($restoreWindow, $fifthTitle, $fifthTitle.Capacity)
    if (-not $fifthTitle.ToString().Contains("[5/5]")) {
        throw "Current-image restore regression failed: immersive browsing did not reach image 5."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 600
    $fifthStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($restoreWindow, -16).ToInt64()
    $fifthRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($restoreWindow, [ref]$fifthRect)
    $fifthWidth = $fifthRect.Right - $fifthRect.Left
    $fifthHeight = $fifthRect.Bottom - $fifthRect.Top
    $restoreMonitor = [YeImageViewerTestNativeV1365]::MonitorFromWindow($restoreWindow, 2)
    $restoreMonitorInfo = New-Object YeImageViewerTestNativeV1365+MONITORINFO
    $restoreMonitorInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($restoreMonitorInfo)
    [void][YeImageViewerTestNativeV1365]::GetMonitorInfo($restoreMonitor, [ref]$restoreMonitorInfo)
    $maximumRestoreWidth = [int](($restoreMonitorInfo.Work.Right - $restoreMonitorInfo.Work.Left) * 90 / 100)
    $maximumRestoreHeight = [int](($restoreMonitorInfo.Work.Bottom - $restoreMonitorInfo.Work.Top) * 90 / 100)
    if (($fifthStyle -band 0x00C00000) -eq 0 -or
        $fifthWidth -eq $firstWidth -or $fifthHeight -eq $firstHeight -or
        [Math]::Abs($fifthWidth - $maximumRestoreWidth) -gt 1 -or
        [Math]::Abs($fifthHeight - $maximumRestoreHeight) -gt 1) {
        throw "Current-image restore regression failed: image 5 did not determine the capped framed size."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0100, [UIntPtr]0x25, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($restoreWindow, 0x0101, [UIntPtr]0x25, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 450
    $normalBrowseRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($restoreWindow, [ref]$normalBrowseRect)
    if (($normalBrowseRect.Right - $normalBrowseRect.Left) -ne $fifthWidth -or
        ($normalBrowseRect.Bottom - $normalBrowseRect.Top) -ne $fifthHeight) {
        throw "Current-image restore regression failed: normal browsing changed the restored frame."
    }
    Write-Host "PASS immersive image 5 determines the capped framed size and normal browsing keeps it fixed."
}
finally {
    if ($restoreTestProcess -and -not $restoreTestProcess.HasExited) {
        [void]$restoreTestProcess.CloseMainWindow()
        if (-not $restoreTestProcess.WaitForExit(3000)) {
            Stop-Process -Id $restoreTestProcess.Id -Force
            $restoreTestProcess.WaitForExit()
        }
    }
    foreach ($restoreFile in @(
        $restoreTestViewer,
        (Join-Path $restoreTestDirectory "YeImageViewer.db"),
        (Join-Path $restoreTestDirectory "YeImageViewer.rotations.db"),
        (Join-Path $restoreTestDirectory "YeImageViewer.rotations.db.tmp")
    )) {
        if (Test-Path -LiteralPath $restoreFile) {
            Remove-Item -LiteralPath $restoreFile -Force
        }
    }
    if (Test-Path -LiteralPath $restoreTestDirectory) {
        Remove-Item -LiteralPath $restoreTestDirectory
    }
}

Write-Host "Opening the SVG background-selector fixture..."
$viewerProcess = $null
try {
    $viewerProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $sharpSvgFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)

    do {
        Start-Sleep -Milliseconds 200
        $viewerProcess.Refresh()
    } while (-not $viewerProcess.HasExited -and $viewerProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($viewerProcess.HasExited -or $viewerProcess.MainWindowHandle -eq 0 -or -not $viewerProcess.Responding) {
        throw "Background-selector regression failed: viewer did not open a responsive SVG window."
    }

    $backgroundCommands = @(1100, 1101, 1102, 1103, 1100)
    foreach ($command in $backgroundCommands) {
        [void][YeImageViewerTestNativeV1365]::SendMessage(
            [IntPtr]$viewerProcess.MainWindowHandle, 0x0111, [UIntPtr]$command, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 150
        $viewerProcess.Refresh()
        if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
            $processState = if ($viewerProcess.HasExited) {
                $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$viewerProcess.ExitCode), 0)
                "exited with 0x$('{0:X8}' -f $unsignedExitCode)"
            } else {
                "stopped responding"
            }
            throw "Background-selector regression failed after menu command ${command}: $processState."
        }
    }

    Write-Host "PASS SVG background modes switch without exiting or hanging."

    $window = [IntPtr]$viewerProcess.MainWindowHandle
    $presentationStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($presentationStyle -band 0x00C00000) -ne 0) {
        throw "Escape regression failed: SVG did not begin in borderless presentation mode."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $presentationRestoredStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($presentationRestoredStyle -band 0x00C00000) -eq 0) {
        throw "Escape regression failed: Escape did not leave presentation mode."
    }
    Write-Host "PASS Escape leaves the initial immersive presentation."

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x46, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $fullScreenStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($fullScreenStyle -band 0x00C00000) -ne 0) {
        throw "Escape regression failed: F did not enter borderless fullscreen."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $restoredStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or ($restoredStyle -band 0x00C00000) -eq 0) {
        throw "Escape regression failed: Escape did not restore the pre-fullscreen window."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0112, [UIntPtr]0xF030, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $maximizePresentationStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    $maximizePresentationExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -20).ToInt64()
    if (($maximizePresentationStyle -band 0x00C00000) -ne 0 -or
        ($maximizePresentationExtendedStyle -band 0x00200000) -eq 0 -or
        [YeImageViewerTestNativeV1365]::IsZoomed($window)) {
        throw "Presentation regression failed: maximize did not enter borderless presentation mode."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $maximizeRestoredStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    $maximizeRestoredExtendedStyle = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -20).ToInt64()
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or ($maximizeRestoredStyle -band 0x00C00000) -eq 0 -or
        ($maximizeRestoredExtendedStyle -band 0x00200000) -eq 0 -or
        [YeImageViewerTestNativeV1365]::IsZoomed($window)) {
        throw "Presentation regression failed: Escape did not restore the pre-presentation frame."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Escape regression failed: default Escape preference closed the normal window."
    }
    Write-Host "PASS maximize enters presentation and Escape restores the framed window."

    $clientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$clientRect)
    $clientWidth = $clientRect.Right - $clientRect.Left
    $clientHeight = $clientRect.Bottom - $clientRect.Top
    $windowDpi = [YeImageViewerTestNativeV1365]::GetDpiForWindow($window)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $switchedClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$switchedClientRect)
    if (($switchedClientRect.Right - $switchedClientRect.Left) -ne $clientWidth -or
        ($switchedClientRect.Bottom - $switchedClientRect.Top) -ne $clientHeight) {
        throw "Framed-size regression failed: normal browsing changed the current fixed client size."
    }
    Write-Host "PASS normal image changes keep the current framed window size."

    $targetClientWidth = [int][Math]::Round($clientWidth * $windowDpi / 96.0)
    $targetClientHeight = [int][Math]::Round($clientHeight * $windowDpi / 96.0)
    $toolbarX = $targetClientWidth - 95
    $toolbarY = $targetClientHeight - 20
    $mousePosition = [IntPtr](($toolbarY -shl 16) -bor ($toolbarX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0200, [UIntPtr]::Zero, $mousePosition)
    Start-Sleep -Milliseconds 250
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Overlay regression failed: bottom toolbar hover was not responsive."
    }
    Write-Host "PASS compact bottom toolbar hover remains responsive."

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0112, [UIntPtr]0xF030, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    $presentationAfterMaximize = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if (($presentationAfterMaximize -band 0x00C00000) -ne 0) {
        throw "Presentation overlay regression failed: maximize did not re-enter presentation."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0111, [UIntPtr]1010, [IntPtr]::Zero)
    $settingDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 100
        $presentationSettingWindow = [YeImageViewerTestNativeV1365]::FindProcessWindow(
            [uint32]$viewerProcess.Id, "YeImageViewerSettingWnd")
    } while ($presentationSettingWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $settingDeadline)
    if ($presentationSettingWindow -eq [IntPtr]::Zero) {
        throw "Presentation overlay regression failed: Settings did not open over presentation."
    }
    [void][YeImageViewerTestNativeV1365]::SetWindowPos(
        $presentationSettingWindow, [IntPtr]::Zero, 80, 80, 0, 0, 0x0015)
    [void][YeImageViewerTestNativeV1365]::SetWindowPos(
        $presentationSettingWindow, [IntPtr]::Zero, 420, 180, 0, 0, 0x0015)
    Start-Sleep -Milliseconds 300
    $viewerProcess.Refresh()
    $styleAfterSettingMove = [YeImageViewerTestNativeV1365]::GetWindowLongPtr($window, -16).ToInt64()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding -or
        ($styleAfterSettingMove -band 0x00C00000) -ne 0) {
        throw "Presentation overlay regression failed after moving Settings over the image."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage(
        $presentationSettingWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    Write-Host "PASS moving Settings over the image keeps presentation stable."

    $presentationClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$presentationClientRect)
    $presentationWidth = $presentationClientRect.Right - $presentationClientRect.Left
    $closeX = $presentationWidth - 12 - 21
    $closeY = 12 + 21
    $closePosition = [IntPtr](($closeY -shl 16) -bor ($closeX -band 0xFFFF))
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0200, [UIntPtr]::Zero, $closePosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0201, [UIntPtr]1, $closePosition)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0202, [UIntPtr]0, $closePosition)
    if (-not $viewerProcess.WaitForExit(3000)) {
        throw "Presentation close regression failed: persistent close button did not exit."
    }
    Write-Host "PASS persistent presentation close button exits cleanly."
}
finally {
    if ($viewerProcess -and -not $viewerProcess.HasExited) {
        [void]$viewerProcess.CloseMainWindow()
        if (-not $viewerProcess.WaitForExit(3000)) {
            Stop-Process -Id $viewerProcess.Id -Force
            $viewerProcess.WaitForExit()
        }
    }
}

$existingViewer = Get-CimInstance Win32_Process -Filter "Name='YeImageViewer.exe'" |
    Where-Object { $_.ExecutablePath -eq $viewer }
if ($existingViewer) {
    throw "Close the YeImageViewer instance running from $viewer before starting the crash regression test."
}

Write-Host "Opening the DJI MotionPhoto crash fixture..."
$viewerProcess = $null
try {
    $viewerProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $crashFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)

    do {
        Start-Sleep -Milliseconds 200
        $viewerProcess.Refresh()
    } while (-not $viewerProcess.HasExited -and $viewerProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($viewerProcess.HasExited) {
        $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$viewerProcess.ExitCode), 0)
        throw "DJI MotionPhoto regression failed: viewer exited with 0x$('{0:X8}' -f $unsignedExitCode)."
    }
    if ($viewerProcess.MainWindowHandle -eq 0) {
        throw "DJI MotionPhoto regression failed: viewer did not create a window before the timeout."
    }
    if (-not $viewerProcess.Responding) {
        throw "DJI MotionPhoto regression failed: viewer window is not responding."
    }

    Start-Sleep -Seconds 2
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "DJI MotionPhoto regression failed: viewer did not remain responsive."
    }

    Write-Host "PASS DJI MotionPhoto remains open and responsive."
}
finally {
    if ($viewerProcess -and -not $viewerProcess.HasExited) {
        [void]$viewerProcess.CloseMainWindow()
        if (-not $viewerProcess.WaitForExit(3000)) {
            Stop-Process -Id $viewerProcess.Id -Force
            $viewerProcess.WaitForExit()
        }
    }
}

Write-Host "Opening the enlarged-text interpolation fixture..."
$viewerProcess = $null
try {
    $viewerProcess = Start-Process -FilePath $viewer -ArgumentList ('"' + $jaggedFixture + '"') -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(6)

    do {
        Start-Sleep -Milliseconds 200
        $viewerProcess.Refresh()
    } while (-not $viewerProcess.HasExited -and $viewerProcess.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

    if ($viewerProcess.HasExited) {
        $unsignedExitCode = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$viewerProcess.ExitCode), 0)
        throw "Raster interpolation regression failed: viewer exited with 0x$('{0:X8}' -f $unsignedExitCode)."
    }
    if ($viewerProcess.MainWindowHandle -eq 0 -or -not $viewerProcess.Responding) {
        throw "Raster interpolation regression failed: viewer did not open a responsive window."
    }

    Start-Sleep -Seconds 1
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Raster interpolation regression failed: viewer did not remain responsive."
    }

    Write-Host "PASS enlarged-text interpolation fixture remains open and responsive."
}
finally {
    if ($viewerProcess -and -not $viewerProcess.HasExited) {
        [void]$viewerProcess.CloseMainWindow()
        if (-not $viewerProcess.WaitForExit(3000)) {
            Stop-Process -Id $viewerProcess.Id -Force
            $viewerProcess.WaitForExit()
        }
    }
}

Write-Host "All regression tests passed."
