$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$menu=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/src/D3D11App.cpp'))
$start=$menu.IndexOf('HMENU D3D11App::CreateContextMenu')
$end=$menu.IndexOf('void D3D11App::ShowContextMenu',$start)
if($start -lt 0 -or $end -le $start){throw '无法定位真实菜单构造器'}
$body=$menu.Substring($start,$end-$start)
function Test-MenuContract([string]$Text) {
    return $Text -notmatch 'toolsMenu' -and $Text -match 'if\(app && app->CanBrowseLayers\(\)\) AppendMenuW\(hMenu, MF_STRING, \(UINT_PTR\)ContextMenu::psdLayers'
}
if(-not(Test-MenuContract $body)){throw '菜单仍有轻工具中转或图层入口未按真实能力显示'}
$main=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/src/main.cpp'))
if($body -match 'ContextMenu::imageColorPicker' -or $main -match 'beginImagePicker|showColorMenu'){
    throw '新入口反例：图片取色或吸管中转菜单仍存在'
}
if($body -notmatch 'trW\(L"取色…", L"Pick color…", L"取色…"\)' -or
   $body -notmatch 'trW\(L"图层与通道", L"Layers and channels", L"圖層與通道"\)' -or
   $main -match '图片取色（屏幕取色见右键菜单）') {throw '菜单及吸管提示必须使用统一名称，不得遗留旧图片取色提示或PSD前缀'}
if($main -notmatch 'case ToolbarCommand::Command::ScreenPicker: openScreenPicker\(\); break;' -or
   $main -notmatch 'if\(colorMenuRequested\)\{colorMenuRequested=false;rightClickMenuRequested=false;openColorPanel\(\);return;\}'){
    throw '吸管左键必须直接屏幕取色，右键必须直接颜色面板'
}
if(Test-MenuContract ($body.Replace('if(app && app->CanBrowseLayers()) ',''))){throw '护栏漏掉无条件PSD入口'}
if(Test-MenuContract ($body+' toolsMenu')){throw '护栏漏掉中转菜单'}
$old=Join-Path $root 'artifacts/color-priority-rc8-20261020/before/D3D11App.cpp'
if(Test-Path -LiteralPath $old){if(Test-MenuContract ([IO.File]::ReadAllText($old))){throw '修复前菜单未触发反例，证据无效'}}
foreach($item in @(@('屏幕快速取色.cmd','--screen-picker'),@('颜色面板.cmd','--color-panel'))) {
    $text=[IO.File]::ReadAllText((Join-Path $root ('tools/'+$item[0]))).Replace("`r`n","`n")
    if($text.Trim() -ne ('@echo off'+"`n"+'start "" "%~dp0YeImageViewer.exe" '+$item[1])){throw ('便携工具启动脚本契约错误：'+$item[0])}
}
Write-Host 'PASS 真实菜单能力契约、两项破坏反例及两个相对路径独立启动脚本；此项不替代窗口运行验收。'
