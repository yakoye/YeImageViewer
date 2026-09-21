<#
.SYNOPSIS
    极端 PNG 测试集探针：逐张解码 + 快速连续切换 + 内存/句柄观察。

.DESCRIPTION
    素材由 test/YeImageViewer-Extreme-Testset-v2-tools/Build-YeImageViewer-Testset.ps1 生成，
    体积近 1 GB，不进仓库；本地没有就记 SKIPPED。测试集的设计意图见同目录的
    YeImageViewer-Extreme-Testset-v2.md，这里把其中能自动判定的部分固化下来：

      逐张解码   每张图跑一次 --decode-probe，按分组给不同的判据：
                 09_Broken_PNG        必须「解码失败」而不是崩溃或卡死——
                                      对损坏文件，正确行为是干净地拒绝；
                 03_Generated_200MP+  解出来或明确拒绝都算通过（单张展开可达 1 GiB，
                                      文档明确允许在申请巨量内存前拒绝），
                                      但崩溃和超时不行；
                 其余各组             必须解出非空图像。

      快速连切   打开 12_Mixed_Switch（真实大图、200MP、16 位、ICC、损坏图混排），
                 连按 → N 次再连按 ← N 次，每次间隔极短。判据是窗口标题里的序号
                 必须正好落在第 N+1 张、再回到第 1 张：
                 序号对不上就说明要么丢了按键，要么某个先发出的解码任务后完成、
                 把画面覆盖回了旧图——后者正是异步解码最容易出的问题。

      内存句柄   连切前后各记一次工作集、私有提交、GDI 和 USER 对象。
                 大图一张展开就是几百 MB，泄漏会非常明显。

.EXAMPLE
    ./probe-extreme-png.ps1
    ./probe-extreme-png.ps1 -SwitchCount 50 -DecodeTimeoutSeconds 90
