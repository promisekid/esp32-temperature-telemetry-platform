param(
    [string]$BinaryReplay = "C:\code\esp32-temperature-telemetry-platform\data\samples\m2_binary_reference.bin",
    [string]$JsonReplay = "C:\code\esp32-temperature-telemetry-platform\data\samples\m2_jsonl_v2_device_reference.jsonl",
    [string]$OutputDir = "C:\code\esp32-temperature-telemetry-platform\docs\reports\assets",
    [ValidateSet("binary", "jsonl", "both")]
    [string]$Mode = "both",
    [ValidateSet("overview", "trend", "faults", "config", "all")]
    [string]$Tab = "overview",
    [ValidateSet("offscreen", "windows", "minimal")]
    [string]$Platform = "offscreen"
)

$ErrorActionPreference = "Stop"

$repoRoot = "C:\code\esp32-temperature-telemetry-platform"
$qtMonitor = Join-Path $repoRoot "build-desktop\desktop\qt_monitor\qt_monitor.exe"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$env:QT_QPA_PLATFORM = $Platform

function Invoke-Capture {
    param(
        [string]$ReplayPath,
        [string]$ReplayMode,
        [string]$TabName,
        [string]$OutputPath
    )

    $proc = Start-Process -FilePath $qtMonitor `
        -ArgumentList @(
            "--replay", $ReplayPath,
            "--mode", $ReplayMode,
            "--tab", $TabName,
            "--screenshot", $OutputPath,
            "--quit-after-ms", "2200"
        ) `
        -PassThru

    $finished = $proc.WaitForExit(8000)
    if (-not $finished) {
        Stop-Process -Id $proc.Id -Force
        throw "qt_monitor did not exit within 8 seconds"
    }

    if ($proc.ExitCode -ne 0) {
        throw "qt_monitor exit code: $($proc.ExitCode)"
    }

    Get-Item $OutputPath | Select-Object FullName, Length, LastWriteTime
}

function Get-CaptureTargets {
    param(
        [string]$ReplayMode
    )

    if ($Tab -eq "all") {
        return @(
            @{ Tab = "overview"; Output = Join-Path $OutputDir "qt_mvp_${ReplayMode}_overview.png" },
            @{ Tab = "trend"; Output = Join-Path $OutputDir "qt_mvp_${ReplayMode}_trend.png" },
            @{ Tab = "faults"; Output = Join-Path $OutputDir "qt_mvp_${ReplayMode}_faults.png" },
            @{ Tab = "config"; Output = Join-Path $OutputDir "qt_mvp_${ReplayMode}_config.png" }
        )
    }

    $fileName = if ($Tab -eq "overview") {
        if ($ReplayMode -eq "binary") { "qt_mvp_binary.png" } else { "qt_mvp_jsonl.png" }
    } else {
        "qt_mvp_${ReplayMode}_${Tab}.png"
    }
    return @(@{ Tab = $Tab; Output = Join-Path $OutputDir $fileName })
}

if ($Mode -in @("binary", "both")) {
    foreach ($target in (Get-CaptureTargets -ReplayMode "binary")) {
        Invoke-Capture -ReplayPath $BinaryReplay -ReplayMode "binary" -TabName $target.Tab -OutputPath $target.Output
    }
}

if ($Mode -in @("jsonl", "both")) {
    foreach ($target in (Get-CaptureTargets -ReplayMode "jsonl")) {
        Invoke-Capture -ReplayPath $JsonReplay -ReplayMode "jsonl" -TabName $target.Tab -OutputPath $target.Output
    }
}
