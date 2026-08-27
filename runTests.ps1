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

foreach ($requiredFile in @($viewer, $unitTests, $crashFixture)) {
    if (-not (Test-Path -LiteralPath $requiredFile)) {
        throw "Required regression-test file is missing: $requiredFile"
    }
}

Write-Host "Running MotionPhoto parser tests..."
& $unitTests
if ($LASTEXITCODE -ne 0) {
    throw "MotionPhoto parser tests failed with exit code $LASTEXITCODE."
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

Write-Host "All regression tests passed."
