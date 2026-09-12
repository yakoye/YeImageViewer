<#
.SYNOPSIS
    YeImageViewer 图片语料测试执行器。

.DESCRIPTION
    读取 test/corpus/manifest.json，按 suite 逐条执行并生成 Markdown + JSON 报告。

    两种探测方式各有分工，互相补不了位：
      decode  用 --decode-probe 走正式解码分派，验证解码结果（尺寸、成败）是否符合预期，
              不创建窗口，快。
      gui     真正启动窗口打开文件，验证不崩溃、不卡死、界面仍然响应。损坏素材必须走
              这一路——解码失败是允许的，界面冻死不是。

    状态取值：PASS / FAIL / KNOWN_UNSUPPORTED / KNOWN_LIMITATION / SKIPPED / ERROR。
    框架自身出问题记 ERROR，绝不记 PASS；存在 release-blocker 的 FAIL 时退出码非 0，
    以便 CI 阻断发布。

.EXAMPLE
    .\run-tests.ps1 -Suite core
    .\run-tests.ps1 -Suite corrupt
    .\run-tests.ps1 -All
#>
param(
    [string]$Suite,
    [switch]$All,
    [string]$Viewer,
    [string]$OutputDir,
    [ValidateSet('auto', 'decode', 'gui', 'both')]
    [string]$Mode = 'auto',
    [int]$TimeoutSeconds = 0,
    # 跳过素材哈希校验。只在反复调试、确信素材没动过时用；常规运行不要加，
    # 否则损坏的素材会被当成真样本，把夹具问题记成解码缺陷。
    [switch]$SkipHashCheck
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$corpusRoot = Join-Path $repoRoot "test\corpus"
$manifestPath = Join-Path $corpusRoot "manifest.json"

if (-not $Viewer) { $Viewer = Join-Path $repoRoot "x64\Release\YeImageViewer.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot "artifacts\test-report" }
if (-not (Test-Path -LiteralPath $Viewer)) { throw "找不到 YeImageViewer.exe：$Viewer" }
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "找不到 manifest：$manifestPath，请先运行 scripts/build-corpus-manifest.ps1。"
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class RunnerNative {
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint msg, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
}
"@

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json

$selectedCategories = @()
if ($All) {
    $selectedCategories = $manifest.cases | ForEach-Object { $_.category } | Sort-Object -Unique
}
elseif ($Suite) {
    if (-not $manifest.suites.PSObject.Properties[$Suite]) {
        throw "manifest 中没有名为 $Suite 的 suite。可选：$(($manifest.suites.PSObject.Properties.Name) -join ', ')"
    }
    $selectedCategories = @($manifest.suites.$Suite)
}
else {
    throw "请指定 -Suite <名称> 或 -All。"
}

$cases = @($manifest.cases | Where-Object { $selectedCategories -contains $_.category })
if ($cases.Count -eq 0) { throw "选中的 suite 没有任何用例。" }

# 超时按类别区分：大图和 RAW 本来就慢，用同一个阈值会制造假的 HANG。
function Get-CaseTimeout {
    param($Case)
    if ($TimeoutSeconds -gt 0) { return $TimeoutSeconds }
    switch ($Case.category) {
        'large-image' { return 60 }
        'raw'         { return 30 }
        default       { return 10 }
    }
}

function Get-CaseMode {
    param($Case)
    if ($Mode -ne 'auto') { return $Mode }
    # 损坏素材的重点是界面不能死，必须真的开窗口；其余先看解码结果。
    if ($Case.category -eq 'corrupt') { return 'both' }
    return 'decode'
}

# --decode-probe 走正式解码分派并把结果写进临时文件，不创建窗口。
function Invoke-DecodeProbe {
    param([string]$ImagePath, [int]$Timeout)

    $resultFile = [IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $Viewer `
            -ArgumentList @('--decode-probe', "`"$ImagePath`"", "`"$resultFile`"") `
            -PassThru -WindowStyle Hidden
        if (-not $process.WaitForExit($Timeout * 1000)) {
            try { Stop-Process -Id $process.Id -Force } catch {}
            [void]$process.WaitForExit(3000)
            return @{ Outcome = 'TIMEOUT'; ExitCode = $null }
        }
        $exitCode = $process.ExitCode

        $fields = @()
        if (Test-Path -LiteralPath $resultFile) {
            $line = (Get-Content -LiteralPath $resultFile -Raw -ErrorAction SilentlyContinue)
            if ($line) { $fields = $line.Trim() -split "`t" }
        }

        # 退出码约定：0 成功、2 解码失败、3 结果文件写不出；其余视为异常终止。
        $outcome = switch ($exitCode) {
            0 { 'DECODED' }
            2 { 'DECODE_FAILED' }
            3 { 'PROBE_IO_ERROR' }
            default { 'CRASH' }
        }
        return @{
            Outcome  = $outcome
            ExitCode = $exitCode
            Width    = if ($fields.Count -ge 2) { [int]$fields[1] } else { 0 }
            Height   = if ($fields.Count -ge 3) { [int]$fields[2] } else { 0 }
            Frames   = if ($fields.Count -ge 4) { [int]$fields[3] } else { 0 }
            Kind     = if ($fields.Count -ge 5) { $fields[4] } else { '' }
            # 通道数与最小 alpha 是后来追加的字段，旧程序不输出，记 $null 以示未知，
            # 不能当成 0——0 通道会被误判成解码出了空图。
            Channels = if ($fields.Count -ge 6) { [int]$fields[5] } else { $null }
            MinAlpha = if ($fields.Count -ge 7) { [int]$fields[6] } else { $null }
        }
    }
    finally {
        Remove-Item -LiteralPath $resultFile -Force -ErrorAction SilentlyContinue
    }
}

