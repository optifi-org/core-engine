# OptiFi Core Engine

The OptiFi Core Engine is a high-performance C++ system daemon that bridges local host network traffic over USB to the ESP32 hardware node. 

## 📚 OS-Specific Build Guides
To compile and run the Core Engine daemon, please refer to the specific setup instructions for your operating system:
- [Ubuntu / Debian](docs/UBUNTU.md)
- [Arch Linux](docs/ARCH.md)
- [Windows](docs/WINDOWS.md)

---

## 🏗️ Architectural Overview & Modules

The Core Engine is built to bypass standard network bottlenecks, using `libusb` and the Linux kernel's Universal TUN/TAP driver to push hardware-accelerated traffic.

### 1. The Bridge Loop (`common/src/main.cpp`)
**Why it was implemented:** We need a main event loop that continuously polls the virtual network interface and the USB hardware to shuttle packets synchronously.
**Signatures & Logic:**
- Uses standard POSIX `read()` and `write()` calls to interact with the Linux `optifi0` TAP interface.
- **Framing Protocol:** When pushing data from USB to the TAP interface, it parses the custom byte-level framing protocol (`'O'`, `'P'`, `'T'`, `'I'`, followed by a length header) to guarantee packet integrity across USB batch boundaries.
- **IPC Telemetry Server:** Provisions a Unix Domain Socket at `/tmp/optifi.sock`. It formats live bridge statistics into pipe-delimited strings (e.g., `BRIDGE_STATS|tx_bytes|rx_bytes|credits`) and broadcasts them to any connected GUI clients.

### 2. Linux Platform Module (`platform/linux/src/PlatformLinux.cpp`)
**Why it was implemented:** Direct manipulation of the OS network stack is platform-specific. This file isolates Linux kernel-level operations.
**Signatures & Logic:**
- `PlatformLinux::CreateTapDevice(...)`: Interfaces with `/dev/net/tun` via `ioctl(fd, TUNSETIFF, ...)` to spawn a raw Layer-2 Ethernet TAP device named `optifi0`.
- Issues `ifconfig` (or `ip link`) commands dynamically to set the MAC address to `02:00:00:13:37:00` and bring the interface UP.

### 3. USB Hardware Integrator (`common/src/UsbHardware.cpp`)
**Why it was implemented:** To talk directly to the TinyUSB vendor endpoints on the ESP32 without relying on slow OS-level CDC/serial drivers.
**Signatures & Logic:**
- Interfaces with `libusb-1.0`.
- `UsbHardware::SendPacket(...)`: Dispatches data to the `0x01` Bulk OUT endpoint.
- `UsbHardware::Poll(...)`: Actively drains the `0x81` Bulk IN endpoint.
- **Burst Batching Optimization:** It aggressively loops up to 20 times per poll cycle, draining massive multi-megabit bursts from the USB FIFO instantly, completely eliminating software-induced bottlenecks and achieving esports-level ping times.
