param([Parameter(Mandatory)][string]$PackageDirectory,[switch]$IntegrityOnly)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$version=(Get-Item -LiteralPath (Join-Path $package 'YeImageViewer.exe')).VersionInfo.ProductVersion
if($version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?$'){throw '只接受具有明确正式或候选版本的绿色包'}
$zip=Join-Path $package "YeImageViewer-v$version-win-x64-portable.zip"
$runtime=Join-Path ([IO.Path]::GetTempPath()) ('YeImageViewer-Package-'+[Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $runtime)
$env:YEIMAGEVIEWER_COLOR_DATA_DIR=Join-Path $runtime 'color-data'
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try {
    if($archive.Entries.Count -ne 15){throw '绿色包根目录文件数量不符'}
    foreach($entry in $archive.Entries){
        $target=[IO.Path]::GetFullPath((Join-Path $runtime $entry.FullName))
        if(-not $target.StartsWith($runtime+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
            $entry.FullName -match '[/\\]'){throw '绿色包应为根目录文件，不接受子目录或路径穿越'}
        if($entry.FullName -match '\.(db|ini|sqlite)(-(wal|shm|journal))?$|before-recovery|\.backup'){throw '不得携带开发配置、颜色历史、收藏或备份'}
    }
} finally {$archive.Dispose()}
Expand-Archive -LiteralPath $zip -DestinationPath $runtime
foreach($name in @('YeImageViewer.exe','YeThumbnailProvider.dll','YeLayerBackend.dll','LAYER_BACKEND_NOTICES.txt')) {
    $expected=if($name -eq 'YeImageViewer.exe'){Join-Path $package $name}else{Join-Path $PSScriptRoot "../x64/Release/$name"}
    if((Get-FileHash -LiteralPath $expected).Hash -ne (Get-FileHash -LiteralPath (Join-Path $runtime $name)).Hash){throw "包内文件与验收构建不一致：$name"}
}
foreach($item in @(@('屏幕快速取色.cmd','--screen-picker'),@('颜色面板.cmd','--color-panel'))) {
    $text=[IO.File]::ReadAllText((Join-Path $runtime $item[0]))
    if(-not $text.Contains(('start "" "%~dp0YeImageViewer.exe" '+$item[1]))){throw '相对路径启动器契约不符'}
}
$sevenZip=Join-Path $env:ProgramFiles '7-Zip/7z.exe'
& $sevenZip t (Join-Path $package "YeImageViewer-v$version-win-x64-portable.7z")
if($LASTEXITCODE -ne 0){throw '7z完整性检查失败'}
Write-Host "PASS 15个根目录文件、启动器、干净配置、EXE/DLL/后端许可哈希及7z完整性。解压目录：$runtime"
if($IntegrityOnly){Write-Host '仅包完整性检查完成；未运行桌面专项，不等于完整候选验收通过。';return}
. (Join-Path $PSScriptRoot 'Wait-DesktopTestSlot.ps1')
Wait-YeViewerDesktopTestSlot
$viewer=Join-Path $runtime 'YeImageViewer.exe'
function Invoke-PackageProbe([string[]]$Arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new($viewer)
    $start.UseShellExecute=$false
    foreach($argument in $Arguments){[void]$start.ArgumentList.Add($argument)}
    $process=[Diagnostics.Process]::Start($start)
    try {
        if(-not $process.WaitForExit(30000)){$process.Kill();throw '解压包专项超时'}
        if(Test-Path -LiteralPath $Arguments[-1]){Get-Content -LiteralPath $Arguments[-1] | Write-Host}
        if($process.ExitCode -ne 0){throw "解压包专项失败：$($Arguments[0])，exit=$($process.ExitCode)"}
    }finally{$process.Dispose()}
}
Invoke-PackageProbe @('--picker-layout-selftest',(Join-Path $runtime 'layout.txt'))
Invoke-PackageProbe @('--screen-picker-selftest',(Join-Path $runtime 'screen.txt'))
Invoke-PackageProbe @('--image-tools-selftest',(Join-Path $PSScriptRoot '../test/image-tools/two-layers.psd'),(Join-Path $runtime 'tools.txt'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/format corpus/files/common.png') -Destination (Join-Path $runtime 'sample.png')
& (Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe') --picker-window-probe $viewer (Join-Path $runtime 'sample.png')
if($LASTEXITCODE -ne 0){throw "解压包入口/独立工具回归失败。保留运行区：$runtime"}
Write-Host "PASS 直接运行ZIP解压后的生产程序，无另拷开发EXE替代。运行区：$runtime"
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../test/image-tools/orientation-alpha.png') -Destination (Join-Path $runtime 'palette-source.png')
& (Join-Path $PSScriptRoot '../x64/Release/YeImageViewerTests.exe') --color-navigation-probe $viewer
if($LASTEXITCODE -ne 0){throw '解压包颜色导航/历史/收藏跨重启复验失败'}
