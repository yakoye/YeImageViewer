$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}

$installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $installationPath) {
    throw "Visual Studio 2026 with MSBuild was not found."
}

$msbuild = Join-Path $installationPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
$solution = Join-Path $PSScriptRoot "YeImageViewer.slnx"
$gitCommitId = (& git -C $PSScriptRoot rev-parse --short=12 HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $gitCommitId) {
    $gitCommitId = "unknown"
}
else {
    $gitCommitId = $gitCommitId.Trim()
}

$psi = [System.Diagnostics.ProcessStartInfo]::new($msbuild)
@($solution, "/m", "/p:Configuration=Release", "/p:Platform=x64", "/p:GitCommitId=$gitCommitId", "/v:m") | ForEach-Object {
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
$process.WaitForExit()
exit $process.ExitCode
