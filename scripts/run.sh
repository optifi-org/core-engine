#!/bin/bash
# Unified Run Script for OptiFi Core Engine

# 1. Navigate to the core-engine root if called from elsewhere
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR/.."

# 2. Build the engine to ensure it's up to date
echo ">>> Building OptiFi Core Engine..."
./scripts/build.sh

# 3. Detect Platform and Run
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]]; then
    echo ">>> Detected Windows. Starting Engine..."
    # Note: On Windows, the terminal should already be Elevated (Administrator)
    ./build/optifi-core-engine.exe
else
    echo ">>> Detected Linux. Starting Engine (Requires sudo)..."
    sudo ./build/optifi-core-engine
fi
