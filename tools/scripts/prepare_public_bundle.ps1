param(
    [string]$RepoRoot = "C:\code\esp32-temperature-telemetry-platform",
    [string]$OutputRoot = "C:\code\esp32-temperature-telemetry-platform\out\public_bundle",
    [string]$BundleName = "esp32-temperature-telemetry-platform",
    [switch]$IncludeSamples
)

$ErrorActionPreference = "Stop"

$sourceRoot = (Resolve-Path $RepoRoot).Path
$bundleRoot = Join-Path $OutputRoot $BundleName

if (Test-Path $bundleRoot) {
    $gitDir = Join-Path $bundleRoot ".git"
    if (Test-Path $gitDir) {
        Get-ChildItem -Force -LiteralPath $bundleRoot |
            Where-Object { $_.Name -ne ".git" } |
            Remove-Item -Recurse -Force
    }
    else {
        Remove-Item -Recurse -Force $bundleRoot
    }
}

New-Item -ItemType Directory -Force -Path $bundleRoot | Out-Null

$filesToCopy = @(
    ".editorconfig",
    ".gitignore",
    "CMakeLists.txt",
    "README.md"
)

$directoriesToCopy = @(
    "firmware",
    "desktop",
    "tools",
    "tests",
    "deploy"
)

foreach ($relativePath in $filesToCopy) {
    $source = Join-Path $sourceRoot $relativePath
    if (Test-Path $source) {
        Copy-Item -Path $source -Destination (Join-Path $bundleRoot $relativePath)
    }
}

foreach ($relativePath in $directoriesToCopy) {
    $source = Join-Path $sourceRoot $relativePath
    if (Test-Path $source) {
        Copy-Item -Path $source -Destination (Join-Path $bundleRoot $relativePath) -Recurse -Force
    }
}

if ($IncludeSamples) {
    $sampleSource = Join-Path $sourceRoot "data\samples"
    if (Test-Path $sampleSource) {
        $sampleTarget = Join-Path $bundleRoot "data\samples"
        New-Item -ItemType Directory -Force -Path $sampleTarget | Out-Null
        Copy-Item -Path (Join-Path $sampleSource "*") -Destination $sampleTarget -Recurse -Force
    }
}

$manifestPath = Join-Path $bundleRoot "PUBLIC_BUNDLE_NOTES.txt"
if (Test-Path $manifestPath) {
    Remove-Item -Force $manifestPath
}

Write-Host ""
Write-Host "Public bundle created:"
Write-Host $bundleRoot
Write-Host ""
Write-Host "Included:"
Write-Host "- README.md"
Write-Host "- firmware/"
Write-Host "- desktop/"
Write-Host "- tools/"
Write-Host "- tests/"
Write-Host "- deploy/"
if ($IncludeSamples) {
    Write-Host "- data/samples/"
}
Write-Host ""
Write-Host "Excluded:"
Write-Host "- docs/"
Write-Host "- data/config/"
Write-Host "- data/logs/"
Write-Host "- data/stability_runs/"
