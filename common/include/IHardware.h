#pragma once
#include <cstdint>

namespace optifi {
namespace core {

class IHardware {
public:
    virtual ~IHardware() = default;
    virtual bool Initialize() = 0;
    virtual void SendPacket(const uint8_t* data, uint32_t size) = 0;
    
    /**
     * @brief Reads a packet from the hardware.
     * @return Number of bytes read, or 0 if no data, or -1 on error.
     */
    virtual int ReadPacket(uint8_t* buffer, uint32_t maxSize) = 0;
    virtual bool IsConnected() const = 0;
    virtual void Disconnect() = 0;

};

} // namespace core
} // namespace optifi
