param(
    [string]$Port = "COM3",
    [int]$Baud = 115200,
    [ValidateSet("auto", "jsonl", "binary")]
    [string]$Mode = "auto",
    [int]$DurationSeconds = 300,
    [string]$RunRoot = "C:\code\esp32-temperature-telemetry-platform\data\stability_runs",
    [int]$ConfigTimeoutSeconds = 6,
    [switch]$SkipConfigSnapshot
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$repoRoot = "C:\code\esp32-temperature-telemetry-platform"
$pythonExe = "C:\Espressif\tools\python\v5.4.3\venv\Scripts\python.exe"
$captureTool = Join-Path $repoRoot "tools\scripts\uart_capture.py"
$configTool = Join-Path $repoRoot "tools\scripts\device_config_tool.py"
$summaryTool = Join-Path $repoRoot "tools\scripts\summarize_stability_run.py"

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runDir = Join-Path $RunRoot "run_$timestamp"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$metadataPath = Join-Path $runDir "run_metadata.txt"
@(
    "timestamp=$timestamp"
    "port=$Port"
    "baud=$Baud"
    "mode=$Mode"
    "duration_seconds=$DurationSeconds"
) | Set-Content -Path $metadataPath -Encoding UTF8

$configSnapshotPath = Join-Path $runDir "config_snapshot.json"
if (-not $SkipConfigSnapshot) {
    Write-Host "Capturing device config snapshot..."
    try {
        $configOutput = & $pythonExe $configTool --port $Port --baud $Baud --mode binary --response-mode auto --timeout $ConfigTimeoutSeconds get 2>&1
        if ($LASTEXITCODE -eq 0) {
            $configOutput | Set-Content -Path $configSnapshotPath -Encoding UTF8
        } else {
            $configOutput | Set-Content -Path (Join-Path $runDir "config_snapshot_error.txt") -Encoding UTF8
            Write-Warning "Config snapshot failed, continuing with telemetry capture."
        }
    } catch {
        $_ | Out-String | Set-Content -Path (Join-Path $runDir "config_snapshot_error.txt") -Encoding UTF8
        Write-Warning "Config snapshot threw an exception, continuing with telemetry capture."
    }
}

Write-Host "Starting telemetry capture..."
& $pythonExe $captureTool --port $Port --baud $Baud --mode $Mode --duration $DurationSeconds --out-dir $runDir
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$jsonlFile = Get-ChildItem -Path $runDir -Filter "*.jsonl" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$csvFile = Get-ChildItem -Path $runDir -Filter "*.csv" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$binFile = Get-ChildItem -Path $runDir -Filter "*.bin" | Sort-Object LastWriteTime -Descending | Select-Object -First 1

$telemetryRows = 0
$firstHostTime = ""
$lastHostTime = ""
if ($csvFile) {
    $rows = Import-Csv -Path $csvFile.FullName
    $telemetryRows = @($rows).Count
    if ($telemetryRows -gt 0) {
        $firstHostTime = $rows[0].host_time
        $lastHostTime = $rows[-1].host_time
    }
}

$summaryPath = Join-Path $runDir "stability_summary.md"
& $pythonExe $summaryTool --run-dir $runDir | Set-Content -Path $summaryPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Add-Content -Path $summaryPath -Encoding UTF8 -Value ""
Add-Content -Path $summaryPath -Encoding UTF8 -Value "## Capture metadata"
Add-Content -Path $summaryPath -Encoding UTF8 -Value ""
Add-Content -Path $summaryPath -Encoding UTF8 -Value "- Timestamp: $timestamp"
Add-Content -Path $summaryPath -Encoding UTF8 -Value "- Port: $Port"
Add-Content -Path $summaryPath -Encoding UTF8 -Value "- Baud: $Baud"
Add-Content -Path $summaryPath -Encoding UTF8 -Value "- Mode: $Mode"
Add-Content -Path $summaryPath -Encoding UTF8 -Value "- Requested duration (seconds): $DurationSeconds"
Add-Content -Path $summaryPath -Encoding UTF8 -Value "- Config snapshot: $(if ((Test-Path $configSnapshotPath)) { $configSnapshotPath } else { "not captured" })"

Write-Host ""
Write-Host "Stability capture complete."
Write-Host "Run directory: $runDir"
Write-Host "Summary: $summaryPath"
