# Unified PowerShell Run Script for OptiFi Core Engine

# 1. Check for Administrator Privileges
$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "CRITICAL: This script must be run as an Administrator to initialize the network driver."
    return
}

# 2. Navigate to core-engine root
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location "$scriptPath\.."

# 3. Build the engine
Write-Host ">>> Building OptiFi Core Engine..." -ForegroundColor Cyan
# We call the existing build logic
bash ./scripts/build.sh

# 4. Run the Engine
if (Test-Path ".\build\optifi-core-engine.exe") {
    Write-Host ">>> Starting Engine..." -ForegroundColor Green
    .\build\optifi-core-engine.exe
} else {
    Write-Error "Build failed or executable not found."
}
