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

    # 登记到右键「打开方式」。不登记的话，用户每次都得翻到 exe 的安装路径手动选一次。
    # 扩展名列表在程序里（SettingParameter::defaultExtList），所以交给 exe 自己写注册表，
    # 免得脚本里再抄一份、两边走偏。这只是加入候选列表，不会抢走默认打开程序。
    $openWithProcess = Start-Process -FilePath $targetExe -ArgumentList @("--register-open-with") -Wait -PassThru
    if ($openWithProcess.ExitCode -ne 0) {
        throw "Open-with registration failed with exit code $($openWithProcess.ExitCode)."
    }
    $registered = $true
}

$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
$shortcutPath = Join-Path $startMenuDir "YeImageViewer.lnk"
$shell = New-Object -ComObject WScript.Shell
# 写 .lnk 偶尔会失败：资源管理器正在读那个目录、同步盘（OneDrive 之类）正占着文件，
# 都会让 Save() 抛「无法保存快捷方式」。这不该让整次安装失败——程序本体已经装好了，
# 快捷方式晚一点再写就是。重试几次，仍然不行就说清楚并继续。
function Save-ShortcutWithRetry {
    param(
        [Parameter(Mandatory = $true)] $Shortcut,
        [Parameter(Mandatory = $true)] [string]$Path,
        [int]$Attempts = 5
    )

    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        try {
            $Shortcut.Save()
            return $true
        }
        catch {
            if ($attempt -eq $Attempts) {
                Write-Warning ("无法写入快捷方式 {0}：{1}。程序本体已安装到 {2}，可手动创建快捷方式。" -f
                    $Path, $_.Exception.Message, $InstallDir)
                return $false
            }
            Start-Sleep -Milliseconds (200 * $attempt)
        }
    }
    return $false
}

if (-not $NoStartMenuShortcut) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $targetExe
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.IconLocation = "$targetExe,0"
    $shortcut.Description = "YeImageViewer 图像查看器"
    if (-not (Save-ShortcutWithRetry -Shortcut $shortcut -Path $shortcutPath)) {
        $shortcutPath = $null
    }
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
        if (-not (Save-ShortcutWithRetry -Shortcut $desktopShortcut -Path $desktopShortcutPath)) {
            $desktopShortcutPath = $null
        }
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
