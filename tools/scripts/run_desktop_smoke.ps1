param(
    [ValidateSet("binary", "jsonl", "both")]
    [string]$Mode = "both",
    [ValidateSet("offscreen", "windows", "minimal")]
    [string]$Platform = "offscreen",
    [switch]$Build
)

$ErrorActionPreference = "Stop"

$repoRoot = "C:\code\esp32-temperature-telemetry-platform"
$telemetryService = Join-Path $repoRoot "build-desktop\desktop\telemetry_service\telemetry_service.exe"
$assetsDir = Join-Path $repoRoot "docs\reports\assets"
$buildScript = Join-Path $repoRoot "tools\scripts\desktop_build.ps1"
$captureScript = Join-Path $repoRoot "tools\scripts\capture_qt_screenshots.ps1"

New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

Get-Process qt_monitor -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process telemetry_service -ErrorAction SilentlyContinue | Stop-Process -Force

if ($Build) {
    & powershell -ExecutionPolicy Bypass -File $buildScript
}

$env:QT_QPA_PLATFORM = $Platform

function Invoke-ServiceSmoke {
    param(
        [string]$ReplayPath,
        [string]$ProtocolMode,
        [string]$OutputPath
    )

    & $telemetryService --replay $ReplayPath --mode $ProtocolMode --duration 5 *> $OutputPath
    if ($LASTEXITCODE -ne 0) {
        throw "telemetry_service failed for mode '$ProtocolMode'"
    }
    return Get-Item $OutputPath
}

$binaryReplay = Join-Path $repoRoot "data\samples\m2_binary_reference.bin"
$jsonReplay = Join-Path $repoRoot "data\samples\m2_jsonl_v2_device_reference.jsonl"

$results = @()

if ($Mode -in @("binary", "both")) {
    $results += Invoke-ServiceSmoke -ReplayPath $binaryReplay -ProtocolMode "binary" -OutputPath (Join-Path $assetsDir "telemetry_service_binary_smoke.txt")
}

if ($Mode -in @("jsonl", "both")) {
    $results += Invoke-ServiceSmoke -ReplayPath $jsonReplay -ProtocolMode "jsonl" -OutputPath (Join-Path $assetsDir "telemetry_service_jsonl_smoke.txt")
}

& powershell -ExecutionPolicy Bypass -File $captureScript -Mode $Mode -Tab all -Platform $Platform
if ($LASTEXITCODE -ne 0) {
    throw "capture_qt_screenshots failed"
}

$qtArtifactNames = @()
if ($Mode -in @("binary", "both")) {
    $qtArtifactNames += @(
        "qt_mvp_binary.png",
        "qt_mvp_binary_overview.png",
        "qt_mvp_binary_trend.png",
        "qt_mvp_binary_faults.png",
        "qt_mvp_binary_config.png"
    )
}

if ($Mode -in @("jsonl", "both")) {
    $qtArtifactNames += @(
        "qt_mvp_jsonl.png",
        "qt_mvp_jsonl_overview.png",
        "qt_mvp_jsonl_trend.png",
        "qt_mvp_jsonl_faults.png",
        "qt_mvp_jsonl_config.png"
    )
}

$results += Get-ChildItem $assetsDir -File | Where-Object { $_.Name -in $qtArtifactNames }

$results | Select-Object FullName, Length, LastWriteTime
