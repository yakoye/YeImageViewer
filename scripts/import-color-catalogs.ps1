# 从已核对MIT授权的上游快照机械生成版本化数据及C++资源；不在产品运行时联网。
param([string]$SourceRoot=(Join-Path $PSScriptRoot '../artifacts'),[switch]$IncludeUserReference)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$output=Join-Path $root 'YeImageViewer/file/colors'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$rows=[Collections.Generic.List[string]]::new()
$rows.Add('# v1 | family | group | name | hex | keywords')
function Add-ColorRow($family,$group,$name,$hex,$keywords) {
    if($hex -notmatch '^#[0-9a-fA-F]{6}$'){throw "非法颜色 $hex"}
    $rows.Add((@($family,$group,$name,$hex.ToUpperInvariant(),$keywords) -join "`t"))
}
$oc=Get-Content (Join-Path $SourceRoot 'upstream-open-color/open-color.json') -Raw | ConvertFrom-Json
foreach($property in $oc.PSObject.Properties){$index=0;foreach($hex in @($property.Value)){Add-ColorRow 'ui' 'Open Color' ($property.Name+' '+$index) $hex $property.Name;$index++}}
$cn=Get-Content (Join-Path $SourceRoot 'upstream-chinese-colors/src/assets/colors.json') -Raw | ConvertFrom-Json
foreach($group in $cn){foreach($color in $group.colors){Add-ColorRow 'traditional' '中国' $color.name $color.hex $group.name}}
$jp=Get-Content (Join-Path $SourceRoot 'upstream-japanese-colors/src/colors.js') -Raw
foreach($match in [regex]::Matches($jp,"name: '([^']+)', reading: '([^']+)', hex: '(#[A-Fa-f0-9]{6})', category: '([^']+)'")){Add-ColorRow 'traditional' '日本' $match.Groups[1].Value $match.Groups[3].Value ($match.Groups[2].Value+' '+$match.Groups[4].Value)}
$gradients=[Collections.Generic.List[string]]::new()
if($IncludeUserReference){
    & node (Join-Path $PSScriptRoot 'inspect-reference-colors.mjs') --export
    if($LASTEXITCODE){throw '静态数据字面量解析失败'}
    $data=Get-Content (Join-Path $SourceRoot 'reference-static-colors.json') -Raw | ConvertFrom-Json -NoEnumerate
    $names=@('Fluent Design','Flat UI','Ant Design','Material Design')
    for($i=0;$i -lt 4;$i++){foreach($group in $data[$i]){$index=1;foreach($chunk in $group.colors){foreach($hex in @($chunk)){if($hex){Add-ColorRow 'ui' $names[$i] (($group.title -split "`n")[0]+' '+$index) $hex $group.title.Replace("`n",' ')};$index++}}}}
    $reference=Join-Path $root 'docs/color-picker/baseline-v2.2/reference/utools-color-helper-5.0.1/assets/index-CnOXSPuO.js'
    $text=Get-Content $reference -Raw
    foreach($match in [regex]::Matches($text,"(tc|nc|nT)=JSON.parse\('((?:\\.|[^'\\])*)'\)")){
        $data=$match.Groups[2].Value.Replace("\'","'") | ConvertFrom-Json -NoEnumerate
        if($match.Groups[1].Value -eq 'tc'){foreach($color in $data){Add-ColorRow 'traditional' '日本' $color.name $color.color ('参考包 '+$color.jname)}}
        if($match.Groups[1].Value -eq 'nc'){foreach($group in $data){foreach($color in $group.colors){Add-ColorRow 'traditional' '中国' $color.name $color.color ('参考包 '+$group.title)}}}
        if($match.Groups[1].Value -eq 'nT'){foreach($colors in $data){if($colors.Count -ge 2 -and $colors.Count -le 4){$normalized=@(foreach($hex in $colors){if($hex -match '^#([a-fA-F0-9])([a-fA-F0-9])([a-fA-F0-9])$'){$hex='#'+$Matches[1]*2+$Matches[2]*2+$Matches[3]*2};if($hex -notmatch '^#[a-fA-F0-9]{6}$'){throw '非法渐变'};$hex.ToUpperInvariant()});$gradients.Add(($normalized -join ','))}}}
    }
}
[IO.File]::WriteAllLines((Join-Path $output 'catalog-v1.tsv'),$rows,[Text.UTF8Encoding]::new($false))
$header="#pragma once`n// 由scripts/import-color-catalogs.ps1生成；修改源TSV后重新生成。`ninline constexpr char COLOR_CATALOG_TSV[]=R`"YECATALOG(`n"+($rows -join "`n")+"`n)YECATALOG`";`ninline constexpr char COLOR_GRADIENT_TSV[]=R`"YEGRADIENT(`n"+($gradients -join "`n")+"`n)YEGRADIENT`";`n"
[IO.File]::WriteAllLines((Join-Path $output 'gradients-v1.tsv'),$gradients,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $root 'YeImageViewer/include/ColorCatalogData.h'),$header,[Text.UTF8Encoding]::new($false))
$notices=[Collections.Generic.List[string]]::new();$notices.Add("# 色库来源与MIT许可`n`n色值为各上游提供的sRGB近似，不是传统染料的测量标准；没有导入参考插件数据、描述、图片、音频。`n")
foreach($repo in @(@('upstream-open-color','https://github.com/yeun/open-color'),@('upstream-chinese-colors','https://github.com/zerosoul/chinese-colors'),@('upstream-japanese-colors','https://github.com/sen-ltd/japanese-colors'))){$folder=Join-Path $SourceRoot $repo[0];$commit=& git -C $folder rev-parse HEAD;$notices.Add("## $($repo[1])`n`n固定提交：$commit`n`n"+(Get-Content (Join-Path $folder 'LICENSE') -Raw))}
$notices.Add("`n数据SHA-256："+(Get-FileHash (Join-Path $output 'catalog-v1.tsv')).Hash)
if($IncludeUserReference){$notices.Add("`n## 用户提供的颜色助手5.0.1参考包数据`n`n本次用户明确同意提取参考数据库。仅提取静态色名/色值/分类/渐变端点，不执行脚本、不复制应用代码、图像或品牌。来源uTools团队颜色助手5.0.1，参考脚本SHA-256："+(Get-FileHash $reference).Hash+"。此部分不宣称属于上述MIT授权，不据此声称第三方已授予公开再分发权；v1.39.0按用户明确要求保留当前数据；本记录不构成对第三方授权范围的独立确认。渐变仅导入2～4色标数据，其他复杂条目不截断。")}
[IO.File]::WriteAllText((Join-Path $output 'SOURCES.md'),($notices -join "`n"),[Text.UTF8Encoding]::new($false))
$noticePath=Join-Path $root 'THIRD_PARTY_NOTICES.md';$existing=[IO.File]::ReadAllText($noticePath);$marker='<!-- COLOR-CATALOG-NOTICES -->';$at=$existing.IndexOf($marker);if($at -ge 0){$existing=$existing.Substring(0,$at)}
[IO.File]::WriteAllText($noticePath,$existing.TrimEnd()+"`n`n"+$marker+"`n"+($notices -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
Write-Host "已生成 $($rows.Count-1) 条色卡；许可和提交见 $output/SOURCES.md"
