#pragma once
#include "IHardware.h"
#include <libusb-1.0/libusb.h>
#include <string>

namespace optifi {
namespace hardware {

class UsbHardware : public core::IHardware {
public:
    UsbHardware(uint16_t vid, uint16_t pid, uint8_t endpoint);
    ~UsbHardware();

    bool Initialize() override;
    void SendPacket(const uint8_t* data, uint32_t size) override;
    void Disconnect() override;

private:
    uint16_t m_vid;
    uint16_t m_pid;
    uint8_t m_endpoint;
    libusb_context* m_ctx;
    libusb_device_handle* m_handle;
};

} // namespace hardware
} // namespace optifi
