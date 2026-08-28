param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "artifacts\release"),
    [switch]$SkipBuild,
    [switch]$IncludeCompatibilityZip
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = $PSScriptRoot
$releaseDirectory = Join-Path $repoRoot "x64\Release"
$viewer = Join-Path $releaseDirectory "YeImageViewer.exe"
$thumbnailProvider = Join-Path $releaseDirectory "YeThumbnailProvider.dll"

if (-not $SkipBuild) {
    $shell = Join-Path $PSHOME "pwsh.exe"
    if (-not (Test-Path -LiteralPath $shell)) {
        throw "PowerShell 7 is required to build the Release package."
    }
    & $shell -NoProfile -File (Join-Path $repoRoot "buildRelease.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed with exit code $LASTEXITCODE."
    }
}

foreach ($requiredFile in @($viewer, $thumbnailProvider)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Release runtime file is missing: $requiredFile"
    }
}

$fileVersion = (Get-Item -LiteralPath $viewer).VersionInfo.FileVersion
$versionParts = @($fileVersion -split "\.")
if ($versionParts.Count -lt 3) {
    throw "Unexpected viewer file version: $fileVersion"
}
$version = "v$($versionParts[0]).$($versionParts[1]).$($versionParts[2])"
$packageName = "YeImageViewer-$version-win-x64-full"

