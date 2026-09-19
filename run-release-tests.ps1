<#
.SYNOPSIS
    发布闸门：一条命令跑完全部测试，给出 PASS / FAIL（测试规格 Phase 7）。

.DESCRIPTION
    把各环节串起来并汇总产出，存在发布阻断失败时退出码非 0，CI 可据此拦住发布。

    环节（标「阻断」的失败会拦住发布）：
      1. Release 构建                                    阻断
      2. runTests.ps1：单元测试 + 窗口行为 + 格式语料       阻断
      3. 语料测试 run-tests.ps1 -All                     阻断
      4. 大图渐进加载探针                                 阻断
      5. 翻页响应性探针                                   阻断
      6. 实况照片声音探针（没有音频设备时记为跳过）          阻断
      7. 性能压测 run-performance.ps1                    阻断
    产出写到 artifacts/release-gate/：
      summary.md        给人看的总结
      results.json      给机器读的结果（含语料的逐用例数据）
      performance.csv   各档位的性能与资源数据
      stages/*.log      每个环节的完整输出

    耗时参考（本机）：不含性能压测约 10~15 分钟；含 10000 张压测再加 15~25 分钟，
    其中大部分是首次生成素材，之后复用。

    性能压测期间不要操作机器：CPU 和内存会被前台程序干扰。

.EXAMPLE
    .\run-release-tests.ps1                      # 全量
    .\run-release-tests.ps1 -SkipBuild           # 复用已有构建
    .\run-release-tests.ps1 -SkipPerformance     # 跳过压测，适合日常提交前自检
    .\run-release-tests.ps1 -PerformanceCount 100,1000
#>
param(
    [switch]$SkipBuild,
    [switch]$SkipPerformance,
    [int[]]$PerformanceCount = @(100, 1000, 10000),
    [int]$PerformanceSwitches = 200,
    [string]$OutputDir
)

$ErrorActionPreference = "Continue"   # 单个环节失败要继续跑完其余环节，最后统一判定
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot "artifacts\release-gate" }
$stageDir = Join-Path $OutputDir "stages"
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

$viewer = Join-Path $repoRoot "x64\Release\YeImageViewer.exe"
$corpusReportDir = Join-Path $OutputDir "corpus"

# 残留进程会让后续环节的构建和测量全部失真，每个环节前后都清一遍
function Stop-ViewerProcesses {
    Get-Process YeImageViewer -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_.Kill(); [void]$_.WaitForExit(3000) } catch {}
    }
}

$stages = @()

function Invoke-Stage {
    param(
        [string]$Name,
        [string]$LogName,
        [bool]$Blocking,
        [scriptblock]$Body,
        [string]$SkipReason = ''
    )

    if ($SkipReason) {
        Write-Host ("[跳过] {0} —— {1}" -f $Name, $SkipReason) -ForegroundColor Yellow
        $script:stages += [pscustomobject]@{
            name = $Name; status = 'SKIPPED'; blocking = $Blocking
            exitCode = $null; seconds = 0; log = ''; detail = $SkipReason
        }
        return
    }

    Write-Host ("[运行] {0}" -f $Name) -ForegroundColor Cyan
    Stop-ViewerProcesses
    $logPath = Join-Path $stageDir $LogName
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    $output = @()
    $exitCode = 0
    try {
        $output = & $Body 2>&1 | ForEach-Object { [string]$_ }
        $exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }
    }
    catch {
        # 环节自身抛异常（例如 runTests.ps1 的 throw）同样算失败，并把原因留在日志里
        $output += ("环节异常：" + $_.Exception.Message)
        $exitCode = 1
    }
    $sw.Stop()
    Stop-ViewerProcesses

    ($output -join "`r`n") | Set-Content -LiteralPath $logPath -Encoding UTF8

    # 退出码 3 = 环节自己判定「未执行」（例如锁屏时读不到屏幕像素、素材不在本地）。
    # 必须和 PASS 分开：否则锁着屏跑一遍，一条阻断项会被当成通过放行。
    $status = switch ($exitCode) { 0 { 'PASS' } 3 { 'SKIPPED' } default { 'FAIL' } }
    $color = switch ($status) { 'PASS' { 'Green' } 'SKIPPED' { 'Yellow' } default { 'Red' } }
    Write-Host ("       {0}  {1:N1}s  日志 {2}" -f $status, $sw.Elapsed.TotalSeconds, $logPath) -ForegroundColor $color

    if ($status -eq 'SKIPPED') {
        $reason = @($output | Where-Object { $_ -match 'SKIPPED' } | Select-Object -First 1)
        $script:stages += [pscustomobject]@{
            name = $Name; status = 'SKIPPED'; blocking = $Blocking
            exitCode = $exitCode; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
            log = $logPath
            detail = if ($reason) { [string]$reason[0] } else { '环节自行判定未执行' }
        }
        Write-Host ("       | " + $(if ($reason) { $reason[0] } else { '' })) -ForegroundColor DarkGray
        return
    }

    # 失败时把日志末几行直接显示出来，不用先去翻文件
    if ($exitCode -ne 0) {
        $tail = @($output | Where-Object { $_ -match 'FAIL|ERROR|错误|异常|失败|阻断' } | Select-Object -Last 8)
        if ($tail.Count -eq 0) { $tail = @($output | Select-Object -Last 8) }
        foreach ($line in $tail) { Write-Host ("       | " + $line) -ForegroundColor DarkGray }
    }

    $script:stages += [pscustomobject]@{
        name = $Name; status = $status; blocking = $Blocking
        exitCode = $exitCode; seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        log = $logPath; detail = ''
    }
}

$gateStart = Get-Date
Write-Host "YeImageViewer 发布闸门"
Write-Host ("仓库：{0}" -f $repoRoot)
Write-Host ("产出：{0}" -f $OutputDir)
Write-Host ""

# ---------------------------------------------------------------- 1 构建
Invoke-Stage -Name "Release 构建" -LogName "01-build.log" -Blocking $true `
    -SkipReason $(if ($SkipBuild) { "指定了 -SkipBuild，复用已有构建" } else { '' }) `
    -Body { & (Join-Path $repoRoot "buildRelease.ps1") }

if (-not (Test-Path -LiteralPath $viewer)) {
    Write-Host ("找不到 {0}，后续环节无法进行。" -f $viewer) -ForegroundColor Red
    exit 2
}

# ---------------------------------------------------------------- 2 既有回归
# 单元测试、窗口行为、格式语料都在 runTests.ps1 里，它失败时用 throw，
# 所以这里靠 Invoke-Stage 的 catch 接住。
Invoke-Stage -Name "单元测试 + 窗口行为 + 格式语料（runTests.ps1）" -LogName "02-runtests.log" -Blocking $true `
    -Body { & (Join-Path $repoRoot "runTests.ps1") -SkipBuild }

# ---------------------------------------------------------------- 3 图片语料
Invoke-Stage -Name "图片语料全套（run-tests.ps1 -All）" -LogName "03-corpus.log" -Blocking $true `
    -Body { & (Join-Path $repoRoot "tools\image-test-runner\run-tests.ps1") -All -OutputDir $corpusReportDir }

# ---------------------------------------------------------------- 4~6 专项探针
Invoke-Stage -Name "大图渐进加载探针" -LogName "04-progressive.log" -Blocking $true `
    -Body { & (Join-Path $repoRoot "tools\image-test-runner\probe-progressive-load.ps1") }

Invoke-Stage -Name "翻页响应性探针" -LogName "05-paging.log" -Blocking $true `
    -Body { & (Join-Path $repoRoot "tools\image-test-runner\probe-paging-responsiveness.ps1") }

# 读音频会话电平判定：自动播放默认静音，空格重播与悬停「实况」标记出声
Invoke-Stage -Name "实况照片声音探针" -LogName "06-live-audio.log" -Blocking $true `
    -Body { & (Join-Path $repoRoot "tools\image-test-runner\probe-live-photo-audio.ps1") }

# ---------------------------------------------------------------- 7 性能压测
Invoke-Stage -Name "性能压测" -LogName "07-performance.log" -Blocking $true `
    -SkipReason $(if ($SkipPerformance) { "指定了 -SkipPerformance" } else { '' }) `
    -Body {
        & (Join-Path $repoRoot "tools\image-test-runner\run-performance.ps1") `
            -Count $PerformanceCount -Switches $PerformanceSwitches -OutputDir $OutputDir
    }

# ---------------------------------------------------------------- 汇总
$duration = [math]::Round(((Get-Date) - $gateStart).TotalSeconds, 1)
$blockingFailures = @($stages | Where-Object { $_.blocking -and $_.status -eq 'FAIL' })
$gatePassed = ($blockingFailures.Count -eq 0)

# 语料的逐用例结果并进来，机器读一个文件就够
$corpusResults = $null
$corpusResultsPath = Join-Path $corpusReportDir "results.json"
if (Test-Path -LiteralPath $corpusResultsPath) {
    try { $corpusResults = Get-Content -LiteralPath $corpusResultsPath -Raw -Encoding UTF8 | ConvertFrom-Json }
    catch { $corpusResults = $null }
}

$perfCsv = Join-Path $OutputDir "performance.csv"
$perfRows = @()
if (Test-Path -LiteralPath $perfCsv) {
    try { $perfRows = @(Import-Csv -LiteralPath $perfCsv) } catch { $perfRows = @() }
}

$results = [ordered]@{
    gate        = if ($gatePassed) { 'PASS' } else { 'FAIL' }
    generatedAt = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
    seconds     = $duration
    viewer      = $viewer
    stages      = @($stages)
    corpus      = $corpusResults
    performance = $perfRows
}
$resultsPath = Join-Path $OutputDir "results.json"
[IO.File]::WriteAllText($resultsPath, ($results | ConvertTo-Json -Depth 8),
    (New-Object Text.UTF8Encoding $false))

$md = @()
$md += "# YeImageViewer 发布闸门报告"
$md += ""
$md += ("- 结论：**{0}**" -f $(if ($gatePassed) { 'PASS' } else { 'FAIL' }))
$md += ("- 生成时间：{0}" -f (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
$md += ("- 总耗时：{0} 秒" -f $duration)
$md += ""
$md += "## 各环节"
$md += ""
$md += "| 环节 | 结果 | 阻断发布 | 耗时(s) |"
$md += "| --- | --- | --- | --- |"
foreach ($s in $stages) {
    $md += ("| {0} | {1} | {2} | {3} |" -f $s.name, $s.status,
        $(if ($s.blocking) { '是' } else { '否' }), $s.seconds)
}

if ($corpusResults) {
    $md += ""
    $md += "## 图片语料"
    $md += ""
    $md += "| 状态 | 数量 |"
    $md += "| --- | --- |"
    foreach ($k in $corpusResults.counts.PSObject.Properties) {
        $md += ("| {0} | {1} |" -f $k.Name, $k.Value)
    }
}

if ($perfRows.Count -gt 0) {
    $md += ""
    $md += "## 性能"
    $md += ""
    $md += "| 图片数 | 启动窗口(ms) | 首次可交互(ms) | 切换中位(ms) | 切换P95(ms) | 内存(MB) | 句柄 | GDI |"
    $md += "| --- | --- | --- | --- | --- | --- | --- | --- |"
    foreach ($r in $perfRows) {
        $md += ("| {0} | {1} | {2} | {3} | {4} | {5} → {6} | {7} → {8} | {9} → {10} |" -f `
            $r.images, $r.startupWindowMs, $r.interactiveMs, $r.switchMedianMs, $r.switchP95Ms,
            $r.workingSetBeforeMB, $r.workingSetAfterMB, $r.handlesBefore, $r.handlesAfter,
            $r.gdiBefore, $r.gdiAfter)
    }
}

if ($blockingFailures.Count -gt 0) {
    $md += ""
    $md += "## 发布阻断项"
    $md += ""
    foreach ($f in $blockingFailures) {
        $md += ("- **{0}**（退出码 {1}）——完整输出见 ``{2}``" -f $f.name, $f.exitCode, $f.log)
    }
}

$skipped = @($stages | Where-Object { $_.status -eq 'SKIPPED' })
if ($skipped.Count -gt 0) {
    $md += ""
    $md += "## 未执行的环节"
    $md += ""
    $md += "下列环节本次没跑，**不代表它们通过**："
    foreach ($s in $skipped) { $md += ("- {0}：{1}" -f $s.name, $s.detail) }
}

$summaryPath = Join-Path $OutputDir "summary.md"
($md -join "`n") | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host ""
Write-Host ("环节统计：PASS {0}  FAIL {1}  SKIPPED {2}  总耗时 {3}s" -f `
    @($stages | Where-Object { $_.status -eq 'PASS' }).Count,
    @($stages | Where-Object { $_.status -eq 'FAIL' }).Count,
    $skipped.Count, $duration)
Write-Host ("报告：{0}" -f $summaryPath)
Write-Host ("结果：{0}" -f $resultsPath)
if (Test-Path -LiteralPath $perfCsv) { Write-Host ("性能：{0}" -f $perfCsv) }
Write-Host ""

if ($gatePassed) {
    if ($skipped.Count -gt 0) {
        Write-Host ("注意：有 {0} 个环节未执行，未执行不等于通过。" -f $skipped.Count) -ForegroundColor Yellow
    }
    Write-Host "PASS 发布闸门通过" -ForegroundColor Green
    exit 0
}
Write-Host ("FAIL 发布闸门未通过：{0} 个阻断环节失败" -f $blockingFailures.Count) -ForegroundColor Red
exit 1
