param([Parameter(Mandatory)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
& (Join-Path $PSScriptRoot 'test-color-candidate-package.ps1') -PackageDirectory $PackageDirectory -IntegrityOnly
if($LASTEXITCODE -ne 0){throw '候选包完整性失败'}
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$version=(Get-Item -LiteralPath (Join-Path $package 'YeImageViewer.exe')).VersionInfo.ProductVersion
$root=Join-Path ([IO.Path]::GetTempPath()) ('YeImageViewer-CandidateOffscreen-'+[Guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath (Join-Path $package "YeImageViewer-v$version-win-x64-portable.zip") -DestinationPath $root
$viewer=Join-Path $root 'YeImageViewer.exe'
$env:YEIMAGEVIEWER_COLOR_DATA_DIR=Join-Path $root 'color-data'
function Invoke-Offscreen([string[]]$Arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new($viewer);$start.UseShellExecute=$false
    foreach($argument in $Arguments){[void]$start.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::Start($start)
    try {if(-not $process.WaitForExit(90000)){$process.Kill();throw '解压后离屏专项超时'}
        if($process.ExitCode -ne 0){throw "解压后离屏专项失败：$($Arguments -join ' ')；证据：$root"}
    }finally{$process.Dispose()}
}
Invoke-Offscreen @('--original-save-selftest',(Join-Path $root 'save-evidence'))
Invoke-Offscreen @('--color-panel-offscreen-selftest',(Join-Path $root 'color-panel.txt'))
Invoke-Offscreen @('--picker-layout-selftest',(Join-Path $root 'picker-layout.txt'))
$fixture=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../test/image-tools/visibility-stack.psd')).Path
$hash=(Get-FileHash -LiteralPath $fixture).Hash
Invoke-Offscreen @('--psd-dock-selftest',$fixture,(Join-Path $root 'dock-evidence'))
if((Get-FileHash -LiteralPath $fixture).Hash -ne $hash){throw '解压包改写了只读图层夹具'}
& (Join-Path $PSScriptRoot 'test-transform-offscreen.ps1') -Viewer $viewer
if($LASTEXITCODE -ne 0){throw '解压后变换/保存/颜色专项失败'}
Write-Host "PASS 完整ZIP解压后：生产保存、颜色交互/悬停、HUD布局、图层组件、192组绘制离屏专项；各项数量见结果文件，未执行桌面输入。直接解压证据：$root"
