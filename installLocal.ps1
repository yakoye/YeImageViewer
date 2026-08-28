param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\YeImageViewer"),
    [switch]$NoDesktopShortcut,
    [switch]$NoStartMenuShortcut,
    [switch]$NoPrompt,
    [switch]$NoLaunch,
    [switch]$SkipRegistration
)

$ErrorActionPreference = "Stop"

$releaseDir = Join-Path $PSScriptRoot "x64\Release"
if (-not (Test-Path -LiteralPath (Join-Path $releaseDir "YeImageViewer.exe") -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $PSScriptRoot "YeImageViewer.exe") -PathType Leaf)) {
    # The one-click self-extracting installer places its three runtime files
    # together in a temporary directory before invoking this script.
    $releaseDir = $PSScriptRoot
}
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

$registered = $false
if (-not $SkipRegistration) {
    $regsvr32 = Join-Path $env:SystemRoot "System32\regsvr32.exe"
    $registerProcess = Start-Process -FilePath $regsvr32 -ArgumentList @("/s", $targetProvider) -Wait -PassThru
    if ($registerProcess.ExitCode -ne 0) {
        throw "Thumbnail provider registration failed with exit code $($registerProcess.ExitCode)."
    }

    $appPathKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\YeImageViewer.exe"
    New-Item -Path $appPathKey -Force | Out-Null
    Set-Item -Path $appPathKey -Value $targetExe
    New-ItemProperty -Path $appPathKey -Name "Path" -Value $InstallDir -PropertyType String -Force | Out-Null
    $registered = $true
}

$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$shortcutPath = Join-Path $startMenuDir "YeImageViewer.lnk"
$shell = New-Object -ComObject WScript.Shell
if (-not $NoStartMenuShortcut) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $targetExe
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.IconLocation = "$targetExe,0"
    $shortcut.Description = "YeImageViewer 图像查看器"
    $shortcut.Save()
}
else {
    $shortcutPath = $null
}

$desktopShortcutPath = $null
if (-not $NoDesktopShortcut) {
    $desktopDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::DesktopDirectory)
    if (-not [string]::IsNullOrWhiteSpace($desktopDirectory)) {
        $desktopShortcutPath = Join-Path $desktopDirectory "YeImageViewer.lnk"
        $desktopShortcut = $shell.CreateShortcut($desktopShortcutPath)
        $desktopShortcut.TargetPath = $targetExe
        $desktopShortcut.WorkingDirectory = $InstallDir
        $desktopShortcut.IconLocation = "$targetExe,0"
        $desktopShortcut.Description = "YeImageViewer 图像查看器"
        $desktopShortcut.Save()
    }
}

if (-not $NoPrompt) {
    $message = "YeImageViewer 安装完成。`n`n安装位置：$InstallDir`n开始菜单和桌面快捷方式已创建。"
    if (-not $NoLaunch) {
        $message += "`n`n点击确定后将打开 YeImageViewer。"
    }
    [void]$shell.Popup($message, 0, "YeImageViewer", 64)
}

if (-not $NoLaunch) {
    Start-Process -FilePath $targetExe -WorkingDirectory $InstallDir
}

[PSCustomObject]@{
    Application = $targetExe
    ThumbnailProvider = $targetProvider
    StartMenuShortcut = $shortcutPath
    DesktopShortcut = $desktopShortcutPath
    Registered = $registered
    Launched = -not $NoLaunch
}
