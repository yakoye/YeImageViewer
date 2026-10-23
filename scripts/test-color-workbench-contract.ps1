$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$panel=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/include/ScreenColorWindow.h'))
$flow=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/include/ColorFlowView.h'))
$controller=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/include/ColorWorkbenchPanel.h'))
$picker=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/include/ScreenPickerSession.h'))
$main=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/src/main.cpp'))
function Test-Workbench([string]$Panel,[string]$Flow,[string]$Controller,[string]$Picker,[string]$Main) {
    return $Panel.Contains('ColorWorkbenchPanel library;') -and -not $Panel.Contains('catalog.create(') -and
        $Panel.Contains('ColorToolHost::launch(') -and -not $Main.Contains('ScreenColorWindow::requestExit(') -and
        $Controller.Contains('place(Search,6,12,') -and $Controller.Contains('searchWorker=std::jthread') -and
        $Controller.Contains('view.jump(item.key)') -and -not ($Controller -match 'L"(上一页|下一页|刷新|保存设置|颜色页)"') -and
        $Flow.Contains('layout_.firstVisible(offset)') -and $Flow.Contains('case WM_MOUSEWHEEL:') -and $Flow.Contains('RECT thumb()const') -and
        $Picker -notmatch 'LoadKeyboardLayout|ActivateKeyboardLayout|UnloadKeyboardLayout' -and $Picker.Contains('ImmAssociateContext(m->window,nullptr)') -and
        $Panel.Contains('ImmAssociateContextEx(child,nullptr,0)') -and
        $Picker.Contains('SetWindowsHookExW(WH_KEYBOARD_LL') -and $Picker.Contains('UnhookWindowsHookEx(keyboardHook)') -and
        $Picker.IndexOf('beginInput();') -ge 0 -and $Picker.IndexOf('beginInput();') -lt $Picker.IndexOf('for(auto& m:monitors) ShowWindow')
}
if(-not(Test-Workbench $panel $flow $controller $picker $main)){throw '颜色工作台生产接线/独立生命周期/连续视口/全局搜索/输入清理契约失败'}
$cases=@(
    @($panel.Replace('ColorWorkbenchPanel library;','ColorLibraryPanel library;'),$flow,$controller,$picker,$main),
    @($panel,$flow,$controller,$picker,($main+' ScreenColorWindow::requestExit();')),
    @($panel,$flow,$controller.Replace('searchWorker=std::jthread','searchWorkerRemoved'),$picker,$main),
    @($panel,$flow.Replace('layout_.firstVisible(offset)','0'),$controller,$picker,$main),
    @($panel,$flow,$controller,$picker.Replace('UnhookWindowsHookEx(keyboardHook)','missingCleanup()'),$main),
    @($panel,$flow,($controller+' L"下一页"'),$picker,$main),
    @($panel,$flow,$controller,($picker.Replace('beginInput();','/* input delayed */')+' beginInput();'),$main),
    @($panel,$flow,$controller,($picker+' LoadKeyboardLayoutW(L"00000409",0);'),$main),
    @($panel,$flow,$controller,($picker+' ActivateKeyboardLayout(layout,0);'),$main),
    @($panel.Replace('ImmAssociateContextEx(child,nullptr,0)',''),$flow,$controller,$picker,$main)
)
$index=0
foreach($case in $cases){$index++;if(Test-Workbench @case){throw "颜色工作台契约漏过破坏反例$index"}}
Write-Host 'PASS 颜色工作台生产接线及10项内存反例（禁止加载/切换键盘布局，仅禁用取色窗口/数值框IME）；不替代真实输入验收。'
