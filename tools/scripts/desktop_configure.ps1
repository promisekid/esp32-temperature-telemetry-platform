param(
    [string]$BuildDir = "C:\code\esp32-temperature-telemetry-platform\build-desktop",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

$cmake = "C:\app\Qt\Tools\CMake_64\bin\cmake.exe"
$sourceDir = "C:\code\esp32-temperature-telemetry-platform"

& $cmake `
    -G Ninja `
    -S $sourceDir `
    -B $BuildDir `
    -DBUILD_DESKTOP=ON `
    -DBUILD_QT_MONITOR=ON `
    -DCMAKE_MAKE_PROGRAM="C:\Espressif\tools\ninja\1.12.1\ninja.exe" `
    -DCMAKE_C_COMPILER="C:\app\Qt\Tools\mingw1310_64\bin\gcc.exe" `
    -DCMAKE_CXX_COMPILER="C:\app\Qt\Tools\mingw1310_64\bin\g++.exe" `
    -DQt6_DIR="C:\app\Qt\6.9.3\mingw_64\lib\cmake\Qt6"

exit $LASTEXITCODE
