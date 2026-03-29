param(
    [ValidateSet("binary", "jsonl", "serial")]
    [string]$Mode = "binary",
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [ValidateSet("windows", "offscreen", "minimal")]
    [string]$Platform = "windows",
    [switch]$ResetProcesses
)

$ErrorActionPreference = "Stop"
$env:QT_QPA_PLATFORM = $Platform

$repoRoot = "C:\code\esp32-temperature-telemetry-platform"
$qtMonitor = Join-Path $repoRoot "build-desktop\desktop\qt_monitor\qt_monitor.exe"
$telemetryService = Join-Path $repoRoot "build-desktop\desktop\telemetry_service\telemetry_service.exe"

if ($ResetProcesses) {
    Get-Process qt_monitor -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process telemetry_service -ErrorAction SilentlyContinue | Stop-Process -Force
}

if ($Mode -eq "serial") {
    $serviceArgs = @("--port", $Port, "--baud", "$Baud", "--mode", "auto")
    $monitorArgs = @("--port", $Port, "--baud", "$Baud", "--mode", "auto")
    $sourceLabel = "serial:$Port@$Baud"
} elseif ($Mode -eq "binary") {
    $replayPath = Join-Path $repoRoot "data\samples\m2_binary_reference.bin"
    $serviceArgs = @("--replay", $replayPath, "--mode", "binary")
    $monitorArgs = @("--replay", $replayPath, "--mode", "binary")
    $sourceLabel = $replayPath
} else {
    $replayPath = Join-Path $repoRoot "data\samples\m2_jsonl_v2_device_reference.jsonl"
    $serviceArgs = @("--replay", $replayPath, "--mode", "jsonl")
    $monitorArgs = @("--replay", $replayPath, "--mode", "jsonl")
    $sourceLabel = $replayPath
}

Write-Host "Launching desktop demo..."
Write-Host "mode: $Mode"
Write-Host "platform: $Platform"
Write-Host "source: $sourceLabel"

Start-Process -FilePath $telemetryService -ArgumentList $serviceArgs | Out-Null
Start-Sleep -Milliseconds 300
Start-Process -FilePath $qtMonitor -ArgumentList $monitorArgs | Out-Null
