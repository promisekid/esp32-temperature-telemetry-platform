param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

$ErrorActionPreference = "Stop"

$env:IDF_TOOLS_PATH = "C:\Espressif\tools"
$env:IDF_COMPONENT_LOCAL_STORAGE_URL = "file://C:\Espressif\tools"
$env:IDF_PATH = "C:\app\esp-idf\.espressif\v5.4.3\esp-idf"
$env:ESP_ROM_ELF_DIR = "C:\Espressif\tools\esp-rom-elfs\20241011"
$env:OPENOCD_SCRIPTS = "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20251215\openocd-esp32\share\openocd\scripts"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v5.4.3\venv"
$env:ESP_IDF_VERSION = "5.4"

$toolPath = @(
    "C:\Espressif\tools\ccache\4.11.2\ccache-4.11.2-windows-x86_64",
    "C:\Espressif\tools\cmake\3.30.2\bin",
    "C:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64",
    "C:\Espressif\tools\esp-clang\esp-18.1.2_20240912\esp-clang\bin",
    "C:\Espressif\tools\esp-rom-elfs\20241011",
    "C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
    "C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\esp32ulp-elf\bin",
    "C:\Espressif\tools\idf-exe\1.0.3",
    "C:\Espressif\tools\ninja\1.12.1",
    "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20250707\openocd-esp32\bin",
    "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20250730\riscv32-esp-elf\bin",
    "C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20250730\riscv32-esp-elf\riscv32-esp-elf\bin",
    "C:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin",
    "C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\bin",
    "C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20250730\xtensa-esp-elf\xtensa-esp-elf\bin",
    "C:\Espressif\tools\python\v5.4.3\venv\Scripts"
)

$env:PATH = ($toolPath -join ";") + ";" + $env:PATH
$python = "C:\Espressif\tools\python\v5.4.3\venv\Scripts\python.exe"
$idfPy = "C:\app\esp-idf\.espressif\v5.4.3\esp-idf\tools\idf.py"

if ($IdfArgs.Count -eq 0) {
    & $python $idfPy --version
    exit $LASTEXITCODE
}

& $python $idfPy @IdfArgs
exit $LASTEXITCODE
