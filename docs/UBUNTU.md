# Ubuntu / Debian Build Guide (Core Engine)

## 1. Prerequisites
You need a C++17 compiler, CMake, and the `libusb-1.0` development headers. You also need standard networking tools to manage the TAP interface.

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev iproute2 ethtool
```

## 2. Compilation
You can compile the daemon using the provided build script, or manually via CMake.
```bash
./scripts/build.sh
```
*(Manual build: `mkdir build && cd build && cmake .. && make`)*

## 3. Running
The Core Engine creates virtual network interfaces (`/dev/net/tun`) and establishes aggressive IP routing tables. **It must be run as root.**
```bash
sudo ./scripts/run.sh
```
