param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

Write-Host "ESP32 AI BOX1 manual flash"
Write-Host "Project: $ProjectRoot"
Write-Host "Port:    $Port"
Write-Host "Baud:    $Baud"
Write-Host ""

$RequiredFiles = @(
    "build\bootloader\bootloader.bin",
    "build\partition_table\partition-table.bin",
    "build\ota_data_initial.bin",
    "build\chatgpt_demo.bin",
    "build\storage.bin",
    "build\srmodels\srmodels.bin"
)

$Missing = @()
foreach ($File in $RequiredFiles) {
    if (-not (Test-Path $File)) {
        $Missing += $File
    }
}

if ($Missing.Count -gt 0) {
    Write-Host "Missing flash files. Build the main project first:" -ForegroundColor Red
    foreach ($File in $Missing) {
        Write-Host "  - $File" -ForegroundColor Red
    }
    Write-Host ""
    Write-Host "Main build:"
    Write-Host "  cd $ProjectRoot"
    Write-Host "  idf.py build"
    exit 1
}

Write-Host "All flash files found." -ForegroundColor Green
Write-Host "Flashing with fixed partition addresses..."
Write-Host ""

python -m esptool --chip esp32s3 -p $Port -b $Baud --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m `
0x0 build\bootloader\bootloader.bin `
0x8000 build\partition_table\partition-table.bin `
0xd000 build\ota_data_initial.bin `
0x10000 build\chatgpt_demo.bin `
0x900000 build\storage.bin `
0xb00000 build\srmodels\srmodels.bin

Write-Host ""
Write-Host "Flash command finished."
Write-Host "Open monitor with:"
Write-Host "  idf.py -p $Port monitor"
