# Unified PowerShell Run Script for OptiFi Core Engine

# 1. Check for Administrator Privileges
$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Warning "CRITICAL: This script should be run as an Administrator to initialize the network driver."
}

# 2. Setup Paths
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Definition
$coreRoot = "$scriptPath\.."
Set-Location $coreRoot

# 3. Build the engine
Write-Host ">>> Building OptiFi Core Engine..." -ForegroundColor Cyan
& "$scriptPath\build.ps1"

# 4. Run the Engine
$exePath = "$coreRoot\build\optifi-core-engine.exe"
if (Test-Path $exePath) {
    Write-Host ">>> Starting Engine..." -ForegroundColor Green
    & $exePath
} else {
    Write-Error "Build failed or executable not found at $exePath"
}
