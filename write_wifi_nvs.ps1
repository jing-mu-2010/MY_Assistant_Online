param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [string]$Ssid,

    [Parameter(Mandatory = $true)]
    [string]$Password,

    [string]$OpenAIKey = "sk-xxxxxxxx"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $projectRoot "build"
$csvPath = Join-Path $buildDir "wifi_nvs.csv"
$binPath = Join-Path $buildDir "wifi_nvs.bin"
$nvsGen = Join-Path $env:IDF_PATH "components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py"

if (-not $env:IDF_PATH) {
    throw "IDF_PATH is not set. Run export.bat first."
}

if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

$csvLines = @(
    "key,type,encoding,value"
    "configuration,namespace,,"
    "ssid,data,string,""$Ssid"""
    "password,data,string,""$Password"""
    "ChatGPT_key,data,string,""$OpenAIKey"""
)

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($csvPath, $csvLines, $utf8NoBom)

python $nvsGen generate $csvPath $binPath 0x4000
python -m esptool --chip esp32s3 -p $Port -b 460800 --before default_reset --after hard_reset write_flash 0x9000 $binPath

Write-Host "NVS Wi-Fi configuration flashed to 0x9000."
