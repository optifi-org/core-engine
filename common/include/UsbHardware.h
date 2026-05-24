#pragma once
#include "IHardware.h"
#include <libusb-1.0/libusb.h>
#include <string>
#include <mutex>

namespace optifi {
namespace hardware {

class UsbHardware : public core::IHardware {
public:
    UsbHardware(uint16_t vid, uint16_t pid, uint8_t outEndpoint, uint8_t inEndpoint);
    ~UsbHardware();

    bool Initialize() override;
    void SendPacket(const uint8_t* data, uint32_t size) override;
    int ReadPacket(uint8_t* buffer, uint32_t maxSize) override;
    bool IsConnected() const override;
    void Disconnect() override;

private:
    uint16_t m_vid;
    uint16_t m_pid;
    uint8_t m_outEndpoint;
    uint8_t m_inEndpoint;
    libusb_context* m_ctx;
    libusb_device_handle* m_handle;
    std::mutex m_mutex;
};

} // namespace hardware
} // namespace optifi
