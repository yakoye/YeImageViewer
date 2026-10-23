param([string]$Library=(Join-Path $PSScriptRoot '../x64/Release/YeLayerBackend.dll'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Test-PortableImports([string[]]$Names){return @($Names | Where-Object {$_ -match '^(?i:VCRUNTIME|MSVCP|MSVCR)[0-9_]+[d]?\.dll$'}).Count -eq 0}
if((Test-PortableImports @('KERNEL32.dll','VCRUNTIME140.dll')) -or
   (Test-PortableImports @('MSVCP140_1.dll')) -or
   (Test-PortableImports @('vcruntime140_1.dll')) -or
   -not(Test-PortableImports @('KERNEL32.dll','ntdll.dll','api-ms-win-crt-runtime-l1-1-0.dll'))){throw '运行库依赖护栏没有捕获最小反例'}
$bytes=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Library))
function U16([int]$offset){if($offset -lt 0 -or $offset+2 -gt $bytes.Length){throw 'PE越界'};return [BitConverter]::ToUInt16($bytes,$offset)}
function U32([int]$offset){if($offset -lt 0 -or $offset+4 -gt $bytes.Length){throw 'PE越界'};return [BitConverter]::ToUInt32($bytes,$offset)}
if((U16 0) -ne 0x5A4D){throw '不是PE文件'}
$pe=[int](U32 60);if((U32 $pe) -ne 0x4550){throw 'PE签名错误'}
$optional=$pe+24;if((U16 $optional) -ne 0x20b){throw '图层后端必须是x64 PE32+'}
$count=U16 ($pe+6);$table=$optional+(U16 ($pe+20))
function Rva([uint32]$address){
    for($i=0;$i -lt $count;$i++){$section=$table+$i*40;$begin=U32 ($section+12);$size=U32 ($section+16)
        if($address -ge $begin -and ([uint64]$address-$begin) -lt $size){$offset=[uint64](U32 ($section+20))+$address-$begin;if($offset -ge $bytes.Length){throw 'PE RVA越界'};return [int]$offset}}
    throw 'PE RVA不属于有效文件节'
}
$import=Rva (U32 ($optional+120));$names=[Collections.Generic.List[string]]::new()
for($i=0;$i -lt 128;$i++){$entry=$import+$i*20;$nameRva=U32 ($entry+12);if(-not $nameRva){break};$offset=Rva $nameRva;$end=$offset
    while($end -lt $bytes.Length -and $end-$offset -lt 260 -and $bytes[$end]){$end++}
    if($end -ge $bytes.Length -or $end-$offset -ge 260){throw 'PE DLL名称越界'}
    $names.Add([Text.Encoding]::ASCII.GetString($bytes,$offset,$end-$offset))
}
if(-not $names.Count){throw '未读到导入表，不能将检查无效当成通过'}
if(-not(Test-PortableImports $names.ToArray())){throw ('绿色后端依赖额外VC运行库：'+($names -join ', '))}
Write-Host ('PASS 图层后端PE导入表无额外VC运行库；动态/大小写/拆分运行库反例有效：'+($names -join ', '))
