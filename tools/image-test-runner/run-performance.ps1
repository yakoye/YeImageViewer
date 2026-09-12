<#
.SYNOPSIS
    大量图片下的性能与资源压测（测试规格 Phase 6）。

.DESCRIPTION
    在 100 / 1000 / 10000 张图片的目录下测量启动、首次可交互、切换延迟和资源占用，
    结果写成 performance.csv 供逐版本对比。

    素材放在 test/corpus/_local/06-performance/（已被 .gitignore 挡住），
    10000 张即使每张只有几 KB 也不该进仓库。首次运行会生成，之后复用。

    度量口径（都不依赖读屏幕，锁屏也能跑）：
      启动时间    进程启动 → 顶层窗口出现
      首次可交互  进程启动 → 窗口能在限定时间内回应 WM_NULL，说明 DrawScene 已跑起来
      扫描时间    同一批素材下，窗口就绪耗时随图片数增长的部分（打开时要扫目录建列表）
      切换延迟    连发翻页键，每次紧跟一个 SendMessageTimeout 探主线程被堵多久
      资源        WorkingSet、私有内存、句柄数、GDI 对象数；切换前后各取一次查泄漏

    注意：测量期间不要操作机器。CPU 与内存会被其他前台程序干扰，
    要比较版本间差异就得在空闲机器上跑。

.EXAMPLE
    .\run-performance.ps1 -Count 100
    .\run-performance.ps1 -Count 100,1000,10000
    .\run-performance.ps1 -Count 1000 -Switches 300
