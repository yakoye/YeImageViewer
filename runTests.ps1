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

foreach ($requiredFile in @($viewer, $unitTests, $crashFixture, $hdrFixture, $sharpSvgFixture, $textSvgFixture, $jaggedFixture)) {
    if (-not (Test-Path -LiteralPath $requiredFile)) {
        throw "Required regression-test file is missing: $requiredFile"
    }
}

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

& $unitTests $hdrFixture $sharpSvgFixture $textSvgFixture
if ($LASTEXITCODE -ne 0) {
    throw "Unit regression tests failed with exit code $LASTEXITCODE."
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