#>
param(
    [string]$Viewer = (Join-Path $PSScriptRoot "..\..\x64\Release\YeImageViewer.exe"),
    [string]$Root = (Join-Path $PSScriptRoot "..\..\test\corpus\_local\07-extreme-png"),
    [int]$DecodeTimeoutSeconds = 90,
    [int]$SwitchCount = 30,
    [int]$SwitchIntervalMs = 40,
    [double]$MemoryGrowthLimitMb = 900,
    [string]$OutputCsv = (Join-Path $PSScriptRoot "..\..\artifacts\release-gate\extreme-png.csv")
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$EXIT_SKIPPED = 3

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class ExtremeProbe {
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint msg, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(
        IntPtr hWnd, uint msg, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(
        IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
    public static string Title(IntPtr hWnd) {
        var buffer = new StringBuilder(1024);
        GetWindowTextW(hWnd, buffer, buffer.Capacity);
        return buffer.ToString();
    }
}
"@

if (-not (Test-Path -LiteralPath $Viewer -PathType Leaf)) {
    Write-Output "SKIPPED 找不到 $Viewer"
    exit $EXIT_SKIPPED
}
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Output "SKIPPED 极端测试集未生成：$Root"
    Write-Output "        生成方式见 test/YeImageViewer-Extreme-Testset-v2-tools/"
    exit $EXIT_SKIPPED
}

# 分组判据。Policy 取值：
#   must-decode   必须解出非空图
#   must-refuse   必须干净地拒绝（解码失败），不能崩溃或卡死
#   may-refuse    解出来或拒绝都行，但不能崩溃或卡死
$groupPolicy = [ordered]@{
    "01_Real_PNG_50-70MB"    = "must-decode"
    "02_Real_100MP_Plus"     = "must-decode"
    # 这一组曾经在 240MP 处被整组拒绝：OpenCV 的 CV_IO_MAX_IMAGE_PIXELS 默认 2^30，
    # 动画/PNG 那条路还要乘「帧数 + 4」，静态 PNG 的实际上限只剩 2.15 亿像素。
    # 上限已在 scripts/build-opencv-slim.ps1 里放宽，所以这里按「必须解出来」卡死，
    # 不用 may-refuse——那样的话同类回归会被当成「优雅拒绝」放过去。
    "03_Generated_200MP_Plus" = "must-decode"
    "04_Alpha_RGBA"          = "must-decode"
    "05_16bit_PNG"           = "must-decode"
    "06_Adam7_Interlaced"    = "must-decode"
    "07_ICC_WideGamut"       = "must-decode"
    "08_UltraWide_Tall"      = "must-decode"
    "09_Broken_PNG"          = "must-refuse"
    "10_Metadata_PNG"        = "must-decode"
    "11_PngSuite_Mixed"      = "must-decode"
}

function Invoke-DecodeProbe {
    param([string]$ImagePath, [int]$Timeout)
    $resultFile = [IO.Path]::GetTempFileName()
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        $process = Start-Process -FilePath $Viewer `
            -ArgumentList @('--decode-probe', "`"$ImagePath`"", "`"$resultFile`"") `
            -PassThru -WindowStyle Hidden
        if (-not $process.WaitForExit($Timeout * 1000)) {
            try { Stop-Process -Id $process.Id -Force } catch { }
            [void]$process.WaitForExit(3000)
            return [pscustomobject]@{ Outcome = 'TIMEOUT'; Width = 0; Height = 0; Ms = $clock.Elapsed.TotalMilliseconds }
        }
        $clock.Stop()
        $fields = @()
        $line = Get-Content -LiteralPath $resultFile -Raw -ErrorAction SilentlyContinue
        if ($line) { $fields = $line.Trim() -split "`t" }
        $outcome = switch ($process.ExitCode) {
            0 { 'DECODED' }
            2 { 'DECODE_FAILED' }
            3 { 'PROBE_IO_ERROR' }
            default { 'CRASH' }
        }
        return [pscustomobject]@{
            Outcome = $outcome
            Width   = if ($fields.Count -ge 2) { [int]$fields[1] } else { 0 }
            Height  = if ($fields.Count -ge 3) { [int]$fields[2] } else { 0 }
            Ms      = [math]::Round($clock.Elapsed.TotalMilliseconds, 1)
        }
    }
    finally { Remove-Item -LiteralPath $resultFile -Force -ErrorAction SilentlyContinue }
}

# ---- 第一段：逐张解码 --------------------------------------------------------
Write-Host "极端 PNG 测试集：$Root"
$rows = New-Object Collections.ArrayList
$failures = New-Object Collections.ArrayList

