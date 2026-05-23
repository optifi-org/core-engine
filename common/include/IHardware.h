#pragma once
#include <cstdint>

namespace optifi {
namespace core {

class IHardware {
public:
    virtual ~IHardware() = default;
    virtual bool Initialize() = 0;
    virtual void SendPacket(const uint8_t* data, uint32_t size) = 0;
    virtual void Disconnect() = 0;
};

} // namespace core
} // namespace optifi
