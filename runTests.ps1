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
$toolbarIcons = @(
    (Join-Path $repoRoot "YeImageViewer\file\icons\previous.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\next.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\rotate-left.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\rotate-right.svg"),
    (Join-Path $repoRoot "YeImageViewer\file\icons\settings.svg")
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

foreach ($requiredFile in @($viewer, $unitTests, $crashFixture, $hdrFixture, $sharpSvgFixture, $textSvgFixture, $jaggedFixture) + $toolbarIcons) {
    if (-not (Test-Path -LiteralPath $requiredFile)) {
        throw "Required regression-test file is missing: $requiredFile"
    }
}

$expectedFileVersion = "1.36.5.0"
$actualFileVersion = (Get-Item -LiteralPath $viewer).VersionInfo.FileVersion
if ($actualFileVersion -ne $expectedFileVersion) {
    throw "Viewer file version mismatch: expected $expectedFileVersion, got $actualFileVersion."
}
Write-Host "PASS viewer file version is $expectedFileVersion."

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

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool IsZoomed(IntPtr window);

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

function Get-ExpectedFixedClientSize {
    param(
        [int]$WorkWidth,
        [int]$WorkHeight
    )

    $maximumWidth = [Math]::Max(1, [int][Math]::Floor($WorkWidth * 0.9))
    $height = [Math]::Max(1, [int][Math]::Floor($WorkHeight * 0.9))
    $width = [int][Math]::Floor($height * 4.0 / 3.0)
    if ($width -gt $maximumWidth) {
        $width = $maximumWidth
        $height = [int][Math]::Floor($width * 3.0 / 4.0)
    }

    return [PSCustomObject]@{
        Width = $width
        Height = $height
    }
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
    $expectedFreshSize = Get-ExpectedFixedClientSize `
        ($freshMonitorInfo.Work.Right - $freshMonitorInfo.Work.Left) `
        ($freshMonitorInfo.Work.Bottom - $freshMonitorInfo.Work.Top)
    if ([Math]::Abs(($freshClientRect.Right - $freshClientRect.Left) - $expectedFreshSize.Width) -gt 1 -or
        [Math]::Abs(($freshClientRect.Bottom - $freshClientRect.Top) - $expectedFreshSize.Height) -gt 1) {
        throw "Fresh-install regression failed: expected a $($expectedFreshSize.Width)x$($expectedFreshSize.Height) 4:3 client, got $(($freshClientRect.Right - $freshClientRect.Left))x$(($freshClientRect.Bottom - $freshClientRect.Top))."
    }
    Write-Host "PASS fresh install opens the fixed 4:3 monitor-relative window."

    [void]$freshProcess.CloseMainWindow()
    if (-not $freshProcess.WaitForExit(3000)) {
        throw "Rotation restart regression failed: initial fresh viewer did not close."
    }
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
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0112, [UIntPtr]0xF120, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250

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
    if (-not [YeImageViewerTestNativeV1365]::IsZoomed($window)) {
        throw "Escape regression failed: test window did not maximize."
    }
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or [YeImageViewerTestNativeV1365]::IsZoomed($window)) {
        throw "Escape regression failed: Escape did not restore the maximized window."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x1B, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $viewerProcess.Refresh()
    if ($viewerProcess.HasExited -or -not $viewerProcess.Responding) {
        throw "Escape regression failed: default Escape preference closed the normal window."
    }
    Write-Host "PASS Escape restores fullscreen/maximized states and does not close by default."

    $clientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$clientRect)
    $clientWidth = $clientRect.Right - $clientRect.Left
    $clientHeight = $clientRect.Bottom - $clientRect.Top
    $windowDpi = [YeImageViewerTestNativeV1365]::GetDpiForWindow($window)
    $windowMonitor = [YeImageViewerTestNativeV1365]::MonitorFromWindow($window, 2)
    $windowMonitorInfo = New-Object YeImageViewerTestNativeV1365+MONITORINFO
    $windowMonitorInfo.Size = [Runtime.InteropServices.Marshal]::SizeOf($windowMonitorInfo)
    [void][YeImageViewerTestNativeV1365]::GetMonitorInfo($windowMonitor, [ref]$windowMonitorInfo)
    $expectedClientSize = Get-ExpectedFixedClientSize `
        ($windowMonitorInfo.Work.Right - $windowMonitorInfo.Work.Left) `
        ($windowMonitorInfo.Work.Bottom - $windowMonitorInfo.Work.Top)
    if ([Math]::Abs($clientWidth - $expectedClientSize.Width) -gt 1 -or
        [Math]::Abs($clientHeight - $expectedClientSize.Height) -gt 1) {
        throw "Initial-size regression failed: expected a $($expectedClientSize.Width)x$($expectedClientSize.Height) 4:3 client, got ${clientWidth}x${clientHeight}."
    }

    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
    [void][YeImageViewerTestNativeV1365]::SendMessage($window, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $switchedClientRect = New-Object YeImageViewerTestNativeV1365+RECT
    [void][YeImageViewerTestNativeV1365]::GetClientRect($window, [ref]$switchedClientRect)
    if (($switchedClientRect.Right - $switchedClientRect.Left) -ne $clientWidth -or
        ($switchedClientRect.Bottom - $switchedClientRect.Top) -ne $clientHeight) {
        throw "Initial-size regression failed: switching to another image changed the fixed client size."
    }
    Write-Host "PASS image changes keep the fixed 4:3 monitor-relative window size."

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
