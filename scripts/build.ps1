# Unified Build & Auto-Dependency Script for Windows

# 0. Prerequisite Check
if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
    Write-Error "CRITICAL: 'cmake' not found! Please install CMake and add it to your PATH."
    Write-Host "Tip: You can install it via winget: 'winget install kitware.cmake'" -ForegroundColor Yellow
    return
}

# 1. Navigate to the core-engine root
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location "$scriptPath\.."

# 2. CLEANUP: If build directory exists but is poisoned (e.g. from Linux), clear it.
if (Test-Path "build\CMakeCache.txt") {
    Write-Host "[CLEAN] Detected existing CMake cache. Clearing it to prevent cross-platform poisoning..." -ForegroundColor Yellow
    Remove-Item -Path "build\*" -Recurse -Force
}

# 3. Create build directory
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build"
}
Set-Location "build"

# 4. Platform-specific dependency fetching (Wintun)
if (-not (Test-Path "wintun.dll")) {
    Write-Host "[SETUP] wintun.dll missing. Downloading..." -ForegroundColor Cyan
    $url = "https://www.wintun.net/builds/wintun-0.14.1.zip"
    Invoke-WebRequest -Uri $url -OutFile "wintun.zip"
    Expand-Archive -Path "wintun.zip" -DestinationPath "temp_wintun" -Force
    Move-Item -Path "temp_wintun\wintun\bin\amd64\wintun.dll" -Destination "." -Force
    Remove-Item -Path "wintun.zip", "temp_wintun" -Recurse -Force
}

# 5. Generate Build Files
Write-Host ">>> Generating Build Files (Windows)..." -ForegroundColor Cyan
# We use MinGW Makefiles as the hardware team's log shows they have 'make'
cmake -G "MinGW Makefiles" ..

# 6. Compile
Write-Host ">>> Compiling Core Engine..." -ForegroundColor Cyan
cmake --build . --parallel $env:NUMBER_OF_PROCESSORS

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed during compilation."
    return
}

Write-Host "[SUCCESS] Core Engine is built and ready." -ForegroundColor Green
