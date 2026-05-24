#include "UsbHardware.h"
#include <iostream>
#include <cstring>
#include <mutex>

namespace optifi {
namespace hardware {

UsbHardware::UsbHardware(uint16_t vid, uint16_t pid, uint8_t outEndpoint, uint8_t inEndpoint)
    : m_vid(vid), m_pid(pid), m_outEndpoint(outEndpoint), m_inEndpoint(inEndpoint), m_ctx(nullptr), m_handle(nullptr) {
}

UsbHardware::~UsbHardware() {
    Disconnect();
}

bool UsbHardware::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_handle) return true;

    if (!m_ctx) {
        int res = libusb_init(&m_ctx);
        if (res < 0) return false;
    }

    m_handle = libusb_open_device_with_vid_pid(m_ctx, m_vid, m_pid);
    if (!m_handle) {
        std::cerr << "[USB] CRITICAL: Device " << std::hex << m_vid << ":" << m_pid 
                  << " NOT FOUND on USB bus. (Check if ESP32 is in Download Mode)" << std::dec << std::endl;
        return false;
    }

    std::cout << "[USB] Device opened. Resetting..." << std::endl;
    libusb_reset_device(m_handle);
    
    std::cout << "[USB] Setting auto-detach-kernel-driver..." << std::endl;
    libusb_set_auto_detach_kernel_driver(m_handle, 1);

    std::cout << "[USB] Claiming interface 0 (Vendor Data)..." << std::endl;
    int res = libusb_claim_interface(m_handle, 0);
    if (res < 0) {
        std::cerr << "[USB] CRITICAL: Failed to claim interface 0: " << libusb_error_name(res) << std::endl;
        libusb_close(m_handle);
        m_handle = nullptr;
        return false;
    }

    std::cout << "[USB] SUCCESS: Connected to Hardware. OUT=0x" << std::hex << (int)m_outEndpoint 
              << " IN=0x" << (int)m_inEndpoint << std::dec << std::endl;
    return true;
}

void UsbHardware::SendPacket(const uint8_t* data, uint32_t size) {
    if (!m_handle) return;

    int transferred = 0;
    // Reduce timeout to 10ms to avoid stalling the engine loop
    int res = libusb_bulk_transfer(m_handle, m_outEndpoint, const_cast<uint8_t*>(data), size, &transferred, 10);
    if (res < 0) {
        if (res != LIBUSB_ERROR_TIMEOUT) {
            std::cerr << "[USB] Bulk SEND error: " << libusb_error_name(res) << " (Size: " << size << ")" << std::endl;
            if (res == LIBUSB_ERROR_NO_DEVICE || res == LIBUSB_ERROR_IO) {
                Disconnect();
            }
        }
    } else if (transferred != (int)size) {
        std::cerr << "[USB] Partial SEND: requested " << size << ", sent " << transferred << std::endl;
    }
}

int UsbHardware::ReadPacket(uint8_t* buffer, uint32_t maxSize) {
    if (!m_handle) return -1;

    int transferred = 0;
    // Relaxed 5ms timeout for better compatibility
    int res = libusb_bulk_transfer(m_handle, m_inEndpoint, buffer, maxSize, &transferred, 5);
    
    if (res == LIBUSB_SUCCESS) {
        return transferred;
    } else if (res == LIBUSB_ERROR_TIMEOUT) {
        return 0; // Normal timeout
    } else {
        // Only log serious errors, ignore timeouts
        std::cerr << "[USB] READ ERROR: " << libusb_error_name(res) << std::endl;
        if (res == LIBUSB_ERROR_NO_DEVICE || res == LIBUSB_ERROR_IO) {
            Disconnect();
        }
        return -1;
    }
}

bool UsbHardware::IsConnected() const {
    return m_handle != nullptr;
}

void UsbHardware::Disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_handle) {
        std::cout << "[USB] Releasing interface 0 and closing device..." << std::endl;
        libusb_release_interface(m_handle, 0);
        libusb_close(m_handle);
        m_handle = nullptr;
    }
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
}

} // namespace hardware
} // namespace optifi
