#include "UsbHardware.h"
#include <iostream>
#include <cstring>

namespace optifi {
namespace hardware {

UsbHardware::UsbHardware(uint16_t vid, uint16_t pid, uint8_t endpoint)
    : m_vid(vid), m_pid(pid), m_endpoint(endpoint), m_ctx(nullptr), m_handle(nullptr) {
}

UsbHardware::~UsbHardware() {
    Disconnect();
}

bool UsbHardware::Initialize() {
    int res = libusb_init(&m_ctx);
    if (res < 0) {
        std::cerr << "[USB] Failed to initialize libusb: " << libusb_error_name(res) << std::endl;
        return false;
    }

    m_handle = libusb_open_device_with_vid_pid(m_ctx, m_vid, m_pid);
    if (!m_handle) {
        // Don't print error here, as we might fallback to MockHardware silently
        return false;
    }

    if (libusb_kernel_driver_active(m_handle, 0) == 1) {
        libusb_detach_kernel_driver(m_handle, 0);
    }

    res = libusb_claim_interface(m_handle, 0);
    if (res < 0) {
        std::cerr << "[USB] Failed to claim interface: " << libusb_error_name(res) << std::endl;
        libusb_close(m_handle);
        m_handle = nullptr;
        return false;
    }

    std::cout << "[USB] Connected to OptiFi Hardware (VID=0x" << std::hex << m_vid 
              << " PID=0x" << m_pid << std::dec << ")" << std::endl;
    return true;
}

void UsbHardware::SendPacket(const uint8_t* data, uint32_t size) {
    if (!m_handle) return;

    int transferred = 0;
    int res = libusb_bulk_transfer(m_handle, m_endpoint, const_cast<uint8_t*>(data), size, &transferred, 1000);
    if (res < 0) {
        std::cerr << "[USB] Transfer failed: " << libusb_error_name(res) << std::endl;
    }
}

void UsbHardware::Disconnect() {
    if (m_handle) {
        libusb_release_interface(m_handle, 0);
        libusb_close(m_handle);
        m_handle = nullptr;
        std::cout << "[USB] Disconnected from hardware." << std::endl;
    }
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
}

} // namespace hardware
} // namespace optifi
