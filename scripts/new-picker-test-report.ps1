param([string]$OutputDirectory=(Join-Path $PSScriptRoot '../artifacts/color-picker-rc6'),
    [string]$ScreenEvidence=(Join-Path $PSScriptRoot '../artifacts/color-picker-rc6-screen-final.log'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$baseline=Join-Path $root 'docs/color-picker/baseline-v2/templates/Test-Case-Registry.csv'
$core=Join-Path $root 'artifacts/color-picker-rc6-core-final.log'
$screen=$ScreenEvidence
if((Get-Content -LiteralPath $core -Raw) -notmatch '103 passed, 0 failed' -or
   (Get-Content -LiteralPath $screen -Raw) -notmatch 'passed=64 failed=0' -or
   (Get-Content -LiteralPath $screen -Raw) -notmatch 'picker-integration passed=119 failed=0') {
    throw '原始专项证据不完整，不能生成通过结果'
}
$rows=@(Import-Csv -LiteralPath $baseline)
if($rows.Count -ne 259 -or @($rows.'Case ID'|Select-Object -Unique).Count -ne 259){throw '需求基线必须保留259个唯一编号'}
$passed=@('CP-001','CP-003','CP-004','CP-007','CP-013','CP-034','CP-036')
$hardware=@('CP-014','CP-015','CP-016','CP-017','CP-018','CP-019','CP-020','CP-021','CP-022','CP-023','CP-024','CP-028','CP-029','CP-030','CP-035','CP-041')
$partial=@('CP-002','CP-005','CP-010','CP-025','CP-026','CP-027','CP-031','CP-032','CP-033','CP-042','CP-043','CP-044','CP-045','CP-047','CP-243','CP-245','CP-249','CP-251','CP-255','CP-256','CP-257')
foreach($row in $rows) {
    if($row.'Case ID' -in $passed) {
        $row.Status='PASS';$row.'Execution Mode'='自动：当前单屏、真实窗口、窗口消息驱动'
        $row.Evidence=[IO.Path]::GetFileName($screen)+'；只覆盖本编号当前单屏场景，不推导物理输入/多屏通过'
    } elseif($row.'Case ID' -in $hardware) {
        $row.Status='BLOCKED';$row.Evidence='当前只有单屏96 DPI；无对应混合DPI/HDR/安全桌面测试条件。模拟几何不是此项通过证据'
    } elseif($row.'Case ID' -in $partial) {
        $row.Status='NOT_RUN';$row.Evidence='仅部分子项已覆盖：core 103、屏幕64、主窗口119断言；完整场景仍未验收，见候选报告'
    } else {
        $row.Status='NOT_RUN';$row.Evidence='尚未完整实现或未完整执行；保留M1–M7计划，不标通过'
    }
}
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$rows|Export-Csv -LiteralPath (Join-Path $OutputDirectory 'Test-Case-Registry.csv') -NoTypeInformation -Encoding utf8BOM
$rows|Group-Object Status|Select-Object Name,Count|Format-Table|Out-String|Write-Host
Write-Host "已生成259条原编号执行登记表：$OutputDirectory"
