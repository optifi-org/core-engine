#!/bin/bash

# OptiFi Chrome Testing Script

if [ "$EUID" -eq 0 ]; then
  echo "ERROR: Do NOT run this script with sudo!"
  exit 1
fi

echo ">>> Initializing static ARP resolution for the ESP32 bridge (may prompt for sudo)..."
# This fixes the "No route to host" hang when ARP resolution times out over the USB tunnel.
sudo ip neigh replace 10.137.137.2 lladdr 02:00:00:13:37:01 dev optifi0

echo ">>> Temporarily providing DNS (since your default Wi-Fi is disabled)..."
cat /etc/resolv.conf > /tmp/optifi_resolv.bak
echo "nameserver 8.8.8.8" | sudo tee /etc/resolv.conf > /dev/null

cleanup() {
    echo ">>> Chrome closed or script aborted. Performing cleanup..."
    sudo ip route del default via 10.137.137.2 dev optifi0 metric 0 2>/dev/null || true
    sudo ip -6 route del ::/0 dev lo metric 1 2>/dev/null || true
    rm -rf /tmp/optifi-chrome-test
    
    echo ">>> Restoring original DNS configuration..."
    if [ -f /tmp/optifi_resolv.bak ]; then
        cat /tmp/optifi_resolv.bak | sudo tee /etc/resolv.conf > /dev/null
        rm -f /tmp/optifi_resolv.bak
    fi
    echo ">>> Cleanup complete. Normal routing restored."
}
trap cleanup EXIT

echo ">>> Checking if the bridge is actually routing to the internet..."
# Test curl via the standard interface vs optifi0
REGULAR_IP=$(curl -4 -s --max-time 3 https://api.ipify.org || echo "FAILED")
OPTIFI_IP=$(curl -4 -s --max-time 3 --interface optifi0 https://api.ipify.org || echo "FAILED")

echo "Your Regular IP: $REGULAR_IP"
echo "Your OptiFi IP:  $OPTIFI_IP"

if [ -z "$OPTIFI_IP" ]; then
    echo "ERROR: optifi0 is not successfully reaching the internet. Chrome cannot use it."
    exit 1
fi

echo ">>> Setting up aggressive OptiFi routing..."
# Add an absolute highest-priority default route (metric 0)
sudo ip route add default via 10.137.137.2 dev optifi0 metric 0

# Temporarily blackhole IPv6 traffic, because Chrome heavily prefers IPv6 
# and will try to bypass optifi0 by routing over your Wi-Fi's IPv6 network!
sudo ip -6 route add ::/0 dev lo metric 1 2>/dev/null || true

echo ">>> Launching isolated Google Chrome instance..."
google-chrome-stable --user-data-dir=/tmp/optifi-chrome-test --no-first-run --new-window "https://fast.com"

