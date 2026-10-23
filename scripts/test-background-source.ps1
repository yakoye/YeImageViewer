param([string]$RepoRoot=(Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
$source=[IO.File]::ReadAllText((Join-Path $RepoRoot 'YeImageViewer/src/D3D11App.cpp'))
$handler=[regex]::Match($source,'(?s)case WM_SETTINGCHANGE:(.*?)break;').Groups[1].Value
if($handler -notmatch 'GlobalVar::currentTheme\s*=\s*GlobalVar::isCurrentUIDarkMode\s*\?\s*deepTheme\s*:\s*lightTheme') {
    throw '系统配色变化后必须同步更新实际画布调色板，不能只更新深色标志让自动棋盘格停在旧配色'
}
Write-Host 'PASS 系统主题通知同步更新画布调色板，自动棋盘格不保留旧配色。'
$settings=[IO.File]::ReadAllText((Join-Path $RepoRoot 'YeImageViewer/include/jarkUtils.h'))
function Test-Default([string]$Text){return $Text -match 'uint32_t backgroundMode = static_cast<uint32_t>\(BackgroundMode::Checkerboard\);' -and $Text.Contains('static_assert(sizeof(SettingParameter) == 4096')}
if(-not(Test-Default $settings)){throw '图片背景必须默认棋盘格，且保留4096字节设置ABI断言'}
if(Test-Default ($settings.Replace('backgroundMode = static_cast<uint32_t>(BackgroundMode::Checkerboard)','backgroundMode = static_cast<uint32_t>(BackgroundMode::Transparent)'))){throw '未捕获旧透明默认值反例'}
$strings=[IO.File]::ReadAllText((Join-Path $RepoRoot 'YeImageViewer/src/stringRes.cpp'))
if(-not $strings.Contains('{L"图片背景色", L"Image background", L"圖片背景色"}')){throw '背景菜单必须明确命名为图片背景色'}
Write-Host 'PASS 图片背景默认棋盘格、三语菜单名称、固定设置ABI及旧默认值反例；不等同于系统材质实屏验收。'
