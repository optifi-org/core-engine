#!/bin/bash
# Automated Linux Test Script

# 1. Build
./scripts/build.sh
cd build

# 2. Start Engine in background
sudo ./optifi-core-engine &
ENGINE_PID=$!

# 3. Wait for initialization
sleep 2

# 4. Configure Networking
echo "[TEST] Configuring optifi0..."
sudo ip addr add 10.137.137.1/24 dev optifi0
sudo ip link set dev optifi0 up

# 5. Run Ping Test
echo "[TEST] Running ping test..."
ping -c 5 10.137.137.2

# 6. Cleanup
echo "[TEST] Cleaning up..."
sudo kill $ENGINE_PID
wait $ENGINE_PID 2>/dev/null
echo "[SUCCESS] Test complete."