#>
param(
    [int[]]$Count = @(100, 1000, 10000),
    [int]$Switches = 200,
    [string]$Viewer,
    [string]$OutputDir,
    [switch]$Regenerate
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Viewer) { $Viewer = Join-Path $repoRoot "x64\Release\YeImageViewer.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot "artifacts\test-report" }
if (-not (Test-Path -LiteralPath $Viewer)) { throw "找不到 YeImageViewer.exe：$Viewer" }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$fixtureRoot = Join-Path $repoRoot "test\corpus\_local\06-performance"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class PerfNative {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(
        IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint timeout, out UIntPtr res);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);

    // 按进程枚举顶层窗口，不看前台窗口：锁屏时前台属于 LogonUI，
    // 按前台判定会取不到主窗口，得出与被测行为无关的结论。
    public static IntPtr FindMainWindow(uint targetPid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint pid = 0;
            GetWindowThreadProcessId(h, out pid);
            if (pid != targetPid || !IsWindowVisible(h)) return true;
            RECT r;
            if (!GetWindowRect(h, out r)) return true;
            if (r.R - r.L < 300 || r.B - r.T < 200) return true;
            found = h;
            return false;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$WM_KEYDOWN = 0x0100; $WM_KEYUP = 0x0101; $WM_NULL = 0x0000
$VK_RIGHT = 0x27; $SMTO_ABORTIFHUNG = 0x0002
$GR_GDIOBJECTS = 0; $GR_USEROBJECTS = 1

function New-PerfFixture {
    param([int]$Number)
    $dir = Join-Path $fixtureRoot ("{0:D5}" -f $Number)
    $existing = if (Test-Path -LiteralPath $dir) { @(Get-ChildItem -LiteralPath $dir -File).Count } else { 0 }
    if ($existing -eq $Number -and -not $Regenerate) { return $dir }

    if (-not (Get-Command magick -ErrorAction SilentlyContinue)) {
        throw "需要 ImageMagick 生成压测素材：未找到 magick 命令。"
    }
    Write-Host ("  生成 {0} 张素材（已有 {1} 张）..." -f $Number, $existing)
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    # 先用 magick 生成一小批不同色调的基图，再复制成 N 个文件名。
    #
    # 不逐张调 magick：10000 次进程启动光开销就要十几分钟。
    # 也不把 10000 张塞进一次调用：参数会超过 Windows 32KB 的命令行上限，
    # magick 直接报「文件名或扩展名太长」。
    # 相邻文件色调不同，肉眼可辨切换确实发生了；解码缓存按路径索引，
    # 内容相同也会逐个文件解码，不影响切换开销的测量。
    # 基图放在各档位之外：放进档位目录会被程序当成待浏览的图片一起扫进列表，
    # 图片数就跟档位名对不上了。放外面还能让各档位共用，不必重复生成。
    $baseCount = [Math]::Min(32, $Number)
    $baseDir = Join-Path $fixtureRoot "_base"
    New-Item -ItemType Directory -Force -Path $baseDir | Out-Null
    $bases = @()
    for ($b = 0; $b -lt $baseCount; $b++) {
        $basePath = Join-Path $baseDir ("base_{0:D2}.jpg" -f $b)
        if (-not (Test-Path -LiteralPath $basePath)) {
            $hue = [int](360 * $b / $baseCount)
            & magick -size 320x240 "gradient:hsl($hue,70%,55%)-hsl($((($hue + 90) % 360)),70%,25%)" `
                -quality 82 $basePath 2>&1 | Out-Null
        }
        $bases += $basePath
    }

    for ($i = 1; $i -le $Number; $i++) {
        $target = Join-Path $dir ("img_{0:D5}.jpg" -f $i)
        if (-not (Test-Path -LiteralPath $target)) {
            Copy-Item -LiteralPath $bases[($i - 1) % $baseCount] -Destination $target
        }
        if ($i % 500 -eq 0 -or $i -eq $Number) {
            Write-Progress -Activity "生成压测素材" -Status ("{0}/{1}" -f $i, $Number) `
                -PercentComplete ([int](100 * $i / $Number))
        }
    }
    Write-Progress -Activity "生成压测素材" -Completed
    return $dir
}

function Get-ResourceSnapshot {
    param($Process)
    $Process.Refresh()
    return [ordered]@{
        workingSetMB = [math]::Round($Process.WorkingSet64 / 1MB, 1)
        privateMB    = [math]::Round($Process.PrivateMemorySize64 / 1MB, 1)
        handles      = $Process.HandleCount
        gdiObjects   = [int][PerfNative]::GetGuiResources($Process.Handle, $GR_GDIOBJECTS)
        userObjects  = [int][PerfNative]::GetGuiResources($Process.Handle, $GR_USEROBJECTS)
        cpuSeconds   = [math]::Round($Process.TotalProcessorTime.TotalSeconds, 2)
    }
}

function Measure-Scenario {
    param([int]$Number, [string]$Dir)

    $files = @(Get-ChildItem -LiteralPath $Dir -File -Filter *.jpg | Sort-Object Name)
    if ($files.Count -lt 1) { throw "$Dir 里没有素材" }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $Viewer -ArgumentList "`"$($files[0].FullName)`"" -PassThru
    try {
        # 启动时间：窗口出现
        $hwnd = [IntPtr]::Zero
        while ($sw.Elapsed.TotalSeconds -lt 120 -and $hwnd -eq [IntPtr]::Zero) {
            $found = [PerfNative]::FindMainWindow([uint32]$proc.Id)
            if ($found -ne [IntPtr]::Zero) { $hwnd = $found } else { Start-Sleep -Milliseconds 10 }
        }
        if ($hwnd -eq [IntPtr]::Zero) { throw "120 秒内没有出现窗口（$Number 张）" }
        $windowMs = $sw.Elapsed.TotalMilliseconds

        # 首次可交互：窗口能回应消息，说明消息泵和 DrawScene 都转起来了
        $interactiveMs = $null
        while ($sw.Elapsed.TotalSeconds -lt 180) {
            [UIntPtr]$r = [UIntPtr]::Zero
            if ([PerfNative]::SendMessageTimeout($hwnd, $WM_NULL, [IntPtr]0, [IntPtr]0,
                    $SMTO_ABORTIFHUNG, 500, [ref]$r) -ne [IntPtr]::Zero) {
                $interactiveMs = $sw.Elapsed.TotalMilliseconds
                break
            }
            Start-Sleep -Milliseconds 20
        }

        Start-Sleep -Seconds 2   # 让预读线程安顿下来再取基线
        $before = Get-ResourceSnapshot $proc

        # 切换延迟：连发翻页键，每次紧跟一次主线程探测
        $latencies = @()
        $hangs = 0
        $switchSw = [System.Diagnostics.Stopwatch]::StartNew()
        for ($i = 1; $i -le $Switches; $i++) {
            [void][PerfNative]::PostMessage($hwnd, $WM_KEYDOWN, [IntPtr]$VK_RIGHT, [IntPtr]0)
            [void][PerfNative]::PostMessage($hwnd, $WM_KEYUP, [IntPtr]$VK_RIGHT, [IntPtr]0)
            $t = [System.Diagnostics.Stopwatch]::StartNew()
            [UIntPtr]$res = [UIntPtr]::Zero
            $answered = [PerfNative]::SendMessageTimeout($hwnd, $WM_NULL, [IntPtr]0, [IntPtr]0,
                $SMTO_ABORTIFHUNG, 5000, [ref]$res)
            $t.Stop()
            if ($answered -eq [IntPtr]::Zero) { $hangs++ }
            $latencies += $t.Elapsed.TotalMilliseconds
            Start-Sleep -Milliseconds 20
        }
        $switchSw.Stop()

        Start-Sleep -Seconds 2
        $after = Get-ResourceSnapshot $proc

        $sorted = @($latencies | Sort-Object)
        return [ordered]@{
            images            = $Number
            startupWindowMs   = [math]::Round($windowMs, 1)
            interactiveMs     = if ($interactiveMs) { [math]::Round($interactiveMs, 1) } else { $null }
            switches          = $Switches
            switchTotalMs     = [math]::Round($switchSw.Elapsed.TotalMilliseconds, 1)
            switchMedianMs    = [math]::Round($sorted[[int]($sorted.Count / 2)], 1)
            switchP95Ms       = [math]::Round($sorted[[int]([Math]::Floor($sorted.Count * 0.95))], 1)
            switchMaxMs       = [math]::Round(($latencies | Measure-Object -Maximum).Maximum, 1)
            switchHangs       = $hangs
            workingSetBeforeMB = $before.workingSetMB
            workingSetAfterMB  = $after.workingSetMB
            privateBeforeMB   = $before.privateMB
            privateAfterMB    = $after.privateMB
            handlesBefore     = $before.handles
            handlesAfter      = $after.handles
            gdiBefore         = $before.gdiObjects
            gdiAfter          = $after.gdiObjects
            userBefore        = $before.userObjects
            userAfter         = $after.userObjects
            cpuSeconds        = $after.cpuSeconds
        }
    }
    finally {
        # 超时或异常都不许留残留进程
        if ($proc -and -not $proc.HasExited) {
            try { $proc.Kill(); [void]$proc.WaitForExit(5000) } catch {}
        }
    }
}

Write-Host "性能压测开始"
Write-Host ("被测程序：{0}" -f $Viewer)
Write-Host ("素材目录：{0}" -f $fixtureRoot)
Write-Host ""

$rows = @()
foreach ($n in ($Count | Sort-Object)) {
    Write-Host ("=== {0} 张 ===" -f $n)
    $dir = New-PerfFixture -Number $n
    $row = Measure-Scenario -Number $n -Dir $dir
    $rows += [pscustomobject]$row
    Write-Host ("  启动窗口 {0} ms   首次可交互 {1} ms" -f $row.startupWindowMs, $row.interactiveMs)
    Write-Host ("  切换 {0} 次：中位 {1} ms  P95 {2} ms  最大 {3} ms  无响应 {4}" -f `
        $row.switches, $row.switchMedianMs, $row.switchP95Ms, $row.switchMaxMs, $row.switchHangs)
    Write-Host ("  内存 {0} → {1} MB   句柄 {2} → {3}   GDI {4} → {5}   CPU {6} s" -f `
        $row.workingSetBeforeMB, $row.workingSetAfterMB, $row.handlesBefore, $row.handlesAfter,
        $row.gdiBefore, $row.gdiAfter, $row.cpuSeconds)
    Write-Host ""
}

# 扫描时间：打开图片时要扫目录建列表，图片越多越慢。
# 单看某一档的启动时间分不出「扫描」和「固定开销」，用相邻档位的增量来估。
if ($rows.Count -ge 2) {
    Write-Host "=== 目录扫描开销（按启动时间随图片数的增量估算）==="
    for ($i = 1; $i -lt $rows.Count; $i++) {
        $a = $rows[$i - 1]; $b = $rows[$i]
        $deltaImages = $b.images - $a.images
        $deltaMs = $b.startupWindowMs - $a.startupWindowMs
        Write-Host ("  {0} → {1} 张：启动多花 {2:N1} ms，折合每千张 {3:N1} ms" -f `
            $a.images, $b.images, $deltaMs, ($deltaMs * 1000 / $deltaImages))
    }
    Write-Host ""
}

