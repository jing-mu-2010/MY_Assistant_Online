param(
    [switch]$NoHistory
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $ProjectRoot

Write-Host "Project directory: $ProjectRoot"

$RequiredFiles = @(
    "build\chatgpt_demo.bin",
    "build\bootloader\bootloader.bin",
    "build\partition_table\partition-table.bin"
)

$Missing = @()
foreach ($File in $RequiredFiles) {
    if (-not (Test-Path $File)) {
        $Missing += $File
    }
}

if ($Missing.Count -gt 0) {
    Write-Host "Build artifacts are missing. Run: idf.py build" -ForegroundColor Red
    foreach ($File in $Missing) {
        Write-Host "  - $File" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Build artifact check passed." -ForegroundColor Green

if (-not $env:MFG_AGENT_API_KEY) {
    $SecureKey = Read-Host "Enter manufacturer agent API_KEY" -AsSecureString
    $PlainKey = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureKey)
    )
    $env:MFG_AGENT_API_KEY = $PlainKey
}

$ArgsList = @(".\tools\pc_agent_chat.py", "--check-build")
if ($NoHistory) {
    $ArgsList += "--no-history"
}

python @ArgsList
