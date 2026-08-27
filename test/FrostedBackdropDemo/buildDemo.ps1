$ErrorActionPreference = "Stop"

$demoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $demoRoot "FrostedBackdropDemo.cpp"
$output = Join-Path $demoRoot "FrostedBackdropDemo.exe"
$object = Join-Path $demoRoot "FrostedBackdropDemo.obj"
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"

if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio Build Tools environment was not found."
}

$compile = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cl /nologo /std:c++20 /EHsc "{1}" /Fo:"{3}" /Fe:"{2}" user32.lib gdi32.lib' -f `
    $vsDevCmd, $source, $output, $object
& cmd.exe /d /s /c $compile
if ($LASTEXITCODE -ne 0) {
    throw "Frosted backdrop demo build failed with exit code $LASTEXITCODE."
}

Write-Host "Built $output"