$sevenZipCandidates = @(
    (Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
)
$sevenZipCommand = Get-Command 7z.exe -ErrorAction SilentlyContinue
if ($sevenZipCommand) {
    $sevenZip = $sevenZipCommand.Source
}
else {
    $sevenZip = $sevenZipCandidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1
}
if (-not $sevenZip) {
    throw "7-Zip is required to create the compact full package."
}

$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporaryRoot = Join-Path $temporaryBase ("YeImageViewer-Package-" + [Guid]::NewGuid().ToString("N"))
$stagingRoot = Join-Path $temporaryRoot $packageName
$stagingRuntime = Join-Path $stagingRoot "x64\Release"
$installerStaging = Join-Path $temporaryRoot "installer"

try {
    New-Item -ItemType Directory -Path $stagingRuntime -Force | Out-Null
    Copy-Item -LiteralPath $viewer -Destination (Join-Path $stagingRuntime "YeImageViewer.exe")
    Copy-Item -LiteralPath $thumbnailProvider -Destination (Join-Path $stagingRuntime "YeThumbnailProvider.dll")
    foreach ($document in @(
        "CHANGELOG.md",
        "installLocal.ps1",
        "LICENSE",
        "README.md",
        "README_EN.md",
        "THIRD_PARTY_NOTICES.md",
        "UPSTREAM.md"
    )) {
        Copy-Item -LiteralPath (Join-Path $repoRoot $document) -Destination (Join-Path $stagingRoot $document)
    }

    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $archive = Join-Path $OutputDirectory "$packageName.7z"
    if (Test-Path -LiteralPath $archive) {
        [IO.File]::Delete([IO.Path]::GetFullPath($archive))
    }

    & $sevenZip a -t7z -mx=9 -m0=lzma2 -md=64m -ms=on -mmt=on $archive $stagingRoot | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        throw "7-Zip package creation failed with exit code $LASTEXITCODE."
    }

    $maximumDownloadBytes = 25MB
    $archiveInfo = Get-Item -LiteralPath $archive
    if ($archiveInfo.Length -gt $maximumDownloadBytes) {
        throw "Full package is $([math]::Round($archiveInfo.Length / 1MB, 2)) MiB, above the 25 MiB release limit."
    }

    $installerSfx = Join-Path $repoRoot "tools\installer\7zSD.sfx"
    if (-not (Test-Path -LiteralPath $installerSfx -PathType Leaf)) {
        throw "The pinned LZMA SDK installer module is missing: $installerSfx"
    }
    New-Item -ItemType Directory -Path $installerStaging -Force | Out-Null
    Copy-Item -LiteralPath $viewer -Destination (Join-Path $installerStaging "YeImageViewer.exe")
    Copy-Item -LiteralPath $thumbnailProvider -Destination (Join-Path $installerStaging "YeThumbnailProvider.dll")
    Copy-Item -LiteralPath (Join-Path $repoRoot "installLocal.ps1") `
        -Destination (Join-Path $installerStaging "installLocal.ps1")
    $installer = [IO.Path]::GetFullPath((Join-Path $OutputDirectory "$packageName-setup.exe"))
    if (Test-Path -LiteralPath $installer) {
        [IO.File]::Delete($installer)
    }
    $installerPayload = Join-Path $temporaryRoot "YeImageViewer-installer.7z"
    Push-Location $installerStaging
    try {
        & $sevenZip a -t7z -mx=9 -m0=lzma2 -md=64m -ms=on -mmt=on `
            $installerPayload "YeImageViewer.exe" "YeThumbnailProvider.dll" "installLocal.ps1" | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "One-click installer payload creation failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
    $installerConfig = Join-Path $temporaryRoot "YeImageViewer-installer-config.txt"
    $configText = @'
;!@Install@!UTF-8!
Title="YeImageViewer"
RunProgram="powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File installLocal.ps1"
;!@InstallEnd@!
'@
    [IO.File]::WriteAllText($installerConfig, $configText,
        [Text.UTF8Encoding]::new($false))
    $installerStream = [IO.File]::Create($installer)
    try {
        foreach ($part in @($installerSfx, $installerConfig, $installerPayload)) {
            $partStream = [IO.File]::OpenRead($part)
            try {
                $partStream.CopyTo($installerStream)
            }
            finally {
                $partStream.Dispose()
            }
        }
    }
    finally {
        $installerStream.Dispose()
    }
    if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
        throw "One-click installer creation failed."
    }
    $installerInfo = Get-Item -LiteralPath $installer
    if ($installerInfo.Length -gt $maximumDownloadBytes) {
        throw "One-click installer is $([math]::Round($installerInfo.Length / 1MB, 2)) MiB, above the 25 MiB release limit."
    }
    $installerVerifyDirectory = Join-Path $temporaryRoot "installer-verify"
    New-Item -ItemType Directory -Path $installerVerifyDirectory -Force | Out-Null
    & $sevenZip x -y "-o$installerVerifyDirectory" $installer | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "One-click installer verification extraction failed with exit code $LASTEXITCODE."
    }
    foreach ($payloadFile in @("YeImageViewer.exe", "YeThumbnailProvider.dll", "installLocal.ps1")) {
        $expectedPayloadFile = Join-Path $installerStaging $payloadFile
        $actualPayloadFile = Join-Path $installerVerifyDirectory $payloadFile
        if (-not (Test-Path -LiteralPath $actualPayloadFile -PathType Leaf) -or
            (Get-FileHash -LiteralPath $expectedPayloadFile -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $actualPayloadFile -Algorithm SHA256).Hash) {
            throw "One-click installer verification failed for $payloadFile."
        }
    }

    $outputs = @($archive, $installer)
    if ($IncludeCompatibilityZip) {
        $zip = Join-Path $OutputDirectory "$packageName.zip"
        if (Test-Path -LiteralPath $zip) {
            [IO.File]::Delete([IO.Path]::GetFullPath($zip))
        }
        Compress-Archive -LiteralPath $stagingRoot -DestinationPath $zip -CompressionLevel Optimal
        $outputs += $zip
    }

    $checksums = foreach ($output in $outputs) {
        $hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
        "$($hash.Hash)  $([IO.Path]::GetFileName($output))"
    }
    $checksumPath = Join-Path $OutputDirectory "SHA256SUMS.txt"
    [IO.File]::WriteAllLines($checksumPath, $checksums, [Text.UTF8Encoding]::new($false))

    [PSCustomObject]@{
        Version = $version
        FullPackage = $archive
        FullPackageMiB = [math]::Round($archiveInfo.Length / 1MB, 2)
        OneClickInstaller = $installer
        OneClickInstallerMiB = [math]::Round($installerInfo.Length / 1MB, 2)
        CompatibilityZip = if ($IncludeCompatibilityZip) { $outputs[-1] } else { $null }
        Checksums = $checksumPath
    }
}
finally {
    $resolvedTemporaryRoot = [IO.Path]::GetFullPath($temporaryRoot)
    if ($resolvedTemporaryRoot.StartsWith($temporaryBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedTemporaryRoot).StartsWith("YeImageViewer-Package-", [StringComparison]::Ordinal)) {
        if (Test-Path -LiteralPath $resolvedTemporaryRoot) {
            [IO.Directory]::Delete($resolvedTemporaryRoot, $true)
        }
    }
}
