# Automated Windows Test Script (Run as Administrator)

# 1. Build
& .\scripts\build.ps1
Set-Location build

# 2. Start Engine in background
$process = Start-Process ./optifi-core-engine.exe -PassThru -WindowStyle Hidden

# 3. Wait for initialization
Start-Sleep -Seconds 3

# 4. Configure Networking
Write-Host "[TEST] Configuring Wintun Adapter..."
netsh interface ip set address name="OptiFi" static 10.137.137.1 255.255.255.0

# 5. Run Ping Test
Write-Host "[TEST] Running ping test..."
ping -n 5 10.137.137.2

# 6. Cleanup
Write-Host "[TEST] Cleaning up..."
Stop-Process -Id $process.Id
Write-Host "[SUCCESS] Test complete."
