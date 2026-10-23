param([Parameter(Mandatory=$true)][string]$Cargo,[Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$upstream=Join-Path $repo 'artifacts/upstream-photocraft'
$metadata=(& $Cargo metadata --locked --format-version 1 --filter-platform x86_64-pc-windows-msvc --manifest-path (Join-Path $repo 'tools/layer-backend/Cargo.toml') | ConvertFrom-Json)
if($LASTEXITCODE){throw '无法导出图层后端依赖元数据'}
$nodes=@{};foreach($node in $metadata.resolve.nodes){$nodes[$node.id]=$node}
$wanted=[Collections.Generic.HashSet[string]]::new();$queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($metadata.resolve.root)
while($queue.Count){$id=$queue.Dequeue();if(-not $wanted.Add($id)){continue}
    foreach($dep in $nodes[$id].deps){if(@($dep.dep_kinds | Where-Object {$_.kind -ne 'dev'}).Count){$queue.Enqueue($dep.pkg)}}
}
$text=[Text.StringBuilder]::new()
[void]$text.AppendLine('YeImageViewer 图层与通道后端：直接使用 PhotoCraft io/doc/compose 等模块。')
[void]$text.AppendLine('上游固定版本：9b18f57344ff70d6f5aa640a0db7bc55596d5324；代码按 MIT 许可使用。')
[void]$text.AppendLine('本文件由 Cargo.lock 与实际目标平台依赖图生成，包含许可文本；不包含品牌资源。')
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$missing=[Collections.Generic.List[string]]::new()
function Append-License([string]$path,[string]$label){
    $full=[IO.Path]::GetFullPath($path);if(-not $seen.Add($full)){return}
    [void]$text.AppendLine("`n===== $label =====`n")
    [void]$text.AppendLine([IO.File]::ReadAllText($full))
}
foreach($name in @('LICENSE-MIT','LICENSE-APACHE','NOTICE','ATTRIBUTION.md')){Append-License (Join-Path $upstream $name) "PhotoCraft/$name"}
foreach($file in Get-ChildItem -LiteralPath $upstream -File -Recurse | Where-Object {$_.Name -match '^(LICENSE|LICENCE|COPYING|OFL|NOTICE)' -and $_.FullName -notmatch '[\\/]\.git[\\/]'}){
    Append-License $file.FullName ('PhotoCraft/'+[IO.Path]::GetRelativePath($upstream,$file.FullName))
}
foreach($package in $metadata.packages | Where-Object {$wanted.Contains($_.id)} | Sort-Object name,version){
    if($package.name -eq 'ye-layer-backend'){continue}
    [void]$text.AppendLine("`n===== dependency $($package.name) $($package.version) ; SPDX $($package.license) =====")
    $dir=Split-Path -Parent $package.manifest_path
    if($dir.StartsWith($upstream,[StringComparison]::OrdinalIgnoreCase)){[void]$text.AppendLine('PhotoCraft own source: MIT / Apache-2.0, license above.');continue}
    $files=@(Get-ChildItem -LiteralPath $dir -File | Where-Object Name -match '^(LICENSE|LICENCE|COPYING|OFL|NOTICE)')
    if($package.license_file){$file=Join-Path $dir $package.license_file;if(Test-Path -LiteralPath $file -PathType Leaf){$files+=Get-Item -LiteralPath $file}}
    if(-not $files.Count){
        $parent=$dir
        for($i=0;$i -lt 3 -and -not $files.Count;$i++){
            $parent=Split-Path -Parent $parent
            $files=@(Get-ChildItem -LiteralPath $parent -File | Where-Object Name -match '^(LICENSE|LICENCE|COPYING|OFL|NOTICE)')
        }
    }
    if(-not $files.Count){$files=@(Get-ChildItem -LiteralPath (Join-Path $repo 'third_party/layer-backend-licenses') -Filter "$($package.name)-$($package.version)*.txt" -File)}
    if(-not $files.Count){$missing.Add("$($package.name) $($package.version) $dir");continue}
    foreach($file in $files){Append-License $file.FullName "$($package.name)/$($file.Name)"}
}
if($missing.Count){throw ("依赖缺少许可全文，不能打包：`n"+($missing -join "`n"))}
[IO.File]::WriteAllText([IO.Path]::GetFullPath($Destination),$text.ToString(),[Text.UTF8Encoding]::new($false))
Write-Host "PASS 实际后端依赖与许可文本已生成：$($wanted.Count) 个依赖节点。"
