param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\YeImageViewer")
)

$ErrorActionPreference = "Stop"

$releaseDir = Join-Path $PSScriptRoot "x64\Release"
$sourceExe = Join-Path $releaseDir "YeImageViewer.exe"
$sourceProvider = Join-Path $releaseDir "YeThumbnailProvider.dll"

if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
    throw "Build output not found: $sourceExe. Run buildRelease.ps1 first."
}
if (-not (Test-Path -LiteralPath $sourceProvider -PathType Leaf)) {
    throw "Build output not found: $sourceProvider. Run buildRelease.ps1 first."
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null

$targetExe = Join-Path $InstallDir "YeImageViewer.exe"
$targetProvider = Join-Path $InstallDir "YeThumbnailProvider.dll"
Copy-Item -LiteralPath $sourceExe -Destination $targetExe -Force
Copy-Item -LiteralPath $sourceProvider -Destination $targetProvider -Force

$regsvr32 = Join-Path $env:SystemRoot "System32\regsvr32.exe"
$registerProcess = Start-Process -FilePath $regsvr32 -ArgumentList @("/s", $targetProvider) -Wait -PassThru
if ($registerProcess.ExitCode -ne 0) {
    throw "Thumbnail provider registration failed with exit code $($registerProcess.ExitCode)."
}

$appPathKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\YeImageViewer.exe"
New-Item -Path $appPathKey -Force | Out-Null
Set-Item -Path $appPathKey -Value $targetExe
New-ItemProperty -Path $appPathKey -Name "Path" -Value $InstallDir -PropertyType String -Force | Out-Null

$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$shortcutPath = Join-Path $startMenuDir "YeImageViewer.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $targetExe
$shortcut.WorkingDirectory = $InstallDir
$shortcut.IconLocation = "$targetExe,0"
$shortcut.Description = "YeImageViewer 图像查看器"
$shortcut.Save()

[PSCustomObject]@{
    Application = $targetExe
    ThumbnailProvider = $targetProvider
    StartMenuShortcut = $shortcutPath
}
