# OptiFi Core Engine

The OptiFi Core Engine is a high-performance C++ system daemon that bridges local host network traffic over USB to the ESP32 hardware node. It provisions a virtual Linux network adapter (TAP interface), manages a credit-based USB bulk-transfer flow control system, and acts as an IPC server to broadcast live telemetry to the OptiFi Frontend dashboard.

## Prerequisites & System Requirements

The Core Engine interacts directly with the Linux kernel's networking stack to create TAP devices, and interfaces with the USB hardware via `libusb`.

### 1. General Requirements
* **OS:** Linux (Windows support is experimental/stubbed)
* **Compiler:** GCC or Clang (C++17 support required)
* **Build System:** CMake (v3.10 or higher) and Make
* **Permissions:** Root / `sudo` access is mandatory when running the engine to allow the configuration of the `/dev/net/tun` network interface.

### 2. Linux-Specific Dependencies
You must install the necessary C++ build tools and networking utilities required for the network bridge setup.

**Dependency Installation (Debian/Ubuntu):**
```bash
# Update package lists
sudo apt update

# Install build tools, libusb, and networking utilities
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev iproute2 ethtool
```
*Note: `iproute2` provides the `ip` command, and `ethtool` is used to disable checksum offloading on the virtual TAP adapter.*

## Installation & Setup

1. **Build the Engine:**
   You can compile the daemon using the provided build script:
   ```bash
   ./scripts/build.sh
   ```
   *Alternatively, you can build manually using CMake:*
   ```bash
   mkdir -p build && cd build
   cmake ..
   make
   ```

2. **Run the Engine:**
   Because the engine provisions the `optifi0` network interface and manipulates routing tables, it **must** be run with root privileges.
   ```bash
   sudo ./scripts/run.sh
   ```
   > **Note:** Once running, the engine will establish a Unix socket (`/tmp/optifi.sock`) to securely communicate live bridging statistics and telemetry with the frontend UI.
