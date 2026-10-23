$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$main=[IO.File]::ReadAllText((Join-Path $root 'YeImageViewer/src/main.cpp'))
function Test-ScaleTransition([string]$Source) {
    $enter=[regex]::Match($Source,'(?s)void enterPresentationMode\(\)\s*\{(.*?)\n    void exitPresentationMode')
    $exit=[regex]::Match($Source,'(?s)void exitPresentationMode\(\)\s*\{(.*?)\n    (?:void|bool|HRESULT) ')
    if(-not $enter.Success -or -not $exit.Success){return $false}
    foreach($body in @($enter.Groups[1].Value,$exit.Groups[1].Value)) {
        $body=[regex]::Replace($body,'//[^\r\n]*|/\*.*?\*/','')
        $guard=$body.IndexOf('PresentationLayout::PreserveDisplayedScale scaleTransition(curPar,restoringFrameWithoutZoom);')
        if($guard -lt 0 -or $guard -gt $body.IndexOf('SetWindowLongPtrW(') -or
            $body.IndexOf('scaleTransition.finish();') -lt $body.LastIndexOf('SetWindowPos(') -or
            $body.Contains('applyPresentationImageLayout(') -or $body.Contains('applyViewingScale(')){return $false}
    }
    return $Source.Contains('if (restoringFrameWithoutZoom)')
}
if(-not (Test-ScaleTransition $main)){throw '普通/沉浸必须在全部窗口变更期间保留实际倍率，不能重新适应窗口'}
$broken=@(
    $main.Replace('PresentationLayout::PreserveDisplayedScale scaleTransition(curPar,restoringFrameWithoutZoom);',''),
    $main.Replace('scaleTransition.finish();','scaleTransition.finish(); applyPresentationImageLayout();'),
    $main.Replace('if (restoringFrameWithoutZoom)','if (false)')
)
foreach($case in $broken){if(Test-ScaleTransition $case){throw '倍率保持契约漏过破坏反例'}}
$baseline=Join-Path $root 'artifacts/rc17-before/main.cpp'
if((Test-Path -LiteralPath $baseline) -and (Test-ScaleTransition ([IO.File]::ReadAllText($baseline)))){throw '修复前快照不能通过新增的往返倍率契约'}
$calls=[regex]::Matches($main,'ImageViewTransform::forSourceImage\(').Count
if($calls -ne 3){throw "旋转缓存/缩小/阅读三个源坐标入口必须统一换算显示宽高，实际$calls"}
Write-Host 'PASS 普通/沉浸切换生产接线、3项破坏反例及旋转3个入口；源码契约不代替真实窗口验收。'