# 真正开窗口：验证进程活着、窗口存在、且能在超时内回应消息（界面没冻死）。
function Invoke-GuiProbe {
    param([string]$ImagePath, [int]$Timeout)

    $process = $null
    try {
        $process = Start-Process -FilePath $Viewer -ArgumentList "`"$ImagePath`"" -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
        do {
            Start-Sleep -Milliseconds 200
            $process.Refresh()
            if ($process.HasExited) { break }
        } while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)

        if ($process.HasExited) {
            # 打开一张图不该让进程自己退出，非 0 退出码更是明确的崩溃信号。
            return @{ Outcome = if ($process.ExitCode -eq 0) { 'EXITED' } else { 'CRASH' }
                      ExitCode = $process.ExitCode; Responsive = $false }
        }
        if ($process.MainWindowHandle -eq 0) {
            return @{ Outcome = 'NO_WINDOW'; ExitCode = $null; Responsive = $false }
        }

        # WM_NULL 能在限定时间内返回，说明消息泵还在转，界面没有永久冻结。
        $result = [UIntPtr]::Zero
        $answered = [RunnerNative]::SendMessageTimeout(
            [IntPtr]$process.MainWindowHandle, 0x0000, [UIntPtr]::Zero, [IntPtr]::Zero,
            0x0002, [uint32]($Timeout * 1000), [ref]$result)
        $responsive = $answered -ne [IntPtr]::Zero
        $process.Refresh()
        return @{
            Outcome    = if ($responsive) { 'RESPONSIVE' } else { 'UI_FREEZE' }
            ExitCode   = $null
            Responsive = $responsive
        }
    }
    finally {
        # 无论结果如何都不留残留进程。
        if ($process -and -not $process.HasExited) {
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(3000)) {
                try { Stop-Process -Id $process.Id -Force } catch {}
                [void]$process.WaitForExit(2000)
            }
        }
    }
}

$results = @()
$startedAt = Get-Date
Write-Host "语料测试开始：$($cases.Count) 个用例，suite = $(if ($All) { 'all' } else { $Suite })"
Write-Host ""

foreach ($case in $cases) {
    $fullPath = Join-Path $corpusRoot ($case.path -replace '/', '\')
    $record = [ordered]@{
        id = $case.id; path = $case.path; category = $case.category
        severity = $case.severity; status = 'ERROR'; detail = ''
    }

    if (-not (Test-Path -LiteralPath $fullPath)) {
        # 素材不在本地（例如体积过大未纳入仓库）不是失败，但必须如实记为跳过。
        $record.status = 'SKIPPED'
        $record.detail = '素材不存在于本地'
        $results += $record
        Write-Host ("  {0,-12} {1}" -f 'SKIPPED', $case.id)
        continue
    }

    # 素材必须和登记的哈希一致才开测。
    # 不校验的话，损坏或被替换的素材会被当成真样本，失败还会被归咎到解码器上——
    # git 外的 RAW 素材尤其如此，下载截断了看不出来。
    # 这类问题记 ERROR 而不是 FAIL：它是夹具问题，不是被测程序的问题。
    if (-not $SkipHashCheck -and $case.PSObject.Properties['sha256'] -and $case.sha256) {
        $actualHash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
        if ($actualHash -ne ([string]$case.sha256).ToUpperInvariant()) {
            $record.status = 'ERROR'
            $record.detail = "素材与登记的哈希不符（期望 $($case.sha256)，实际 $actualHash）：素材被改动或下载不完整，先修素材再谈解码结果"
            $results += $record
            Write-Host ("  {0,-12} {1}" -f 'ERROR', $case.id)
            continue
        }
    }

    $timeout = Get-CaseTimeout $case
    $caseMode = Get-CaseMode $case
    $failures = @()
    $details = @()
    # 已登记的「本机解码器不支持」标记。解不出来记 KNOWN_UNSUPPORTED；
    # 反过来若竟然解出来了，说明标记过期，必须让它显性失败——
    # 过期的「不支持」标记会长期掩盖真实回归。
    $knownUnsupported = $case.expected.PSObject.Properties['knownUnsupported'] -and
        $case.expected.knownUnsupported
    $unsupportedHit = $false

    try {
        if ($caseMode -in @('decode', 'both')) {
            $probe = Invoke-DecodeProbe -ImagePath $fullPath -Timeout $timeout
            $details += "decode=$($probe.Outcome)"

            switch ($probe.Outcome) {
                'CRASH'         { $failures += "解码探测异常退出（退出码 $($probe.ExitCode)）" }
                'TIMEOUT'       { $failures += "解码探测超过 ${timeout}s 未结束" }
                'PROBE_IO_ERROR'{ $failures += '探测无法写出结果文件' }
                'DECODE_FAILED' {
                    $allowFailure = $case.expected.PSObject.Properties['allowDecodeFailure'] -and
                        $case.expected.allowDecodeFailure
                    # 已登记为「本机解码器不支持」的格式：解不出来是已知缺口，如实记
                    # KNOWN_UNSUPPORTED，既不算通过也不算失败。规格明令不许把它当 PASS。
                    if ($knownUnsupported) { $unsupportedHit = $true }
                    elseif (-not $allowFailure) { $failures += '解码失败，但该素材应当能够解码' }
                }
                'DECODED' {
                    $details += "$($probe.Width)x$($probe.Height)"
                    if ($knownUnsupported) {
                        $failures += ('该格式在 manifest 里标记为不支持，但现在解码成功了：' +
                            '请去掉该标记（这是登记过期，不是程序问题）')
                    }
                    foreach ($dimension in @('width', 'height')) {
                        if (-not $case.expected.PSObject.Properties[$dimension]) { continue }
                        $expectedValue = [int]$case.expected.$dimension
                        $actualValue = if ($dimension -eq 'width') { $probe.Width } else { $probe.Height }
                        if ($expectedValue -ne $actualValue) {
                            $failures += "$dimension 期望 $expectedValue，实际 $actualValue"
                        }
                    }

                    # 透明通道：素材本身有非不透明像素时，解码结果必须留住它。
                    # 只比尺寸的话，解码器丢掉 alpha 或把整层填成 255 都能蒙过去。
                    if ($case.expected.PSObject.Properties['channels']) {
                        if ($null -eq $probe.Channels) {
                            $failures += '程序未输出通道数：请用当前版本重新构建 YeImageViewer.exe'
                        }
                        elseif ([int]$case.expected.channels -ne $probe.Channels) {
                            $failures += "通道数期望 $([int]$case.expected.channels)，实际 $($probe.Channels)"
                        }
                    }
                    # 多页 / 动画：帧数不足说明只解出了第一页
                    if ($case.expected.PSObject.Properties['minimumFrames']) {
                        $wanted = [int]$case.expected.minimumFrames
                        if ($probe.Frames -lt $wanted) {
                            $failures += "帧数期望至少 $wanted，实际 $($probe.Frames)"
                        }
                        else { $details += "frames=$($probe.Frames)" }
                    }

                    # 不透明的素材必须解出不透明。没有这一条就抓不到
                    # 「整层 alpha 被归零、图变成全透明」——图在界面上完全看不见。
                    if ($case.expected.PSObject.Properties['minMinAlpha']) {
                        $floor = [int]$case.expected.minMinAlpha
                        if ($null -eq $probe.MinAlpha) {
                            $failures += '程序未输出 alpha 信息：请用当前版本重新构建 YeImageViewer.exe'
                        }
                        elseif ($probe.MinAlpha -lt $floor) {
                            $failures += ("素材不透明，解码后最小 alpha 却是 $($probe.MinAlpha)" +
                                "（下限 $floor）：透明通道被错误归零，图会显示为全透明")
                        }
                    }

                    if ($case.expected.PSObject.Properties['maxMinAlpha']) {
                        $bound = [int]$case.expected.maxMinAlpha
                        if ($null -eq $probe.MinAlpha) {
                            $failures += '程序未输出 alpha 信息：请用当前版本重新构建 YeImageViewer.exe'
                        }
                        elseif ($probe.MinAlpha -gt $bound) {
                            $failures += ("素材最小 alpha 为 $($case.expected.sourceMinAlpha)，" +
                                "解码后却是 $($probe.MinAlpha)（上限 $bound）：透明通道被丢弃或填平")
                        }
                        else {
                            $details += "minAlpha=$($probe.MinAlpha)"
                        }
                    }
                }
            }
        }

        if ($caseMode -in @('gui', 'both')) {
            $gui = Invoke-GuiProbe -ImagePath $fullPath -Timeout $timeout
            $details += "gui=$($gui.Outcome)"
            switch ($gui.Outcome) {
                'CRASH'     { $failures += "打开后进程异常退出（退出码 $($gui.ExitCode)）" }
                'EXITED'    { $failures += '打开后进程自行退出' }
                'NO_WINDOW' { $failures += "${timeout}s 内没有出现窗口" }
                'UI_FREEZE' { $failures += '窗口在超时内未响应消息' }
            }
        }

        $record.status = if ($failures.Count -gt 0) { 'FAIL' }
            elseif ($unsupportedHit) { 'KNOWN_UNSUPPORTED' }
            else { 'PASS' }
        if ($unsupportedHit -and $case.PSObject.Properties['comment']) {
            $details += $case.comment
        }
        $record.detail = (($details + $failures) -join '; ')
    }
    catch {
        # 框架自身出错单独记 ERROR，绝不能混进 PASS。
        $record.status = 'ERROR'
        $record.detail = "测试框架异常：$($_.Exception.Message)"
    }

    $results += $record
    $marker = switch ($record.status) { 'PASS' { 'PASS' } 'FAIL' { 'FAIL' } default { $record.status } }
    Write-Host ("  {0,-12} {1}  {2}" -f $marker, $case.id, $record.detail)
}

$duration = [Math]::Round(((Get-Date) - $startedAt).TotalSeconds, 1)
if (-not (Test-Path -LiteralPath $OutputDir)) { [void](New-Item -ItemType Directory -Path $OutputDir -Force) }

$counts = @{}
foreach ($status in @('PASS', 'FAIL', 'KNOWN_UNSUPPORTED', 'KNOWN_LIMITATION', 'SKIPPED', 'ERROR')) {
    $counts[$status] = @($results | Where-Object { $_.status -eq $status }).Count
}
$blockerFailures = @($results | Where-Object {
    $_.severity -eq 'release-blocker' -and $_.status -in @('FAIL', 'ERROR') })

$resultsPath = Join-Path $OutputDir "results.json"
([ordered]@{
    generatedAt = (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
    suite = if ($All) { 'all' } else { $Suite }
    durationSeconds = $duration
    counts = $counts
    releaseBlockerFailures = $blockerFailures.Count
    results = $results
} | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $resultsPath -Encoding UTF8

$summary = @()
$summary += "# YeImageViewer 语料测试报告"
$summary += ""
$summary += "- 生成时间：$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
$summary += "- Suite：$(if ($All) { 'all' } else { $Suite })"
$summary += "- 用例数：$($results.Count)，耗时 ${duration}s"
$summary += ""
$summary += "## 结果统计"
$summary += ""
$summary += "| 状态 | 数量 |"
$summary += "| --- | --- |"
foreach ($status in @('PASS', 'FAIL', 'KNOWN_UNSUPPORTED', 'KNOWN_LIMITATION', 'SKIPPED', 'ERROR')) {
    $summary += "| $status | $($counts[$status]) |"
}
$summary += ""
$summary += "## 发布阻断项"
$summary += ""
$summary += "release-blocker 失败数：$($blockerFailures.Count)"
if ($blockerFailures.Count -gt 0) {
    $summary += ""
    foreach ($failure in $blockerFailures) {
        $summary += "- ``$($failure.id)``（$($failure.category)）：$($failure.detail)"
    }
}
$failed = @($results | Where-Object { $_.status -in @('FAIL', 'ERROR') })
if ($failed.Count -gt 0) {
    $summary += ""
    $summary += "## 全部失败明细"
    $summary += ""
    foreach ($failure in $failed) {
        $summary += "- ``$($failure.id)``：$($failure.status) — $($failure.detail)"
    }
}
$summaryPath = Join-Path $OutputDir "summary.md"
($summary -join "`n") | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host ""
Write-Host "PASS $($counts['PASS'])  FAIL $($counts['FAIL'])  SKIPPED $($counts['SKIPPED'])  ERROR $($counts['ERROR'])"
Write-Host "报告：$summaryPath"

$exitCode = 0
if ($blockerFailures.Count -gt 0) {
    Write-Host "存在 $($blockerFailures.Count) 个发布阻断失败。" -ForegroundColor Red
    $exitCode = 1
}
# ERROR 也必须让退出码非 0。ERROR 的含义是「这条用例没测出结果」，
# 不是「测过了没问题」——素材损坏、探测写不出文件、框架自身异常都归在这里。
# 放它过去，CI 就会带着一批根本没测到的用例放行。
if ($counts['ERROR'] -gt 0) {
    Write-Host "存在 $($counts['ERROR']) 个 ERROR：这些用例没有得出结果，不能视为通过。" -ForegroundColor Red
    $exitCode = 1
}
exit $exitCode
