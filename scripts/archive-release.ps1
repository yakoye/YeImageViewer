param(
    [Parameter(Mandatory)][string]$ReleaseDirectory,
    [string[]]$ArchiveRoots = @('\\192.168.7.100\Downloads\yakoye-release', 'C:\Users\color\Downloads\yakoye-release')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$source = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$exe = Join-Path $source 'YeImageViewer.exe'
$version = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?$') {throw '无法从 EXE 读取安全的版本目录名'}
$names = @('YeImageViewer.exe', "YeImageViewer-v$version-win-x64-portable.zip",
    "YeImageViewer-v$version-win-x64-portable.7z", 'SHA256SUMS.txt')
$files = foreach ($name in $names) {Get-Item -LiteralPath (Join-Path $source $name)}
$failures = @()
foreach ($root in $ArchiveRoots) {
    try {
        $relative = if ($version.Contains('-')) {"YeImageViewer\候选版\v$version"} else {"YeImageViewer\v$version"}
        $destination = Join-Path $root $relative
        [void](New-Item -ItemType Directory -Path $destination -Force)
        foreach ($file in $files) {
            $target = Join-Path $destination $file.Name
            $expected = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            if (Test-Path -LiteralPath $target) {
                if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected) {
                    throw "已归档文件内容不同，拒绝覆盖历史版本：$target"
                }
            } else {Copy-Item -LiteralPath $file.FullName -Destination $target}
            if ((Get-Item -LiteralPath $target).Length -ne $file.Length -or
                (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected) {
                throw "归档大小或 SHA-256 不符：$target"
            }
        }
        Write-Host "PASS Release 产物大小与 SHA-256 全部一致：$destination"
    } catch {$failures += "${root}：$($_.Exception.Message)"}
}
if ($failures.Count) {throw ('Release 归档未全部完成：' + ($failures -join '; '))}
