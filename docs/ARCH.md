# Arch Linux Build Guide (Core Engine)

## 1. Prerequisites
You need a C++17 compiler, CMake, and the `libusb` development package.

```bash
sudo pacman -Syu
sudo pacman -S base-devel cmake pkg-config libusb iproute2 ethtool
```

## 2. Compilation
Compile the daemon using the provided build script.
```bash
./scripts/build.sh
```

## 3. Running
The Core Engine modifies kernel routing tables and provisions virtual TAP devices. **It must be run as root.**
```bash
sudo ./scripts/run.sh
```
