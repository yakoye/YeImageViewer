param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# 装 Visual Studio 一定会带 vswhere，所以找不到它就是根本没装，
# 而不是「装了但位置不对」——把这句话说明白，省掉一轮猜。
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw ("Visual Studio Installer (vswhere.exe) was not found at ${vswhere}. " +
        "Install Visual Studio 2026 or the Build Tools with the C++ workload.")
}

$installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $installationPath) {
    throw ("No Visual Studio installation with MSBuild was found. This project needs the " +
        "v145 toolset, which ships with Visual Studio 2026 / Build Tools 2026.")
}
$installationPath = $installationPath.Trim()

$msbuild = Join-Path $installationPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
if (-not (Test-Path -LiteralPath $msbuild)) {
    throw ("MSBuild was not found at ${msbuild}. The Visual Studio installation at " +
        "${installationPath} is missing the C++ build tools; add the " +
        "'Desktop development with C++' workload.")
}

$solution = Join-Path $PSScriptRoot "YeImageViewer.slnx"
if (-not (Test-Path -LiteralPath $solution)) {
    throw "The solution file was not found at ${solution}."
}

# 版本号里要带提交号，但没有 git（或这份源码不是 git 检出）也得能编过。
$gitCommitId = "unknown"
try {
    $described = & git -C $PSScriptRoot rev-parse --short=12 HEAD 2>$null
    if ($LASTEXITCODE -eq 0 -and $described) {
        $gitCommitId = ([string]$described).Trim()
    }
}
catch {
    # git 没装或不在 PATH 里：留着 unknown，不影响编译
}

$psi = [System.Diagnostics.ProcessStartInfo]::new($msbuild)
@($solution, "/m", "/p:Configuration=$Configuration", "/p:Platform=x64", "/p:GitCommitId=$gitCommitId", "/v:m") | ForEach-Object {
    [void]$psi.ArgumentList.Add($_)
}

$psi.WorkingDirectory = $PSScriptRoot
$psi.UseShellExecute = $false

$envItems = Get-ChildItem Env: | Sort-Object Name -Unique
$psi.Environment.Clear()
foreach ($item in $envItems) {
    if (-not $psi.Environment.ContainsKey($item.Name)) {
        $psi.Environment[$item.Name] = $item.Value
    }
}

if ($psi.Environment.ContainsKey("PATH")) {
    $pathValue = $psi.Environment["PATH"]
    [void]$psi.Environment.Remove("PATH")
    $psi.Environment["Path"] = $pathValue
}

$process = [System.Diagnostics.Process]::Start($psi)
if (-not $process) {
    throw "Failed to start MSBuild at ${msbuild}."
}
$process.WaitForExit()
if ($process.ExitCode -ne 0) {
    Write-Host "MSBuild failed with exit code $($process.ExitCode)." -ForegroundColor Red
}
exit $process.ExitCode