foreach ($group in $groupPolicy.Keys) {
    $directory = Join-Path $Root $group
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        Write-Host ("  {0,-24} 未生成，跳过" -f $group) -ForegroundColor DarkYellow
        continue
    }
    $files = Get-ChildItem -LiteralPath $directory -File | Sort-Object Name
    if ($files.Count -eq 0) {
        Write-Host ("  {0,-24} 目录为空，跳过" -f $group) -ForegroundColor DarkYellow
        continue
    }
    $policy = $groupPolicy[$group]
    $groupFail = 0
    $slowest = 0.0
    foreach ($file in $files) {
        $probe = Invoke-DecodeProbe -ImagePath $file.FullName -Timeout $DecodeTimeoutSeconds
        if ($probe.Ms -gt $slowest) { $slowest = $probe.Ms }
        $ok = switch ($policy) {
            "must-decode" { $probe.Outcome -eq 'DECODED' -and $probe.Width -gt 0 -and $probe.Height -gt 0 }
            "must-refuse" { $probe.Outcome -eq 'DECODE_FAILED' }
            "may-refuse"  { $probe.Outcome -in @('DECODED', 'DECODE_FAILED') }
            default       { $false }
        }
        if (-not $ok) {
            $groupFail++
            [void]$failures.Add(("{0}/{1}：{2}（{3}x{4}，{5} ms）" -f `
                $group, $file.Name, $probe.Outcome, $probe.Width, $probe.Height, $probe.Ms))
        }
        [void]$rows.Add([pscustomobject]@{
            分组 = $group; 文件 = $file.Name; 判据 = $policy
            结果 = $probe.Outcome; 宽 = $probe.Width; 高 = $probe.Height
            解码毫秒 = $probe.Ms; 通过 = $ok
        })
    }
    $status = if ($groupFail -eq 0) { "PASS" } else { "FAIL $groupFail" }
    Write-Host ("  {0,-24} {1,3} 张  {2,-8} 最慢 {3,8:N0} ms" -f $group, $files.Count, $status, $slowest)
}

# ---- 第二段：快速连续切换 ----------------------------------------------------
$mixedDirectory = Join-Path $Root "12_Mixed_Switch"
$switchNote = ""
$switchOk = $true
$memoryRow = $null
if (-not (Test-Path -LiteralPath $mixedDirectory -PathType Container)) {
    $switchNote = "12_Mixed_Switch 未生成，连切未测"
    Write-Host "  $switchNote" -ForegroundColor DarkYellow
}
else {
    $mixedFiles = Get-ChildItem -LiteralPath $mixedDirectory -File | Sort-Object Name
    if ($mixedFiles.Count -lt ($SwitchCount + 1)) {
        $SwitchCount = [Math]::Max(1, $mixedFiles.Count - 1)
    }
    Write-Host ""
    Write-Host ("快速连切：{0} 张混排素材，连按 → {1} 次再连按 ← {1} 次" -f $mixedFiles.Count, $SwitchCount)

    $process = Start-Process -FilePath $Viewer -ArgumentList ('"' + $mixedFiles[0].FullName + '"') -PassThru
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 150
            $process.Refresh()
        } while (-not $process.HasExited -and $process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
        if ($process.HasExited -or $process.MainWindowHandle -eq 0) {
            throw "窗口没有出现"
        }
        $window = [IntPtr]$process.MainWindowHandle
        Start-Sleep -Milliseconds 800

        $process.Refresh()
        $beforeWorkingSet = [math]::Round($process.WorkingSet64 / 1MB, 1)
        $beforePrivate = [math]::Round($process.PrivateMemorySize64 / 1MB, 1)
        $beforeGdi = [ExtremeProbe]::GetGuiResources($process.Handle, 0)
        $beforeUser = [ExtremeProbe]::GetGuiResources($process.Handle, 1)

        # 连按 →。间隔比解码快得多，正是为了制造「旧任务后完成」的场面。
        for ($index = 0; $index -lt $SwitchCount; $index++) {
            [void][ExtremeProbe]::SendMessage($window, 0x0100, [UIntPtr]0x27, [IntPtr]::Zero)
            [void][ExtremeProbe]::SendMessage($window, 0x0101, [UIntPtr]0x27, [IntPtr]::Zero)
            Start-Sleep -Milliseconds $SwitchIntervalMs
        }
        Start-Sleep -Seconds 4     # 等后台解码全部落地，旧结果要是会覆盖，就在这几秒里发生

        $title = [ExtremeProbe]::Title($window)
        $forwardIndex = if ($title -match '\[(\d+)/(\d+)\]') { [int]$Matches[1] } else { -1 }
        if ($forwardIndex -ne ($SwitchCount + 1)) {
            $switchOk = $false
            [void]$failures.Add("连按 → $SwitchCount 次后停在第 $forwardIndex 张，应为第 $($SwitchCount + 1) 张：$title")
        }

        for ($index = 0; $index -lt $SwitchCount; $index++) {
            [void][ExtremeProbe]::SendMessage($window, 0x0100, [UIntPtr]0x25, [IntPtr]::Zero)
            [void][ExtremeProbe]::SendMessage($window, 0x0101, [UIntPtr]0x25, [IntPtr]::Zero)
            Start-Sleep -Milliseconds $SwitchIntervalMs
        }
        Start-Sleep -Seconds 4

        $title = [ExtremeProbe]::Title($window)
        $backIndex = if ($title -match '\[(\d+)/(\d+)\]') { [int]$Matches[1] } else { -1 }
        if ($backIndex -ne 1) {
            $switchOk = $false
            [void]$failures.Add("再连按 ← $SwitchCount 次后停在第 $backIndex 张，应回到第 1 张：$title")
        }

        # 界面必须还活着：解码卡死时窗口还在，但消息泵已经不回了
        $result = [UIntPtr]::Zero
        $responded = [ExtremeProbe]::SendMessageTimeout($window, 0x0000, [UIntPtr]::Zero, [IntPtr]::Zero, 2, 3000, [ref]$result)
        if ($responded -eq [IntPtr]::Zero) {
            $switchOk = $false
            [void]$failures.Add("连切之后主线程不再响应消息")
        }

        $process.Refresh()
        $afterWorkingSet = [math]::Round($process.WorkingSet64 / 1MB, 1)
        $afterPrivate = [math]::Round($process.PrivateMemorySize64 / 1MB, 1)
        $afterGdi = [ExtremeProbe]::GetGuiResources($process.Handle, 0)
        $afterUser = [ExtremeProbe]::GetGuiResources($process.Handle, 1)
        $memoryRow = [pscustomobject]@{
            工作集MB = "$beforeWorkingSet → $afterWorkingSet"
            私有提交MB = "$beforePrivate → $afterPrivate"
            GDI = "$beforeGdi → $afterGdi"
            USER = "$beforeUser → $afterUser"
        }
        Write-Host ("  工作集 {0} → {1} MB   私有提交 {2} → {3} MB   GDI {4} → {5}   USER {6} → {7}" -f `
            $beforeWorkingSet, $afterWorkingSet, $beforePrivate, $afterPrivate, $beforeGdi, $afterGdi, $beforeUser, $afterUser)

        $growth = $afterPrivate - $beforePrivate
        if ($growth -gt $MemoryGrowthLimitMb) {
            $switchOk = $false
            [void]$failures.Add("连切 $($SwitchCount * 2) 次后私有提交增长 $growth MB，超过上限 $MemoryGrowthLimitMb MB")
        }
        if ($afterGdi -gt ($beforeGdi * 3 + 10) -or $afterUser -gt ($beforeUser * 3 + 10)) {
            $switchOk = $false
            [void]$failures.Add("连切后 GDI/USER 对象成倍增长：GDI $beforeGdi→$afterGdi，USER $beforeUser→$afterUser")
        }
    }
    catch {
        $switchOk = $false
        [void]$failures.Add("连切阶段异常：$($_.Exception.Message)")
    }
    finally {
        if ($process -and -not $process.HasExited) {
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(5000)) { Stop-Process -Id $process.Id -Force }
        }
    }
}

# ---- 汇总 --------------------------------------------------------------------
if ($rows.Count -gt 0 -and $OutputCsv) {
    $directory = Split-Path -Parent $OutputCsv
    if ($directory -and -not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $rows | Export-Csv -LiteralPath $OutputCsv -NoTypeInformation -Encoding UTF8
}

$decodedCount = ($rows | Where-Object { $_.通过 }).Count
Write-Host ""
Write-Host ("解码 {0}/{1} 项符合判据；连切 {2}" -f $decodedCount, $rows.Count,
    $(if ($switchNote) { "未测" } elseif ($switchOk) { "通过" } else { "不通过" }))

if ($rows.Count -eq 0 -and $switchNote) {
    Write-Output "SKIPPED 测试集素材不完整，没有可执行的用例"
    exit $EXIT_SKIPPED
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "失败项：" -ForegroundColor Red
    $failures | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" }
    if ($failures.Count -gt 20) { Write-Host "  （共 $($failures.Count) 项）" }
    Write-Output "FAIL 极端测试集有 $($failures.Count) 项不符合判据"
    exit 1
}

Write-Output "PASS 极端测试集逐张解码与快速连切均符合判据"
exit 0
