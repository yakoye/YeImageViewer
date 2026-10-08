<#
.SYNOPSIS
反向验证 check-source-invariants.ps1：逐条制造它该抓的错误，确认它真会报错。

.DESCRIPTION
静态检查最危险的失效方式不是误报，而是**什么都抓不到**——正则写歪一个字符、
源码换了写法让匹配落空，检查照样打印 PASS，于是看起来有护栏，其实没有。
所以每加一条检查，这里就加一条配套的「破坏」：临时改坏一处源码，确认检查
以预期的那句话失败，然后立刻把文件按原字节还原。

还原用的是改之前的字节，不是 git checkout：工作区里常有还没提交的改动，
`git checkout --` 会连那些一起抹掉（已经踩过一次）。
中途被 Ctrl+C 打断也会还原——finally 兜着。

    ./scripts/verify-source-invariant-checks.ps1
#>
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$checker = Join-Path $PSScriptRoot "check-source-invariants.ps1"
if (-not (Test-Path -LiteralPath $checker -PathType Leaf)) {
    throw "The checker under test was not found at ${checker}."
}

# 仓库里存的是 CRLF；带换行的破坏点统一按 LF 匹配，写回时再换成 CRLF
$crlf = [string][char]13 + [char]10
$lf = [string][char]10

# PowerShell 打印异常时会按终端宽度折行，还在续行前面加 `|` 竖线，
# 于是期望的那句话在输出里被切成了两段。比对前先把空白和竖线全抹掉。
function Get-Comparable([string]$Text) {
    return ($Text -replace '[\s|]+', '')
}

# 用子进程跑：检查脚本靠 throw 报错，同进程调用会把异常直接丢到这里，
# 拿不到「它失败了」这个事实再继续下一条。
function Invoke-CheckerIsolated {
    $shell = Join-Path $PSHOME "pwsh.exe"
    if (-not (Test-Path -LiteralPath $shell)) {
        $shell = Join-Path $PSHOME "powershell.exe"
    }
    $output = & $shell -NoProfile -ExecutionPolicy Bypass -File $checker -RepoRoot $RepoRoot 2>&1 |
        Out-String
    return [pscustomobject]@{ Failed = ($LASTEXITCODE -ne 0); Output = $output }
}

$cases = @(
    @{
        Name = "README 漏掉一个能解码的格式"
        File = "README.md"
        Old = "- 静态：``apng avif avifs blp bmp dds"
        New = "- 静态：``apng avif avifs blp bmp"
        Expect = "Format list regression failed"
    },
    @{
        Name = "默认关联一个解不了的格式"
        File = "YeImageViewer\include\jarkUtils.h"
        Old = '"apng,avif,bmp,gif'
        New = '"abc,apng,avif,bmp,gif'
        Expect = "Format list regression failed"
    },
    @{
        Name = "繁體那一列照抄英文"
        File = "YeImageViewer\src\stringRes.cpp"
        Old = '{"分辨率", "Resolution", "解析度"}'
        New = '{"分辨率", "Resolution", "Resolution"}'
        Expect = "fills the 繁體中文 column with the English text"
    },
    @{
        Name = "在字符串表中间插一条，打乱所有硬编码索引"
        File = "YeImageViewer\src\stringRes.cpp"
        Old = '    {"NULL", "NULL", "NULL"},' + $lf
        New = '    {"NULL", "NULL", "NULL"},' + $lf + '    {"插队", "Intruder", "插隊"},' + $lf
        Expect = "an entry was inserted or removed mid-table"
    },
    @{
        Name = "漏填一列，界面上会显示空白"
        File = "YeImageViewer\src\stringRes.cpp"
        Old = '{"主题", "Theme", "主題"}'
        New = '{"主题", "Theme", ""}'
        Expect = "leaves column 2 empty"
    },
    @{
        Name = "把「界面是不是中文」写成 UI_LANG == 0"
        File = "YeImageViewer\include\jarkUtils.h"
        Old = "    return UiLanguage::isChinese(GlobalVar::settingParameter.UI_LANG);"
        New = "    return GlobalVar::settingParameter.UI_LANG == 0;"
        Expect = "Language regression failed"
    },
    @{
        Name = "派生类析构不再先停预读线程"
        File = "YeImageViewer\include\ImageDatabase.h"
        Old = "        stopPreloadWorker();" + $lf
        New = ""
        Expect = "does not call stopPreloadWorker"
    },
    @{
        Name = "又靠左上角像素颜色认内置提示图"
        File = "YeImageViewer\src\main.cpp"
        Old = "        if (imgDB.isErrorTipsFrame(srcImg)) {"
        New = "        if (*((uint32_t*)srcImg.ptr()) == lightTheme.BG) {"
        Expect = "must be identified by ImageDatabase::isErrorTipsFrame"
    },
    @{
        Name = "PSD 的解码顺序被改回 stb 优先"
        File = "YeImageViewer\src\ImageDatabase.cpp"
        Old = "        img = loadPSD(path, fileBuf);" + $lf +
            "        if (img.empty())" + $lf +
            "            img = loadSTB(path, fileBuf);"
        New = "        img = loadSTB(path, fileBuf);" + $lf +
            "        if (img.empty())" + $lf +
            "            img = loadPSD(path, fileBuf);"
        Expect = "loadSTB runs before loadPSD"
    },
    @{
        Name = "发版只改了 .rc，关于页的版本号还停在上一版"
        File = "YeImageViewer\src\main.cpp"
        Old = 'appVersion = L"v1.37.5"'
        New = 'appVersion = L"v1.37.4"'
        Expect = "the About page shows v1.37.4 while the version resource says"
    },
    @{
        Name = "FileVersion 和 ProductVersion 对不上"
        File = "YeImageViewer\YeImageViewer.rc"
        Old = 'VALUE "FileVersion", "1.37.5.0"'
        New = 'VALUE "FileVersion", "1.38.0.0"'
        Expect = "disagree in YeImageViewer.rc"
    },
    @{
        Name = "README 的版本号没跟着发版一起改"
        File = "README.md"
        Old = "当前版本：**v1.37.5**"
        New = "当前版本：**v1.37.2-rc1**"
        Expect = "README.md says v1.37.2-rc1 while the program is"
    },
    @{
        Name = "测试文档里那份版本号没跟着发版一起改"
        File = "test\README.md"
        Old = 'EXE 版本为 `1.37.5.0`（ProductVersion `1.37.5`）'
        New = 'EXE 版本为 `1.37.4.0`（ProductVersion `1.37.4`）'
        Expect = "test/README.md says 1.37.4.0 / 1.37.4 while the program is"
    }
)

