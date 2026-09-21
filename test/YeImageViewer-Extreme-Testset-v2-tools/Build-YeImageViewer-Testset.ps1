param(
    [string]$Root = (Join-Path $PSScriptRoot "YeImageViewer-Extreme-Testset-v2-data"),
    [switch]$SkipRealLarge,
    [switch]$SkipGenerated,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "=== $Text ===" -ForegroundColor Cyan
}

function Ensure-Dir([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Download-File {
    param(
        [Parameter(Mandatory=$true)][string]$Url,
        [Parameter(Mandatory=$true)][string]$OutFile
    )

    Ensure-Dir (Split-Path -Parent $OutFile)

    if ((Test-Path -LiteralPath $OutFile) -and -not $Force) {
        $len = (Get-Item -LiteralPath $OutFile).Length
        if ($len -gt 1024) {
            Write-Host "[SKIP] $([IO.Path]::GetFileName($OutFile)) ($([math]::Round($len/1MB,2)) MB)"
            return
        }
    }

    $tmp = "$OutFile.part"
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue

    Write-Host "[GET ] $([IO.Path]::GetFileName($OutFile))"

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -L --fail --retry 3 --retry-delay 2 `
            -A "YeImageViewer-Extreme-Testset-v2/1.0" `
            -o $tmp $Url
        if ($LASTEXITCODE -ne 0) {
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
            throw "curl download failed: $Url"
        }
    }
    else {
        Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing `
            -Headers @{ "User-Agent" = "YeImageViewer-Extreme-Testset-v2/1.0" }
    }

    Move-Item -LiteralPath $tmp -Destination $OutFile -Force
}

function Download-Wikimedia {
    param(
        [Parameter(Mandatory=$true)][string]$FileName,
        [Parameter(Mandatory=$true)][string]$DestDir,
        [string]$Prefix = ""
    )
    $escaped = [Uri]::EscapeDataString($FileName)
    $url = "https://commons.wikimedia.org/wiki/Special:Redirect/file/$escaped"
    $outName = if ($Prefix) { "$Prefix$FileName" } else { $FileName }
    Download-File -Url $url -OutFile (Join-Path $DestDir $outName)
}

function Download-ZipAndExpand {
    param(
        [string]$Url,
        [string]$ZipPath,
        [string]$ExpandDir
    )

    Download-File -Url $Url -OutFile $ZipPath

    if (Test-Path -LiteralPath $ExpandDir) {
        Remove-Item -LiteralPath $ExpandDir -Recurse -Force
    }
    Ensure-Dir $ExpandDir
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $ExpandDir -Force
}

function Copy-FilesNumbered {
    param(
        [System.Collections.IEnumerable]$Files,
        [string]$DestDir,
        [int]$Count
    )
    Ensure-Dir $DestDir
    $i = 1
    foreach ($f in ($Files | Select-Object -First $Count)) {
        $name = "{0:D2}_{1}" -f $i, $f.Name
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $DestDir $name) -Force
        $i++
    }
}

function Find-Python {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) { return @($py.Source, "-3") }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) { return @($python.Source) }

    $python3 = Get-Command python3.exe -ErrorAction SilentlyContinue
    if ($python3) { return @($python3.Source) }

    return $null
}

function Add-MixedLink {
    param(
        [string]$Source,
        [string]$Destination
    )
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Force
    }
    try {
        New-Item -ItemType HardLink -Path $Destination -Target $Source -ErrorAction Stop | Out-Null
    }
    catch {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

Write-Step "Preparing folders"
Ensure-Dir $Root

$folders = @(
    "01_Real_PNG_50-70MB",
    "02_Real_100MP_Plus",
    "03_Generated_200MP_Plus",
    "04_Alpha_RGBA",
    "05_16bit_PNG",
    "06_Adam7_Interlaced",
    "07_ICC_WideGamut",
    "08_UltraWide_Tall",
    "09_Broken_PNG",
    "10_Metadata_PNG",
    "11_PngSuite_Mixed",
    "12_Mixed_Switch",
    "_licenses"
)
foreach ($f in $folders) { Ensure-Dir (Join-Path $Root $f) }

$temp = Join-Path $Root "_tmp"
Ensure-Dir $temp

if (-not $SkipRealLarge) {
    Write-Step "Downloading 10 real 50-70MB PNG files"

    $real50 = @(
        "Bayerische Vermessungsverwaltung - DOP - 611000 5310000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 606000 5434000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 611000 5300000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 555000 5510000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 605000 5310000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 556000 5534000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 606000 5513000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 715000 5563000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 675000 5560000 (2025).png",
        "Bayerische Vermessungsverwaltung - DOP - 568000 5510000 (2025).png"
    )

    $d1 = Join-Path $Root "01_Real_PNG_50-70MB"
    for ($i = 0; $i -lt $real50.Count; $i++) {
        Download-Wikimedia -FileName $real50[$i] -DestDir $d1 -Prefix ("{0:D2}_" -f ($i+1))
    }

    Write-Step "Downloading 10 real 100MP+ PNG files"

    $real100mp = @(
        "Mandelbrot Set Image 106.png",
        "Mandelbrot Set Image 107.png",
        "Mandelbrot Set Image 108.png",
        "Mandelbrot Set Image 109.png",
        "Mandelbrot Set Image 110.png",
        "Mandelbrot Set Image 111.png",
        "Mandelbrot Set Image 112.png",
        "Mandelbrot Set Image 113.png",
        "Burning Ship Ultra HD Render By Graphiq.png",
        "Lyapunov exponent of double pendulums.png"
    )

    $d2 = Join-Path $Root "02_Real_100MP_Plus"
    for ($i = 0; $i -lt $real100mp.Count; $i++) {
        Download-Wikimedia -FileName $real100mp[$i] -DestDir $d2 -Prefix ("{0:D2}_" -f ($i+1))
    }
}
else {
    Write-Step "Skipping real large-image downloads"
}

Write-Step "Downloading PngSuite"
$pngSuiteZip = Join-Path $temp "pngsuite-main.zip"
$pngSuiteExpand = Join-Path $temp "pngsuite"
Download-ZipAndExpand `
    -Url "https://github.com/lunapaint/pngsuite/archive/refs/heads/main.zip" `
    -ZipPath $pngSuiteZip `
    -ExpandDir $pngSuiteExpand

$pngSuiteDir = Get-ChildItem -Path $pngSuiteExpand -Directory -Recurse |
    Where-Object { $_.Name -eq "png" } |
    Select-Object -First 1

if (-not $pngSuiteDir) {
    throw "Could not locate PngSuite png directory."
}

$allPng = Get-ChildItem -LiteralPath $pngSuiteDir.FullName -File -Filter "*.png" | Sort-Object Name

# Alpha: names containing PNG color type 4a or 6a
$alphaFiles = $allPng | Where-Object { $_.BaseName -match '(4a|6a)' }
Copy-FilesNumbered -Files $alphaFiles -DestDir (Join-Path $Root "04_Alpha_RGBA") -Count 10

# 16-bit: filename ends in 16 according to PngSuite naming
$bit16Files = $allPng | Where-Object { $_.BaseName -match '16$' }
Copy-FilesNumbered -Files $bit16Files -DestDir (Join-Path $Root "05_16bit_PNG") -Count 10

# Adam7: PngSuite naming puts i/n at the fourth position for standard image files.
$interlacedFiles = $allPng | Where-Object { $_.BaseName -match '^...i' }
Copy-FilesNumbered -Files $interlacedFiles -DestDir (Join-Path $Root "06_Adam7_Interlaced") -Count 10

# Mixed valid/basic cases; avoid files beginning with x (PngSuite erroneous-file family).
$mixedPngSuite = $allPng | Where-Object { $_.BaseName -notmatch '^x' }
Copy-FilesNumbered -Files $mixedPngSuite -DestDir (Join-Path $Root "11_PngSuite_Mixed") -Count 30

# Preserve license/readme.
$pngSuiteRoot = Split-Path -Parent $pngSuiteDir.FullName
foreach ($n in @("LICENSE", "README.md", "PngSuite.README")) {
    $p = Join-Path $pngSuiteRoot $n
    if (Test-Path -LiteralPath $p) {
        Copy-Item -LiteralPath $p -Destination (Join-Path $Root "_licenses\pngsuite_$n") -Force
    }
}

Write-Step "Downloading ICC / P3 / Rec.2020 wide-gamut tests"
$wideZip = Join-Path $temp "wide-gamut-tests-master.zip"
$wideExpand = Join-Path $temp "wide-gamut"
Download-ZipAndExpand `
    -Url "https://github.com/codelogic/wide-gamut-tests/archive/refs/heads/master.zip" `
    -ZipPath $wideZip `
    -ExpandDir $wideExpand

$wideRoot = Get-ChildItem -Path $wideExpand -Directory |
    Where-Object { $_.Name -like "wide-gamut-tests-*" } |
    Select-Object -First 1

if (-not $wideRoot) {
    throw "Could not locate wide-gamut-tests directory."
}

$iccNames = @(
    "P3-sRGB-red.png",
    "P3-sRGB-green.png",
    "P3-sRGB-blue.png",
    "P3-sRGB-color-ring.png",
    "P3-sRGB-color-bars.png",
    "R2020-sRGB-red.png",
    "R2020-sRGB-green.png",
    "R2020-sRGB-blue.png",
    "R2020-sRGB-color-ring.png",
    "R2020-sRGB-color-bars.png"
)

$iccDest = Join-Path $Root "07_ICC_WideGamut"
for ($i = 0; $i -lt $iccNames.Count; $i++) {
    $src = Join-Path $wideRoot.FullName $iccNames[$i]
    if (Test-Path -LiteralPath $src) {
        Copy-Item -LiteralPath $src `
            -Destination (Join-Path $iccDest ("{0:D2}_{1}" -f ($i+1), $iccNames[$i])) -Force
    }
}

$wideLicense = Join-Path $wideRoot.FullName "LICENSE"
if (Test-Path -LiteralPath $wideLicense) {
    Copy-Item -LiteralPath $wideLicense -Destination (Join-Path $Root "_licenses\wide-gamut-tests_LICENSE") -Force
}

if (-not $SkipGenerated) {
    Write-Step "Generating 200MP+, ultra-wide/tall, broken and metadata PNGs"

    $pyCmd = Find-Python
    if ($null -eq $pyCmd) {
        Write-Warning "Python was not found. Generated groups 03/08/09/10 will be skipped."
    }
    else {
        $generator = Join-Path $PSScriptRoot "generate_extreme_png.py"
        if (-not (Test-Path -LiteralPath $generator)) {
            throw "Missing generator: $generator"
        }

        if ($pyCmd.Count -eq 2) {
            & $pyCmd[0] $pyCmd[1] $generator --root $Root
        }
        else {
            & $pyCmd[0] $generator --root $Root
        }

        if ($LASTEXITCODE -ne 0) {
            throw "generate_extreme_png.py failed."
        }
    }
}
else {
    Write-Step "Skipping generated extreme PNG groups"
}

Write-Step "Building Mixed_Switch directory"
$mixedDir = Join-Path $Root "12_Mixed_Switch"
Get-ChildItem -LiteralPath $mixedDir -File -ErrorAction SilentlyContinue | Remove-Item -Force

$sourceDirs = @(
    "01_Real_PNG_50-70MB",
    "03_Generated_200MP_Plus",
    "05_16bit_PNG",
    "07_ICC_WideGamut",
    "09_Broken_PNG",
    "04_Alpha_RGBA",
    "02_Real_100MP_Plus",
    "08_UltraWide_Tall",
    "10_Metadata_PNG",
    "06_Adam7_Interlaced",
    "11_PngSuite_Mixed"
)

$queues = @{}
foreach ($d in $sourceDirs) {
    $path = Join-Path $Root $d
    $queues[$d] = @(
        Get-ChildItem -LiteralPath $path -File -Filter "*.png" -ErrorAction SilentlyContinue |
        Sort-Object Name
    )
}

$positions = @{}
foreach ($d in $sourceDirs) { $positions[$d] = 0 }

$mixIndex = 1
while ($mixIndex -le 40) {
    $added = $false

    foreach ($d in $sourceDirs) {
        if ($mixIndex -gt 40) { break }

        $arr = $queues[$d]
        $pos = [int]$positions[$d]

        if ($arr.Count -gt $pos) {
            $src = $arr[$pos]
            $positions[$d] = $pos + 1

            $safeCategory = $d -replace '^\d+_', ''
            $destName = "{0:D3}_{1}_{2}" -f $mixIndex, $safeCategory, $src.Name
            Add-MixedLink -Source $src.FullName -Destination (Join-Path $mixedDir $destName)

            $mixIndex++
            $added = $true
        }
    }

    if (-not $added) { break }
}

Write-Step "Writing MANIFEST.csv"
$manifest = foreach ($d in $folders | Where-Object { $_ -notmatch '^_' }) {
    $dir = Join-Path $Root $d
    Get-ChildItem -LiteralPath $dir -File -Filter "*.png" -ErrorAction SilentlyContinue | ForEach-Object {
        [PSCustomObject]@{
            Category = $d
            FileName = $_.Name
            SizeMB   = [math]::Round($_.Length / 1MB, 3)
            FullPath = $_.FullName
        }
    }
}
$manifest | Export-Csv -LiteralPath (Join-Path $Root "MANIFEST.csv") -NoTypeInformation -Encoding UTF8

Write-Step "Summary"
$summary = foreach ($d in $folders | Where-Object { $_ -notmatch '^_' }) {
    $dir = Join-Path $Root $d
    $items = @(Get-ChildItem -LiteralPath $dir -File -Filter "*.png" -ErrorAction SilentlyContinue)
    [PSCustomObject]@{
        Category = $d
        Count = $items.Count
        DiskMB = [math]::Round((($items | Measure-Object Length -Sum).Sum) / 1MB, 2)
    }
}
$summary | Format-Table -AutoSize

Write-Host ""
Write-Host "Test set root:" -ForegroundColor Green
Write-Host $Root
Write-Host ""
Write-Host "Recommended first test: open 12_Mixed_Switch and rapidly press Left/Right." -ForegroundColor Green
