param(
    [string]$BuildDir = "C:\code\esp32-temperature-telemetry-platform\build-desktop",
    [switch]$Configure,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$configureScript = Join-Path $scriptDir "desktop_configure.ps1"
$cmake = "C:\app\Qt\Tools\CMake_64\bin\cmake.exe"

if ($Configure -or -not (Test-Path $BuildDir)) {
    & $configureScript -BuildDir $BuildDir -Clean:$Clean
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

& $cmake --build $BuildDir
exit $LASTEXITCODE