$csvPath = Join-Path $OutputDir "performance.csv"
$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
Write-Host ("已写入 {0}" -f $csvPath)

# 判定：这里只卡「明显坏掉」的情形，具体阈值随机器不同，逐版本对比 CSV 更有意义。
$failed = $false
foreach ($row in $rows) {
    if ($row.switchHangs -gt 0) {
        Write-Host ("FAIL {0} 张时翻页出现 {1} 次主线程无响应" -f $row.images, $row.switchHangs) -ForegroundColor Red
        $failed = $true
    }
    # 句柄和 GDI 在切换前后翻倍以上，基本只能是泄漏
    if ($row.handlesBefore -gt 0 -and $row.handlesAfter -gt $row.handlesBefore * 2) {
        Write-Host ("FAIL {0} 张时句柄从 {1} 涨到 {2}，疑似泄漏" -f `
            $row.images, $row.handlesBefore, $row.handlesAfter) -ForegroundColor Red
        $failed = $true
    }
    if ($row.gdiBefore -gt 0 -and $row.gdiAfter -gt $row.gdiBefore * 2) {
        Write-Host ("FAIL {0} 张时 GDI 对象从 {1} 涨到 {2}，疑似泄漏" -f `
            $row.images, $row.gdiBefore, $row.gdiAfter) -ForegroundColor Red
        $failed = $true
    }
}
if ($failed) { exit 1 }
Write-Host "PASS 各档位均无主线程无响应、无句柄/GDI 倍增"
exit 0
