# 从2x2三平铺层最小夹具生成四通道CMYK：原alpha通道改为K，无透明度。
# 仅生成测试素材，不涉及用户PSD/PSB；图层流/物理顺序保持原样。
$ErrorActionPreference='Stop'
$fixtureBytes=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'visibility-stack.psd'))
$script:fixtureOffset=26
function Read-Field([int]$Count){
    [uint64]$value=0
    for($i=0;$i -lt $Count;$i++){
        if($script:fixtureOffset -ge $fixtureBytes.Length){throw '夹具截断'}
        $value=($value -shl 8)-bor $fixtureBytes[$script:fixtureOffset];$script:fixtureOffset++
    }
    return $value
}
$length=Read-Field 4;$script:fixtureOffset+=$length
$length=Read-Field 4;$script:fixtureOffset+=$length
$sectionLength=Read-Field 4
$sectionStart=$script:fixtureOffset
$layerLength=Read-Field 4
$layerCount=Read-Field 2
if($layerCount -ne 3){throw '最小夹具应为3层'}
for($layer=0;$layer -lt $layerCount;$layer++){
    $script:fixtureOffset+=16
    $count=Read-Field 2
    if($count -ne 4){throw '通道数应为4'}
    for($channel=0;$channel -lt $count;$channel++){
        $idOffset=$script:fixtureOffset;$id=Read-Field 2
        if($id -eq 65535){$fixtureBytes[$idOffset]=0;$fixtureBytes[$idOffset+1]=3}
        [void](Read-Field 4)
    }
    $script:fixtureOffset+=12
    $extra=Read-Field 4;$script:fixtureOffset+=$extra
}
$script:fixtureOffset=$sectionStart+$sectionLength
if((Read-Field 2) -ne 0){throw '仅接受无压缩合成夹具'}
$fixtureBytes[12]=0;$fixtureBytes[13]=4
$fixtureBytes[24]=0;$fixtureBytes[25]=4
$generated=[byte[]]::new($fixtureBytes.Length+4)
[Array]::Copy($fixtureBytes,$generated,$fixtureBytes.Length)
for($i=$fixtureBytes.Length;$i -lt $generated.Length;$i++){$generated[$i]=255}
$destination=Join-Path $PSScriptRoot 'readonly-cmyk.psd'
if(Test-Path -LiteralPath $destination){
    if([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([IO.File]::ReadAllBytes($destination))) -ne
       [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($generated))){throw '已有夹具不同，拒绝覆盖'}
}else{[IO.File]::WriteAllBytes($destination,$generated)}
Write-Host 'PASS 生成2x2三图层CMYK只读前置能力最小夹具'
