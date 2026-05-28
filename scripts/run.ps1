param(
    [switch]$ForceBuild
)

# Unified PowerShell Run Script for OptiFi Core Engine

$ErrorActionPreference = "Stop"

# 1. Check for Administrator Privileges
$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "CRITICAL: This script MUST be run as an Administrator to initialize the network driver."
    Write-Host "Please restart your terminal (PowerShell) as Administrator and try again." -ForegroundColor Yellow
    return
}

# 2. Setup Paths
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Definition
$coreRoot = "$scriptPath\.."
Set-Location $coreRoot
$exePath = "$coreRoot\build\optifi-core-engine.exe"

# 3. Build the engine when needed
if ($ForceBuild -or (-not (Test-Path $exePath))) {
    Write-Host ">>> Building OptiFi Core Engine..." -ForegroundColor Cyan
    & "$scriptPath\build.ps1"
} else {
    Write-Host ">>> Using existing Core Engine build. Pass -ForceBuild to rebuild." -ForegroundColor DarkGray
}

# 4. Run the Engine
if (Test-Path $exePath) {
    Write-Host ">>> Starting Engine..." -ForegroundColor Green
    & $exePath
} else {
    Write-Error "Build failed or executable not found at $exePath"
}
