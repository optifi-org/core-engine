#!/bin/bash
# Unified Build & Auto-Dependency Script

mkdir -p build
cd build

# 1. Platform-specific dependency fetching
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    if [ ! -f "wintun.dll" ]; then
        echo "[SETUP] wintun.dll missing. Downloading from official source..."
        # Download Wintun 0.14.1 (Stable)
        curl -L -o wintun.zip https://www.wintun.net/builds/wintun-0.14.1.zip
        
        # Extract only the amd64 dll
        # Note: 'unzip' is standard in MSYS2/Git Bash
        unzip -j wintun.zip wintun/bin/amd64/wintun.dll
        
        rm wintun.zip
        echo "  -> wintun.dll downloaded and deployed."
    fi
fi

# 2. Generate Build Files
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    cmake -G "MinGW Makefiles" ..
else
    cmake ..
fi

# 3. Compile (Universal command that works for Make, Ninja, or MSVC)
cmake --build . --parallel $(nproc)

echo "[SUCCESS] Core Engine is built and ready."
