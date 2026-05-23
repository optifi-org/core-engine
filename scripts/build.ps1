# Unified Build & Auto-Dependency Script for Windows

# 1. Create and enter build directory
if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build"
}
Set-Location "build"

# 2. Platform-specific dependency fetching (Wintun)
if (-not (Test-Path "wintun.dll")) {
    Write-Host "[SETUP] wintun.dll missing. Downloading from official source..." -ForegroundColor Cyan
    
    $url = "https://www.wintun.net/builds/wintun-0.14.1.zip"
    $zipPath = "wintun.zip"
    
    Invoke-WebRequest -Uri $url -OutFile $zipPath
    
    # Extract only the amd64 dll
    Expand-Archive -Path $zipPath -DestinationPath "temp_wintun" -Force
    Move-Item -Path "temp_wintun\wintun\bin\amd64\wintun.dll" -Destination "." -Force
    
    # Cleanup
    Remove-Item -Path $zipPath -Force
    Remove-Item -Path "temp_wintun" -Recurse -Force
    
    Write-Host "  -> wintun.dll downloaded and deployed." -ForegroundColor Green
}

# 3. Generate Build Files (Using MinGW Makefiles)
Write-Host ">>> Generating Build Files..." -ForegroundColor Cyan
cmake -G "MinGW Makefiles" ..

# 4. Compile
Write-Host ">>> Compiling Core Engine..." -ForegroundColor Cyan
cmake --build . --parallel $env:NUMBER_OF_PROCESSORS

Write-Host "[SUCCESS] Core Engine is built and ready." -ForegroundColor Green