# 先确认干净的树上检查是通过的，否则后面「它失败了」说明不了任何事
$baseline = Invoke-CheckerIsolated
if ($baseline.Failed) {
    throw ("The source invariant checks already fail on an unmodified tree, so breaking things " +
        "proves nothing. Fix this first:`n" + $baseline.Output)
}

$problems = @()
foreach ($case in $cases) {
    $path = Join-Path $RepoRoot $case.File
    $original = [IO.File]::ReadAllBytes($path)
    $text = [Text.Encoding]::UTF8.GetString($original).Replace($crlf, $lf)
    if (-not $text.Contains($case.Old)) {
        $problems += "$($case.Name)：破坏点在 $($case.File) 里找不到了，这条反向测试已经失效"
        Write-Host "STALE $($case.Name)" -ForegroundColor Yellow
        continue
    }

    $index = $text.IndexOf($case.Old)
    $broken = $text.Substring(0, $index) + $case.New + $text.Substring($index + $case.Old.Length)
    try {
        [IO.File]::WriteAllBytes($path, [Text.Encoding]::UTF8.GetBytes($broken.Replace($lf, $crlf)))
        $result = Invoke-CheckerIsolated
    }
    finally {
        [IO.File]::WriteAllBytes($path, $original)
    }

    if (-not $result.Failed) {
        $problems += "$($case.Name)：破坏之后检查竟然还是通过"
        Write-Host "MISSED $($case.Name)" -ForegroundColor Red
    }
    elseif (-not (Get-Comparable $result.Output).Contains((Get-Comparable $case.Expect))) {
        $problems += "$($case.Name)：报错了，但不是预期那条（找不到 '$($case.Expect)'）"
        Write-Host "WRONG  $($case.Name)" -ForegroundColor Red
        Write-Host $result.Output
    }
    else {
        Write-Host "CAUGHT $($case.Name)"
    }
}

$after = Invoke-CheckerIsolated
if ($after.Failed) {
    $problems += "跑完之后检查仍然失败，说明有文件没还原干净：`n" + $after.Output
}

if ($problems.Count -gt 0) {
    throw ("The source invariant checks are not trustworthy:`n - " + ($problems -join "`n - "))
}

Write-Host ("PASS all $($cases.Count) deliberate breakages were caught, and the tree is back to " +
    "where it started.")
