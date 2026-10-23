param([switch]$ExpectMissing)
$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '../YeImageViewer/src/main.cpp'
$source=[IO.File]::ReadAllText($path)
$match=[regex]::Match($source,'(?s)void initOpenFile\([^)]*\)\s*\{(.*?)namespace fs = std::filesystem;')
if(-not $match.Success){throw '无法唯一识别重新打开图片的生产入口'}
function Has-DockReset([string]$body) {
    $withoutComments=[regex]::Replace($body,'(?s)/\*.*?\*/|//[^\r\n]*','')
    return [regex]::IsMatch($withoutComments,'(?m)^\s*psdDock\.close\(\);')
}
$prefix=$match.Groups[1].Value
if($ExpectMissing){if(Has-DockReset $prefix){throw '修复前入口已经包含清理，不能算有效对照'};Write-Host 'PASS 修复前生产入口缺少同路径重开时的PSD会话清理（源码契约对照）。';exit 0}
if(-not(Has-DockReset $prefix)){throw '重新打开图片前必须丢弃PSD临时会话，不能按路径字符串保留旧图层数据'}
foreach($broken in @($prefix.Replace('psdDock.close();',''),$prefix.Replace('psdDock.close();','// psdDock.close();'))) {
    if(Has-DockReset $broken){throw 'PSD入口契约未捕获缺失调用或注释伪装'}
}
Write-Host 'PASS 同路径重开入口先清理PSD临时会话；缺失调用和注释伪装均被捕获。此项为源码契约，不冒称真实重开窗口验收。'
