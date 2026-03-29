param(
    [string]$Target = "esp32s3"
)

$ErrorActionPreference = "Stop"

if (Test-Path ".\build") {
    Remove-Item -Recurse -Force ".\build"
}

if (Test-Path ".\sdkconfig") {
    Remove-Item -Force ".\sdkconfig"
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$idfEnv = Join-Path $scriptDir "idf_env.ps1"

& $idfEnv "set-target" $Target
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $idfEnv "reconfigure"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $idfEnv "build"
exit $LASTEXITCODE
