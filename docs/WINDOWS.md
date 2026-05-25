# Windows Build Guide (Core Engine)

*Note: Windows support for the TAP virtual network interface is highly experimental. It relies on the OpenVPN TAP-Windows6 driver.*

## 1. Prerequisites
- **Compiler**: Visual Studio 2022 (Desktop Development with C++) or MinGW-w64.
- **Build System**: CMake (v3.10+)
- **USB Driver**: Zadig (to install the WinUSB driver for the ESP32 `0x303A:0x4001` device).
- **Libraries**: You must manually download and link `libusb-1.0`.

## 2. Compilation
Open a Developer Command Prompt or PowerShell:
```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## 3. Running
You must run the resulting `optifi-core.exe` as Administrator to allow it to interact with the TAP-Windows6 driver.
```powershell
.\Release\optifi-core.exe
```
